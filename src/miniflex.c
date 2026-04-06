/*
 * miniflex — Compact FlexBuffers reader/writer in pure C.
 *
 * Supports the full FlexBuffers wire format, plus miniflex-specific mutable
 * string/blob allocation helpers.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "miniflex/miniflex.h"

/* ── Bit width helpers ──────────────────────────────────────────── */

static inline uint8_t bw_from_log2(uint8_t log2) {
    return (uint8_t)(1u << log2);
}

static inline uint8_t log2_from_bw(uint8_t bw) {
    switch (bw) {
        case 1: return 0;
        case 2: return 1;
        case 4: return 2;
        case 8: return 3;
        default: return 0xFF;
    }
}

static inline bool bw_is_valid(uint8_t bw) {
    return bw == 1 || bw == 2 || bw == 4 || bw == 8;
}

static inline uint8_t packed_type(uint8_t type, uint8_t bit_width_log2) {
    return (uint8_t)((type << 2) | bit_width_log2);
}

/* ── Type helpers ───────────────────────────────────────────────── */

static inline bool type_is_known(uint8_t type) {
    return type <= MFX_FBT_BOOL || type == MFX_FBT_VECTOR_BOOL;
}

static inline bool type_is_inline_scalar(uint8_t type) {
    return type == MFX_FBT_NULL || type == MFX_FBT_INT || type == MFX_FBT_UINT ||
           type == MFX_FBT_FLOAT || type == MFX_FBT_BOOL;
}

static inline bool type_is_indirect_scalar(uint8_t type) {
    return type == MFX_FBT_INDIRECT_INT || type == MFX_FBT_INDIRECT_UINT ||
           type == MFX_FBT_INDIRECT_FLOAT;
}

static inline bool type_is_typed_vector(uint8_t type) {
    return (type >= MFX_FBT_VECTOR_INT && type <= MFX_FBT_VECTOR_STRING_DEPRECATED) ||
           type == MFX_FBT_VECTOR_BOOL;
}

static inline bool type_is_fixed_typed_vector(uint8_t type) {
    return type >= MFX_FBT_VECTOR_INT2 && type <= MFX_FBT_VECTOR_FLOAT4;
}

static inline bool type_is_vector_like(uint8_t type) {
    return type == MFX_FBT_VECTOR || type == MFX_FBT_MAP ||
           type_is_typed_vector(type) || type_is_fixed_typed_vector(type);
}

static inline mfx_type typed_vector_elem_type(uint8_t type) {
    switch (type) {
        case MFX_FBT_VECTOR_INT: return MFX_FBT_INT;
        case MFX_FBT_VECTOR_UINT: return MFX_FBT_UINT;
        case MFX_FBT_VECTOR_FLOAT: return MFX_FBT_FLOAT;
        case MFX_FBT_VECTOR_KEY: return MFX_FBT_KEY;
        case MFX_FBT_VECTOR_STRING_DEPRECATED: return MFX_FBT_STRING;
        case MFX_FBT_VECTOR_BOOL: return MFX_FBT_BOOL;
        default: return MFX_FBT_NULL;
    }
}

static inline mfx_type fixed_vector_elem_type(uint8_t type) {
    switch (type) {
        case MFX_FBT_VECTOR_INT2:
        case MFX_FBT_VECTOR_INT3:
        case MFX_FBT_VECTOR_INT4:
            return MFX_FBT_INT;
        case MFX_FBT_VECTOR_UINT2:
        case MFX_FBT_VECTOR_UINT3:
        case MFX_FBT_VECTOR_UINT4:
            return MFX_FBT_UINT;
        case MFX_FBT_VECTOR_FLOAT2:
        case MFX_FBT_VECTOR_FLOAT3:
        case MFX_FBT_VECTOR_FLOAT4:
            return MFX_FBT_FLOAT;
        default:
            return MFX_FBT_NULL;
    }
}

static inline uint32_t fixed_vector_arity(uint8_t type) {
    switch (type) {
        case MFX_FBT_VECTOR_INT2:
        case MFX_FBT_VECTOR_UINT2:
        case MFX_FBT_VECTOR_FLOAT2:
            return 2;
        case MFX_FBT_VECTOR_INT3:
        case MFX_FBT_VECTOR_UINT3:
        case MFX_FBT_VECTOR_FLOAT3:
            return 3;
        case MFX_FBT_VECTOR_INT4:
        case MFX_FBT_VECTOR_UINT4:
        case MFX_FBT_VECTOR_FLOAT4:
            return 4;
        default:
            return 0;
    }
}

static inline bool type_is_offset_encoded(uint8_t type) {
    return !type_is_inline_scalar(type);
}

static inline bool type_is_typed_vector_elem(uint8_t type) {
    return type == MFX_FBT_INT || type == MFX_FBT_UINT || type == MFX_FBT_FLOAT ||
           type == MFX_FBT_KEY || type == MFX_FBT_STRING || type == MFX_FBT_BOOL;
}

static inline bool type_is_fixed_vector_elem(uint8_t type) {
    return type == MFX_FBT_INT || type == MFX_FBT_UINT || type == MFX_FBT_FLOAT;
}

static inline uint8_t typed_vector_wire_type(uint8_t elem_type, uint32_t arity) {
    if (arity == 0) {
        switch (elem_type) {
            case MFX_FBT_INT: return MFX_FBT_VECTOR_INT;
            case MFX_FBT_UINT: return MFX_FBT_VECTOR_UINT;
            case MFX_FBT_FLOAT: return MFX_FBT_VECTOR_FLOAT;
            case MFX_FBT_KEY: return MFX_FBT_VECTOR_KEY;
            case MFX_FBT_STRING: return MFX_FBT_VECTOR_STRING_DEPRECATED;
            case MFX_FBT_BOOL: return MFX_FBT_VECTOR_BOOL;
            default: return MFX_FBT_NULL;
        }
    }

    switch (arity) {
        case 2:
            switch (elem_type) {
                case MFX_FBT_INT: return MFX_FBT_VECTOR_INT2;
                case MFX_FBT_UINT: return MFX_FBT_VECTOR_UINT2;
                case MFX_FBT_FLOAT: return MFX_FBT_VECTOR_FLOAT2;
                default: return MFX_FBT_NULL;
            }
        case 3:
            switch (elem_type) {
                case MFX_FBT_INT: return MFX_FBT_VECTOR_INT3;
                case MFX_FBT_UINT: return MFX_FBT_VECTOR_UINT3;
                case MFX_FBT_FLOAT: return MFX_FBT_VECTOR_FLOAT3;
                default: return MFX_FBT_NULL;
            }
        case 4:
            switch (elem_type) {
                case MFX_FBT_INT: return MFX_FBT_VECTOR_INT4;
                case MFX_FBT_UINT: return MFX_FBT_VECTOR_UINT4;
                case MFX_FBT_FLOAT: return MFX_FBT_VECTOR_FLOAT4;
                default: return MFX_FBT_NULL;
            }
        default:
            return MFX_FBT_NULL;
    }
}

/* ── Read primitives ────────────────────────────────────────────── */

static uint64_t read_uint(const uint8_t *p, uint8_t bw) {
    switch (bw) {
        case 1: return p[0];
        case 2: { uint16_t v; memcpy(&v, p, 2); return v; }
        case 4: { uint32_t v; memcpy(&v, p, 4); return v; }
        case 8: { uint64_t v; memcpy(&v, p, 8); return v; }
        default: return 0;
    }
}

static int64_t read_int(const uint8_t *p, uint8_t bw) {
    switch (bw) {
        case 1: return (int8_t)p[0];
        case 2: { int16_t v; memcpy(&v, p, 2); return v; }
        case 4: { int32_t v; memcpy(&v, p, 4); return v; }
        case 8: { int64_t v; memcpy(&v, p, 8); return v; }
        default: return 0;
    }
}

static double read_float(const uint8_t *p, uint8_t bw) {
    if (bw == 4) {
        float v;
        memcpy(&v, p, 4);
        return (double)v;
    }
    if (bw == 8) {
        double v;
        memcpy(&v, p, 8);
        return v;
    }
    return 0.0;
}

static const uint8_t *deref(const uint8_t *p, uint8_t bw) {
    return p - read_uint(p, bw);
}

/* ── Width helpers ──────────────────────────────────────────────── */

static uint8_t width_for_int(int64_t val) {
    if (val >= -128 && val <= 127) return 0;
    if (val >= -32768 && val <= 32767) return 1;
    if (val >= -2147483648LL && val <= 2147483647LL) return 2;
    return 3;
}

static uint8_t width_for_uint(uint64_t val) {
    if (val <= 0xFFu) return 0;
    if (val <= 0xFFFFu) return 1;
    if (val <= 0xFFFFFFFFu) return 2;
    return 3;
}

static uint8_t width_for_float(double val) {
    float f = (float)val;
    if ((double)f == val || (val != val && (double)f != (double)f)) return 2;
    return 3;
}

static uint8_t width_for_offset(uint32_t offset, uint32_t from_pos) {
    return width_for_uint((uint64_t)(from_pos - offset));
}

static bool int_fits_width(int64_t val, uint8_t bw) {
    switch (bw) {
        case 1: return val >= INT8_MIN && val <= INT8_MAX;
        case 2: return val >= INT16_MIN && val <= INT16_MAX;
        case 4: return val >= INT32_MIN && val <= INT32_MAX;
        case 8: return true;
        default: return false;
    }
}

static bool uint_fits_width(uint64_t val, uint8_t bw) {
    switch (bw) {
        case 1: return val <= UINT8_MAX;
        case 2: return val <= UINT16_MAX;
        case 4: return val <= UINT32_MAX;
        case 8: return true;
        default: return false;
    }
}

static bool float_fits_width(double val, uint8_t bw) {
    (void)val;
    return bw == 4 || bw == 8;
}

/* ── Reader ─────────────────────────────────────────────────────── */

typedef struct {
    const uint8_t *data;
    uint32_t size;
    uint8_t byte_width;
    bool has_type_table;
    mfx_type elem_type;
    bool is_map;
} vector_info;

static bool get_vector_info(mfx_ref ref, vector_info *out) {
    const uint8_t *p;

    if (!type_is_vector_like(ref.type)) return false;
    if (!bw_is_valid(ref.parent_width) || !bw_is_valid(ref.byte_width)) return false;

    memset(out, 0, sizeof(*out));
    out->byte_width = ref.byte_width;
    out->is_map = ref.type == MFX_FBT_MAP;

    p = deref(ref.data, ref.parent_width);
    out->data = p;

    if (ref.type == MFX_FBT_VECTOR || ref.type == MFX_FBT_MAP) {
        out->size = (uint32_t)read_uint(p - ref.byte_width, ref.byte_width);
        out->has_type_table = true;
        out->elem_type = MFX_FBT_NULL;
        return true;
    }

    if (type_is_typed_vector(ref.type)) {
        out->size = (uint32_t)read_uint(p - ref.byte_width, ref.byte_width);
        out->has_type_table = false;
        out->elem_type = typed_vector_elem_type(ref.type);
        return true;
    }

    out->size = fixed_vector_arity(ref.type);
    out->has_type_table = false;
    out->elem_type = fixed_vector_elem_type(ref.type);
    return true;
}

mfx_ref mfx_root(const uint8_t *buf, uint32_t size) {
    mfx_ref ref = {0};

    if (!buf || size < 3) return ref;
    if (!bw_is_valid(buf[size - 1])) return ref;

    ref.data = buf + size - 2 - buf[size - 1];
    ref.parent_width = buf[size - 1];
    ref.byte_width = bw_from_log2(buf[size - 2] & 3);
    ref.type = buf[size - 2] >> 2;
    if (!type_is_known(ref.type)) return (mfx_ref){0};
    return ref;
}

mfx_type mfx_type_of(mfx_ref ref) { return (mfx_type)ref.type; }
bool mfx_is_null(mfx_ref ref) { return ref.type == MFX_FBT_NULL; }
bool mfx_is_int(mfx_ref ref) { return ref.type == MFX_FBT_INT || ref.type == MFX_FBT_INDIRECT_INT; }
bool mfx_is_uint(mfx_ref ref) { return ref.type == MFX_FBT_UINT || ref.type == MFX_FBT_INDIRECT_UINT; }
bool mfx_is_float(mfx_ref ref) { return ref.type == MFX_FBT_FLOAT || ref.type == MFX_FBT_INDIRECT_FLOAT; }
bool mfx_is_numeric(mfx_ref ref) { return mfx_is_int(ref) || mfx_is_uint(ref) || mfx_is_float(ref); }
bool mfx_is_string(mfx_ref ref) { return ref.type == MFX_FBT_STRING; }
bool mfx_is_blob(mfx_ref ref) { return ref.type == MFX_FBT_BLOB; }
bool mfx_is_map(mfx_ref ref) { return ref.type == MFX_FBT_MAP; }
bool mfx_is_vector(mfx_ref ref) { return type_is_vector_like(ref.type); }
bool mfx_is_typed_vector(mfx_ref ref) { return type_is_typed_vector(ref.type); }
bool mfx_is_fixed_typed_vector(mfx_ref ref) { return type_is_fixed_typed_vector(ref.type); }
bool mfx_is_bool(mfx_ref ref) { return ref.type == MFX_FBT_BOOL; }

int64_t mfx_int(mfx_ref ref) {
    if (ref.type == MFX_FBT_INDIRECT_INT) return read_int(deref(ref.data, ref.parent_width), ref.byte_width);
    if (ref.type == MFX_FBT_INT) return read_int(ref.data, ref.parent_width);
    if (ref.type == MFX_FBT_UINT) return (int64_t)read_uint(ref.data, ref.parent_width);
    if (ref.type == MFX_FBT_INDIRECT_UINT) return (int64_t)read_uint(deref(ref.data, ref.parent_width), ref.byte_width);
    if (ref.type == MFX_FBT_BOOL) return read_uint(ref.data, ref.parent_width) != 0;
    return 0;
}

uint64_t mfx_uint(mfx_ref ref) {
    if (ref.type == MFX_FBT_INDIRECT_UINT) return read_uint(deref(ref.data, ref.parent_width), ref.byte_width);
    if (ref.type == MFX_FBT_UINT) return read_uint(ref.data, ref.parent_width);
    if (ref.type == MFX_FBT_INT) return (uint64_t)read_int(ref.data, ref.parent_width);
    if (ref.type == MFX_FBT_INDIRECT_INT) return (uint64_t)read_int(deref(ref.data, ref.parent_width), ref.byte_width);
    if (ref.type == MFX_FBT_BOOL) return read_uint(ref.data, ref.parent_width) != 0;
    return 0;
}

float mfx_float(mfx_ref ref) { return (float)mfx_double(ref); }

double mfx_double(mfx_ref ref) {
    if (ref.type == MFX_FBT_INDIRECT_FLOAT) return read_float(deref(ref.data, ref.parent_width), ref.byte_width);
    if (ref.type == MFX_FBT_FLOAT) return read_float(ref.data, ref.parent_width);
    if (ref.type == MFX_FBT_INT || ref.type == MFX_FBT_INDIRECT_INT) return (double)mfx_int(ref);
    if (ref.type == MFX_FBT_UINT || ref.type == MFX_FBT_INDIRECT_UINT) return (double)mfx_uint(ref);
    if (ref.type == MFX_FBT_BOOL) return read_uint(ref.data, ref.parent_width) != 0;
    return 0.0;
}

bool mfx_bool(mfx_ref ref) {
    if (ref.type == MFX_FBT_BOOL) return read_uint(ref.data, ref.parent_width) != 0;
    if (mfx_is_int(ref) || mfx_is_uint(ref)) return mfx_uint(ref) != 0;
    return false;
}

const char *mfx_string(mfx_ref ref, uint32_t *out_len) {
    const uint8_t *p;

    if (out_len) *out_len = 0;

    if (ref.type == MFX_FBT_STRING) {
        p = deref(ref.data, ref.parent_width);
        if (out_len) *out_len = (uint32_t)read_uint(p - ref.byte_width, ref.byte_width);
        return (const char *)p;
    }

    return "";
}

const char *mfx_key(mfx_ref ref, uint32_t *out_len) {
    const uint8_t *p;

    if (out_len) *out_len = 0;
    if (ref.type != MFX_FBT_KEY) return "";

    p = deref(ref.data, ref.parent_width);
    if (out_len) *out_len = (uint32_t)strlen((const char *)p);
    return (const char *)p;
}

const uint8_t *mfx_blob(mfx_ref ref, uint32_t *out_size) {
    const uint8_t *p;

    if (out_size) *out_size = 0;
    if (ref.type != MFX_FBT_BLOB) return NULL;

    p = deref(ref.data, ref.parent_width);
    if (out_size) *out_size = (uint32_t)read_uint(p - ref.byte_width, ref.byte_width);
    return p;
}

uint32_t mfx_vec_size(mfx_ref ref) {
    vector_info info;
    return get_vector_info(ref, &info) ? info.size : 0;
}

mfx_type mfx_vec_elem_type(mfx_ref ref) {
    vector_info info;
    return get_vector_info(ref, &info) ? info.elem_type : MFX_FBT_NULL;
}

mfx_ref mfx_vec_at(mfx_ref ref, uint32_t index) {
    vector_info info;
    mfx_ref result = {0};

    if (!get_vector_info(ref, &info) || index >= info.size) return result;

    result.data = info.data + index * info.byte_width;
    result.parent_width = info.byte_width;
    if (info.has_type_table) {
        const uint8_t *types = info.data + info.size * info.byte_width;
        result.type = types[index] >> 2;
        result.byte_width = bw_from_log2(types[index] & 3);
    } else {
        result.type = (uint8_t)info.elem_type;
        result.byte_width = info.byte_width;
    }
    return result;
}

uint32_t mfx_map_size(mfx_ref ref) {
    return ref.type == MFX_FBT_MAP ? mfx_vec_size(ref) : 0;
}

static const uint8_t *map_keys_vec(mfx_ref ref, uint32_t *out_size, uint8_t *out_keys_bw) {
    const uint8_t *p;
    uint8_t bw;

    if (out_size) *out_size = 0;
    if (out_keys_bw) *out_keys_bw = 0;
    if (ref.type != MFX_FBT_MAP) return NULL;

    p = deref(ref.data, ref.parent_width);
    bw = ref.byte_width;

    if (out_size) *out_size = (uint32_t)read_uint(p - bw, bw);
    if (out_keys_bw) *out_keys_bw = (uint8_t)read_uint(p - bw * 2, bw);
    return (p - bw * 3) - read_uint(p - bw * 3, bw);
}

const char *mfx_map_key_at(mfx_ref ref, uint32_t index) {
    uint32_t size = 0;
    uint8_t keys_bw = 0;
    const uint8_t *keys = map_keys_vec(ref, &size, &keys_bw);
    const uint8_t *key_ptr;

    if (!keys || index >= size) return NULL;
    key_ptr = keys + index * keys_bw;
    return (const char *)(key_ptr - read_uint(key_ptr, keys_bw));
}

mfx_ref mfx_map_at_index(mfx_ref ref, uint32_t index) {
    return ref.type == MFX_FBT_MAP ? mfx_vec_at(ref, index) : (mfx_ref){0};
}

mfx_ref mfx_map_at(mfx_ref ref, const char *key) {
    uint32_t size = 0;
    uint8_t keys_bw = 0;
    const uint8_t *keys = map_keys_vec(ref, &size, &keys_bw);
    int lo = 0;
    int hi = (int)size - 1;

    if (!keys || !key) return (mfx_ref){0};

    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        const uint8_t *kp = keys + mid * keys_bw;
        const char *cur = (const char *)(kp - read_uint(kp, keys_bw));
        int cmp = strcmp(cur, key);
        if (cmp == 0) return mfx_vec_at(ref, (uint32_t)mid);
        if (cmp < 0) lo = mid + 1;
        else hi = mid - 1;
    }

    return (mfx_ref){0};
}

/* ── Mutation ───────────────────────────────────────────────────── */

static bool mutate_target(mfx_ref ref, uint8_t **out_p, uint8_t *out_bw) {
    if (ref.type == MFX_FBT_INT || ref.type == MFX_FBT_UINT ||
        ref.type == MFX_FBT_FLOAT || ref.type == MFX_FBT_BOOL) {
        *out_p = (uint8_t *)ref.data;
        *out_bw = ref.parent_width;
        return true;
    }

    if (ref.type == MFX_FBT_INDIRECT_INT || ref.type == MFX_FBT_INDIRECT_UINT ||
        ref.type == MFX_FBT_INDIRECT_FLOAT) {
        *out_p = (uint8_t *)deref(ref.data, ref.parent_width);
        *out_bw = ref.byte_width;
        return true;
    }

    return false;
}

bool mfx_mutate_int(mfx_ref ref, int64_t val) {
    uint8_t *p;
    uint8_t bw;

    if (!mfx_is_int(ref) || !mutate_target(ref, &p, &bw) || !int_fits_width(val, bw)) return false;

    switch (bw) {
        case 1: { int8_t v = (int8_t)val; memcpy(p, &v, 1); return true; }
        case 2: { int16_t v = (int16_t)val; memcpy(p, &v, 2); return true; }
        case 4: { int32_t v = (int32_t)val; memcpy(p, &v, 4); return true; }
        case 8: memcpy(p, &val, 8); return true;
        default: return false;
    }
}

bool mfx_mutate_uint(mfx_ref ref, uint64_t val) {
    uint8_t *p;
    uint8_t bw;

    if (!mfx_is_uint(ref) || !mutate_target(ref, &p, &bw) || !uint_fits_width(val, bw)) return false;

    switch (bw) {
        case 1: { uint8_t v = (uint8_t)val; memcpy(p, &v, 1); return true; }
        case 2: { uint16_t v = (uint16_t)val; memcpy(p, &v, 2); return true; }
        case 4: { uint32_t v = (uint32_t)val; memcpy(p, &v, 4); return true; }
        case 8: memcpy(p, &val, 8); return true;
        default: return false;
    }
}

bool mfx_mutate_float(mfx_ref ref, double val) {
    uint8_t *p;
    uint8_t bw;

    if (!mfx_is_float(ref) || !mutate_target(ref, &p, &bw) || !float_fits_width(val, bw)) return false;

    if (bw == 4) {
        float v = (float)val;
        memcpy(p, &v, 4);
        return true;
    }
    if (bw == 8) {
        memcpy(p, &val, 8);
        return true;
    }
    return false;
}

bool mfx_mutate_bool(mfx_ref ref, bool val) {
    uint8_t *p;
    uint8_t bw;
    uint8_t v = val ? 1 : 0;

    if (ref.type != MFX_FBT_BOOL || !mutate_target(ref, &p, &bw)) return false;
    if (!uint_fits_width(v, bw)) return false;

    switch (bw) {
        case 1: memcpy(p, &v, 1); return true;
        case 2: { uint16_t x = v; memcpy(p, &x, 2); return true; }
        case 4: { uint32_t x = v; memcpy(p, &x, 4); return true; }
        case 8: { uint64_t x = v; memcpy(p, &x, 8); return true; }
        default: return false;
    }
}

bool mfx_mutate_string(mfx_ref ref, const char *val, uint32_t max_len) {
    const uint8_t *p;
    uint32_t original_len;
    uint32_t val_len;
    uint32_t buf_len;

    if (ref.type != MFX_FBT_STRING || !val) return false;
    p = deref(ref.data, ref.parent_width);
    original_len = (uint32_t)read_uint(p - ref.byte_width, ref.byte_width);
    buf_len = original_len < max_len ? original_len : max_len;
    val_len = (uint32_t)strlen(val);
    if (val_len > buf_len) return false;

    memset((uint8_t *)p, 0, original_len);
    memcpy((uint8_t *)p, val, val_len);
    return true;
}

bool mfx_mutate_blob(mfx_ref ref, const uint8_t *data, uint32_t size) {
    uint32_t existing = 0;
    const uint8_t *p;

    if (ref.type != MFX_FBT_BLOB || !data) return false;
    p = mfx_blob(ref, &existing);
    if (!p || size > existing) return false;
    memcpy((uint8_t *)p, data, size);
    return true;
}

/* ── Verifier ───────────────────────────────────────────────────── */

typedef struct {
    const uint8_t *buf;
    const uint8_t *end;
} verify_ctx;

static bool verify_range(verify_ctx *ctx, const uint8_t *p, uint32_t len) {
    return p >= ctx->buf && p <= ctx->end && (uint64_t)(ctx->end - p) >= len;
}

static bool verify_deref(verify_ctx *ctx, const uint8_t *p, uint8_t bw, const uint8_t **out) {
    uint64_t delta;

    if (!bw_is_valid(bw) || !verify_range(ctx, p, bw)) return false;
    delta = read_uint(p, bw);
    if (delta > (uint64_t)(p - ctx->buf)) return false;
    *out = p - delta;
    return true;
}

static bool verify_nt_string(verify_ctx *ctx, const uint8_t *p) {
    const uint8_t *cur = p;
    while (cur < ctx->end) {
        if (*cur == 0) return true;
        cur++;
    }
    return false;
}

static bool verify_ref(verify_ctx *ctx, mfx_ref ref);

static bool verify_map_keys(verify_ctx *ctx, mfx_ref ref, uint32_t count) {
    const uint8_t *p;
    const uint8_t *keys_offset_ptr;
    const uint8_t *keys_vec;
    uint8_t bw = ref.byte_width;
    uint8_t keys_bw;
    uint32_t keys_count;
    const char *prev = NULL;
    uint32_t i;

    p = deref(ref.data, ref.parent_width);
    keys_offset_ptr = p - bw * 3;
    if (!verify_range(ctx, keys_offset_ptr, bw * 3)) return false;
    if (!verify_deref(ctx, keys_offset_ptr, bw, &keys_vec)) return false;

    keys_bw = (uint8_t)read_uint(p - bw * 2, bw);
    if (!bw_is_valid(keys_bw)) return false;
    if (!verify_range(ctx, keys_vec - keys_bw, keys_bw)) return false;
    keys_count = (uint32_t)read_uint(keys_vec - keys_bw, keys_bw);
    if (keys_count != count) return false;
    if (!verify_range(ctx, keys_vec, keys_count * keys_bw)) return false;

    for (i = 0; i < keys_count; i++) {
        const uint8_t *slot = keys_vec + i * keys_bw;
        const uint8_t *key_ptr;
        const char *cur;
        if (!verify_deref(ctx, slot, keys_bw, &key_ptr)) return false;
        if (!verify_nt_string(ctx, key_ptr)) return false;
        cur = (const char *)key_ptr;
        if (prev && strcmp(prev, cur) > 0) return false;
        prev = cur;
    }

    return true;
}

static bool verify_vector_like(verify_ctx *ctx, mfx_ref ref) {
    vector_info info;
    uint32_t i;

    if (!get_vector_info(ref, &info)) return false;

    if (type_is_fixed_typed_vector(ref.type)) {
        if (!verify_range(ctx, info.data, info.size * info.byte_width)) return false;
    } else {
        if (!verify_range(ctx, info.data - info.byte_width, info.byte_width)) return false;
        if (!verify_range(ctx, info.data, info.size * info.byte_width)) return false;
    }

    if (info.has_type_table) {
        if (!verify_range(ctx, info.data + info.size * info.byte_width, info.size)) return false;
    }

    if (ref.type == MFX_FBT_MAP && !verify_map_keys(ctx, ref, info.size)) return false;

    for (i = 0; i < info.size; i++) {
        mfx_ref elem = mfx_vec_at(ref, i);
        if (!type_is_known(elem.type)) return false;
        if (info.has_type_table && !bw_is_valid(elem.byte_width)) return false;
        if (!verify_ref(ctx, elem)) return false;
    }

    return true;
}

static bool verify_ref(verify_ctx *ctx, mfx_ref ref) {
    const uint8_t *p;
    uint32_t len;

    if (!type_is_known(ref.type)) return false;
    if (!bw_is_valid(ref.parent_width) || !bw_is_valid(ref.byte_width)) return false;
    if (!verify_range(ctx, ref.data, ref.parent_width)) return false;

    if (type_is_inline_scalar(ref.type)) return true;

    if (type_is_indirect_scalar(ref.type)) {
        return verify_deref(ctx, ref.data, ref.parent_width, &p) && verify_range(ctx, p, ref.byte_width);
    }

    switch (ref.type) {
        case MFX_FBT_KEY:
            return verify_deref(ctx, ref.data, ref.parent_width, &p) && verify_nt_string(ctx, p);
        case MFX_FBT_STRING:
            if (!verify_deref(ctx, ref.data, ref.parent_width, &p)) return false;
            if (!verify_range(ctx, p - ref.byte_width, ref.byte_width)) return false;
            len = (uint32_t)read_uint(p - ref.byte_width, ref.byte_width);
            return verify_range(ctx, p, len + 1) && p[len] == 0;
        case MFX_FBT_BLOB:
            if (!verify_deref(ctx, ref.data, ref.parent_width, &p)) return false;
            if (!verify_range(ctx, p - ref.byte_width, ref.byte_width)) return false;
            len = (uint32_t)read_uint(p - ref.byte_width, ref.byte_width);
            return verify_range(ctx, p, len);
        default:
            return verify_vector_like(ctx, ref);
    }
}

bool mfx_verify(const uint8_t *buf, uint32_t size) {
    verify_ctx ctx;
    mfx_ref root;

    if (!buf || size < 3) return false;
    root = mfx_root(buf, size);
    if (!root.data) return false;

    ctx.buf = buf;
    ctx.end = buf + size;

    if (!verify_range(&ctx, root.data, root.parent_width)) return false;
    return verify_ref(&ctx, root);
}

/* ── Writer ─────────────────────────────────────────────────────── */

typedef struct {
    uint8_t type;
    uint8_t min_bit_width;
    union {
        int64_t i;
        uint64_t u;
        double f;
        uint32_t offset;
    } val;
} build_value;

typedef enum {
    FRAME_MAP = 1,
    FRAME_VECTOR,
    FRAME_TYPED_VECTOR,
    FRAME_FIXED_TYPED_VECTOR
} frame_kind;

typedef struct {
    uint32_t start;
    uint8_t kind;
    uint8_t elem_type;
    uint8_t arity;
} container_frame;

struct mfx_builder {
    uint8_t *buf;
    uint32_t buf_size;
    uint32_t buf_cap;

    build_value *values;
    uint32_t values_size;
    uint32_t values_cap;

    container_frame *containers;
    uint32_t containers_size;
    uint32_t containers_cap;

    bool failed;
    const char *error;
};

static void builder_fail_msg(mfx_builder *b, const char *msg) {
    if (!b) return;
    b->failed = true;
    if (!b->error) b->error = msg;
}

static void builder_fail(mfx_builder *b) {
    builder_fail_msg(b, "builder failed");
}

static void buf_ensure(mfx_builder *b, uint32_t needed) {
    uint32_t min_cap;
    uint32_t new_cap;
    uint8_t *new_buf;

    if (b->failed || b->buf_size + needed <= b->buf_cap) return;
    min_cap = b->buf_size + needed;
    new_cap = b->buf_cap ? b->buf_cap * 2 : 256;
    while (new_cap < min_cap) new_cap *= 2;
        new_buf = (uint8_t *)realloc(b->buf, new_cap);
        if (!new_buf) {
            builder_fail_msg(b, "out of memory");
            return;
        }
    b->buf = new_buf;
    b->buf_cap = new_cap;
}

static uint32_t buf_write(mfx_builder *b, const void *data, uint32_t size) {
    uint32_t offset = b->buf_size;
    buf_ensure(b, size);
    if (b->failed) return 0;
    memcpy(b->buf + offset, data, size);
    b->buf_size += size;
    return offset;
}

static void buf_write_byte(mfx_builder *b, uint8_t v) {
    (void)buf_write(b, &v, 1);
}

static void buf_pad_to_align(mfx_builder *b, uint8_t alignment) {
    while (!b->failed && b->buf_size % alignment != 0) buf_write_byte(b, 0);
}

static void buf_write_uint(mfx_builder *b, uint64_t val, uint8_t bw) {
    buf_pad_to_align(b, bw);
    if (b->failed) return;
    switch (bw) {
        case 1: { uint8_t v = (uint8_t)val; buf_write(b, &v, 1); break; }
        case 2: { uint16_t v = (uint16_t)val; buf_write(b, &v, 2); break; }
        case 4: { uint32_t v = (uint32_t)val; buf_write(b, &v, 4); break; }
        case 8: buf_write(b, &val, 8); break;
        default: builder_fail(b); break;
    }
}

static void buf_write_int(mfx_builder *b, int64_t val, uint8_t bw) {
    buf_pad_to_align(b, bw);
    if (b->failed) return;
    switch (bw) {
        case 1: { int8_t v = (int8_t)val; buf_write(b, &v, 1); break; }
        case 2: { int16_t v = (int16_t)val; buf_write(b, &v, 2); break; }
        case 4: { int32_t v = (int32_t)val; buf_write(b, &v, 4); break; }
        case 8: buf_write(b, &val, 8); break;
        default: builder_fail(b); break;
    }
}

static void push_value(mfx_builder *b, build_value v) {
    build_value *new_vals;
    if (b->failed) return;
    if (b->values_size >= b->values_cap) {
        uint32_t new_cap = b->values_cap ? b->values_cap * 2 : 32;
        new_vals = (build_value *)realloc(b->values, new_cap * sizeof(build_value));
        if (!new_vals) {
            builder_fail_msg(b, "out of memory");
            return;
        }
        b->values = new_vals;
        b->values_cap = new_cap;
    }
    b->values[b->values_size++] = v;
}

static void push_container(mfx_builder *b, uint8_t kind, uint8_t elem_type, uint8_t arity) {
    container_frame *new_frames;
    if (b->failed) return;
    if (b->containers_size >= b->containers_cap) {
        uint32_t new_cap = b->containers_cap ? b->containers_cap * 2 : 8;
        new_frames = (container_frame *)realloc(b->containers, new_cap * sizeof(container_frame));
        if (!new_frames) {
            builder_fail_msg(b, "out of memory");
            return;
        }
        b->containers = new_frames;
        b->containers_cap = new_cap;
    }
    b->containers[b->containers_size++] = (container_frame){
        .start = b->values_size,
        .kind = kind,
        .elem_type = elem_type,
        .arity = arity
    };
}

static void push_key_if_needed(mfx_builder *b, const char *key);

static uint32_t write_key(mfx_builder *b, const char *key) {
    uint32_t offset = b->buf_size;
    buf_write(b, key, (uint32_t)strlen(key));
    buf_write_byte(b, 0);
    return offset;
}

static uint32_t write_string_n(mfx_builder *b, const char *s, uint32_t fixed_len) {
    uint32_t len = fixed_len ? fixed_len : (uint32_t)strlen(s);
    uint8_t bw = bw_from_log2(width_for_uint(len));
    uint32_t offset;

    buf_pad_to_align(b, bw);
    buf_write_uint(b, len, bw);
    offset = b->buf_size;
    if (fixed_len) {
        uint32_t slen = (uint32_t)strlen(s);
        if (slen > fixed_len) slen = fixed_len;
        buf_ensure(b, fixed_len + 1);
        if (b->failed) return 0;
        memset(b->buf + b->buf_size, 0, fixed_len + 1);
        memcpy(b->buf + b->buf_size, s, slen);
        b->buf_size += fixed_len;
    } else {
        buf_write(b, s, len);
    }
    buf_write_byte(b, 0);
    return offset;
}

static uint32_t write_blob(mfx_builder *b, const uint8_t *data, uint32_t size) {
    uint8_t bw = bw_from_log2(width_for_uint(size));
    uint32_t offset;

    buf_pad_to_align(b, bw);
    buf_write_uint(b, size, bw);
    offset = b->buf_size;
    if (data) {
        buf_write(b, data, size);
    } else {
        buf_ensure(b, size);
        if (b->failed) return 0;
        memset(b->buf + b->buf_size, 0, size);
        b->buf_size += size;
    }
    return offset;
}

static void push_key_if_needed(mfx_builder *b, const char *key) {
    if (!key) return;
    push_value(b, (build_value){ .type = MFX_FBT_KEY, .min_bit_width = 0, .val.offset = write_key(b, key) });
}

static uint8_t compute_element_bw(build_value *vals, uint32_t count, uint32_t base_pos, uint32_t prefix_elems, uint8_t initial_bw, bool fixed) {
    uint8_t bw_log2 = initial_bw;
    int pass;

    for (pass = 0; pass < 4; pass++) {
        uint8_t new_bw = fixed ? 0 : width_for_uint(count);
        uint8_t bw = bw_from_log2(bw_log2);
        uint32_t data_start = base_pos + prefix_elems * bw;
        uint32_t i;

        for (i = 0; i < count; i++) {
            uint8_t w = vals[i].min_bit_width;
            if (type_is_offset_encoded(vals[i].type)) {
                uint8_t off_w = width_for_offset(vals[i].val.offset, data_start + i * bw);
                if (off_w > w) w = off_w;
            }
            if (w > new_bw) new_bw = w;
        }

        if (new_bw == bw_log2) break;
        bw_log2 = new_bw;
    }

    return bw_log2;
}

static void write_value_at_width(mfx_builder *b, build_value v, uint8_t byte_width) {
    switch (v.type) {
        case MFX_FBT_INT:
            buf_write_int(b, v.val.i, byte_width);
            break;
        case MFX_FBT_UINT:
        case MFX_FBT_BOOL:
            buf_write_uint(b, v.val.u, byte_width);
            break;
        case MFX_FBT_FLOAT:
            if (byte_width == 4) {
                float f = (float)v.val.f;
                buf_write(b, &f, 4);
            } else if (byte_width == 8) {
                buf_write(b, &v.val.f, 8);
            } else {
                builder_fail(b);
            }
            break;
        case MFX_FBT_NULL:
            buf_write_uint(b, 0, byte_width);
            break;
        default: {
            uint32_t delta = b->buf_size - v.val.offset;
            buf_write_uint(b, delta, byte_width);
            break;
        }
    }
}

static void sort_map_pairs(mfx_builder *b, build_value *vals, uint32_t count) {
    uint32_t npairs = count / 2;
    uint32_t i;

    for (i = 1; i < npairs; i++) {
        build_value tmp_key = vals[i * 2];
        build_value tmp_val = vals[i * 2 + 1];
        const char *key_str = (const char *)(b->buf + tmp_key.val.offset);
        int j = (int)i - 1;

        while (j >= 0) {
            const char *other_key = (const char *)(b->buf + vals[j * 2].val.offset);
            if (strcmp(other_key, key_str) <= 0) break;
            vals[(j + 1) * 2] = vals[j * 2];
            vals[(j + 1) * 2 + 1] = vals[j * 2 + 1];
            j--;
        }

        vals[(j + 1) * 2] = tmp_key;
        vals[(j + 1) * 2 + 1] = tmp_val;
    }
}

static build_value write_untyped_vector(mfx_builder *b, build_value *vals, uint32_t count) {
    uint8_t bw_log2 = compute_element_bw(vals, count, b->buf_size, 1, width_for_uint(count), false);
    uint8_t bw = bw_from_log2(bw_log2);
    uint32_t offset;
    uint32_t i;

    buf_pad_to_align(b, bw);
    buf_write_uint(b, count, bw);
    offset = b->buf_size;

    for (i = 0; i < count; i++) write_value_at_width(b, vals[i], bw);

    for (i = 0; i < count; i++) {
        uint8_t stored_width = type_is_inline_scalar(vals[i].type) ? bw_log2 : vals[i].min_bit_width;
        buf_write_byte(b, packed_type(vals[i].type, stored_width));
    }

    return (build_value){ .type = MFX_FBT_VECTOR, .min_bit_width = bw_log2, .val.offset = offset };
}

static build_value write_typed_vector(mfx_builder *b, build_value *vals, uint32_t count, uint8_t elem_type, bool fixed) {
    uint8_t initial_bw = fixed ? 0 : width_for_uint(count);
    uint8_t bw_log2 = compute_element_bw(vals, count, b->buf_size, fixed ? 0 : 1, initial_bw, fixed);
    uint8_t bw = bw_from_log2(bw_log2);
    uint32_t offset;
    uint32_t i;
    uint8_t type = typed_vector_wire_type(elem_type, fixed ? count : 0);

    if (!type) {
        builder_fail_msg(b, "unsupported typed vector element type");
        return (build_value){0};
    }

    buf_pad_to_align(b, bw);
    if (!fixed) buf_write_uint(b, count, bw);
    offset = b->buf_size;
    for (i = 0; i < count; i++) write_value_at_width(b, vals[i], bw);

    return (build_value){ .type = type, .min_bit_width = bw_log2, .val.offset = offset };
}

static build_value write_map(mfx_builder *b, build_value *vals, uint32_t count) {
    uint32_t npairs = count / 2;
    uint8_t keys_bw_log2 = 0;
    uint8_t val_bw_log2;
    uint8_t keys_bw;
    uint8_t val_bw;
    uint32_t keys_vec_offset;
    uint32_t map_offset;
    build_value *value_entries;
    uint32_t i;

    sort_map_pairs(b, vals, count);

    for (i = 0; i < npairs; i++) {
        uint8_t off_w = width_for_offset(vals[i * 2].val.offset, b->buf_size + i * bw_from_log2(keys_bw_log2));
        if (off_w > keys_bw_log2) keys_bw_log2 = off_w;
    }
    keys_bw = bw_from_log2(keys_bw_log2);

    buf_pad_to_align(b, keys_bw);
    buf_write_uint(b, npairs, keys_bw);
    keys_vec_offset = b->buf_size;
    for (i = 0; i < npairs; i++) {
        uint32_t delta = b->buf_size - vals[i * 2].val.offset;
        buf_write_uint(b, delta, keys_bw);
    }

    value_entries = (build_value *)malloc(npairs * sizeof(build_value));
    if (!value_entries) {
        builder_fail_msg(b, "out of memory");
        return (build_value){0};
    }
    for (i = 0; i < npairs; i++) value_entries[i] = vals[i * 2 + 1];

    val_bw_log2 = compute_element_bw(value_entries, npairs, b->buf_size, 3, width_for_uint(npairs), false);
    if (width_for_offset(keys_vec_offset, b->buf_size) > val_bw_log2) val_bw_log2 = width_for_offset(keys_vec_offset, b->buf_size);
    val_bw = bw_from_log2(val_bw_log2);

    buf_pad_to_align(b, val_bw);
    buf_write_uint(b, b->buf_size - keys_vec_offset, val_bw);
    buf_write_uint(b, keys_bw, val_bw);
    buf_write_uint(b, npairs, val_bw);
    map_offset = b->buf_size;

    for (i = 0; i < npairs; i++) write_value_at_width(b, value_entries[i], val_bw);
    for (i = 0; i < npairs; i++) {
        uint8_t stored_width = type_is_inline_scalar(value_entries[i].type) ? val_bw_log2 : value_entries[i].min_bit_width;
        buf_write_byte(b, packed_type(value_entries[i].type, stored_width));
    }

    free(value_entries);
    return (build_value){ .type = MFX_FBT_MAP, .min_bit_width = val_bw_log2, .val.offset = map_offset };
}

static void check_typed_vector_values(mfx_builder *b, build_value *vals, uint32_t count, uint8_t elem_type, bool fixed, uint8_t arity) {
    uint32_t i;

    if (!type_is_typed_vector_elem(elem_type) || (fixed && !type_is_fixed_vector_elem(elem_type))) {
        builder_fail_msg(b, "invalid typed vector element type");
        return;
    }
    if (fixed && count != arity) {
        builder_fail_msg(b, "fixed vector arity mismatch");
        return;
    }

    for (i = 0; i < count; i++) {
        uint8_t t = vals[i].type;
        if (elem_type == MFX_FBT_STRING && t == MFX_FBT_STRING) continue;
        if (t != elem_type) {
            builder_fail_msg(b, "typed vector contains mismatched element type");
            return;
        }
    }
}

static void end_container(mfx_builder *b, uint8_t expected_kind) {
    container_frame frame;
    uint32_t count;
    build_value *vals;
    build_value result = {0};

    if (b->failed || b->containers_size == 0) {
        builder_fail_msg(b, "container stack underflow");
        return;
    }

    frame = b->containers[--b->containers_size];
    if (frame.kind != expected_kind) {
        builder_fail_msg(b, "container end kind mismatch");
        return;
    }

    count = b->values_size - frame.start;
    vals = b->values + frame.start;

    switch (frame.kind) {
        case FRAME_MAP:
            if (count % 2 != 0) {
                builder_fail_msg(b, "map requires key/value pairs");
                return;
            }
            for (uint32_t i = 0; i < count; i += 2) {
                if (vals[i].type != MFX_FBT_KEY) {
                    builder_fail_msg(b, "map keys must be key values");
                    return;
                }
            }
            result = write_map(b, vals, count);
            break;
        case FRAME_VECTOR:
            result = write_untyped_vector(b, vals, count);
            break;
        case FRAME_TYPED_VECTOR:
            check_typed_vector_values(b, vals, count, frame.elem_type, false, 0);
            if (b->failed) return;
            result = write_typed_vector(b, vals, count, frame.elem_type, false);
            break;
        case FRAME_FIXED_TYPED_VECTOR:
            check_typed_vector_values(b, vals, count, frame.elem_type, true, frame.arity);
            if (b->failed) return;
            result = write_typed_vector(b, vals, count, frame.elem_type, true);
            break;
        default:
            builder_fail_msg(b, "unknown container frame kind");
            return;
    }

    if (b->failed) return;
    b->values_size = frame.start;
    push_value(b, result);
}

mfx_builder *mfx_builder_create(void) {
    return (mfx_builder *)calloc(1, sizeof(mfx_builder));
}

void mfx_builder_destroy(mfx_builder *b) {
    if (!b) return;
    free(b->buf);
    free(b->values);
    free(b->containers);
    free(b);
}

void mfx_builder_clear(mfx_builder *b) {
    if (!b) return;
    b->buf_size = 0;
    b->values_size = 0;
    b->containers_size = 0;
    b->failed = false;
    b->error = NULL;
}

bool mfx_builder_ok(const mfx_builder *b) {
    return b && !b->failed;
}

const char *mfx_builder_error(const mfx_builder *b) {
    return (b && b->error) ? b->error : "";
}

void mfx_build_null(mfx_builder *b, const char *key) {
    push_key_if_needed(b, key);
    push_value(b, (build_value){ .type = MFX_FBT_NULL, .min_bit_width = 0, .val.u = 0 });
}

void mfx_build_int(mfx_builder *b, const char *key, int64_t val) {
    push_key_if_needed(b, key);
    push_value(b, (build_value){ .type = MFX_FBT_INT, .min_bit_width = width_for_int(val), .val.i = val });
}

void mfx_build_uint(mfx_builder *b, const char *key, uint64_t val) {
    push_key_if_needed(b, key);
    push_value(b, (build_value){ .type = MFX_FBT_UINT, .min_bit_width = width_for_uint(val), .val.u = val });
}

void mfx_build_float(mfx_builder *b, const char *key, float val) {
    push_key_if_needed(b, key);
    push_value(b, (build_value){ .type = MFX_FBT_FLOAT, .min_bit_width = 2, .val.f = (double)val });
}

void mfx_build_double(mfx_builder *b, const char *key, double val) {
    push_key_if_needed(b, key);
    push_value(b, (build_value){ .type = MFX_FBT_FLOAT, .min_bit_width = width_for_float(val), .val.f = val });
}

static void build_indirect(mfx_builder *b, const char *key, uint8_t type, uint8_t bw_log2, int64_t i, uint64_t u, double f) {
    uint8_t bw = bw_from_log2(bw_log2);
    uint32_t offset;

    push_key_if_needed(b, key);
    buf_pad_to_align(b, bw);
    offset = b->buf_size;

    switch (type) {
        case MFX_FBT_INDIRECT_INT: buf_write_int(b, i, bw); break;
        case MFX_FBT_INDIRECT_UINT: buf_write_uint(b, u, bw); break;
        case MFX_FBT_INDIRECT_FLOAT:
            if (bw == 4) {
                float fv = (float)f;
                buf_write(b, &fv, 4);
            } else if (bw == 8) {
                buf_write(b, &f, 8);
            } else {
                builder_fail(b);
            }
            break;
        default:
            builder_fail(b);
            return;
    }

    push_value(b, (build_value){ .type = type, .min_bit_width = bw_log2, .val.offset = offset });
}

void mfx_build_indirect_int(mfx_builder *b, const char *key, int64_t val) {
    build_indirect(b, key, MFX_FBT_INDIRECT_INT, width_for_int(val), val, 0, 0.0);
}

void mfx_build_indirect_uint(mfx_builder *b, const char *key, uint64_t val) {
    build_indirect(b, key, MFX_FBT_INDIRECT_UINT, width_for_uint(val), 0, val, 0.0);
}

void mfx_build_indirect_float(mfx_builder *b, const char *key, float val) {
    build_indirect(b, key, MFX_FBT_INDIRECT_FLOAT, 2, 0, 0, (double)val);
}

void mfx_build_indirect_double(mfx_builder *b, const char *key, double val) {
    build_indirect(b, key, MFX_FBT_INDIRECT_FLOAT, width_for_float(val), 0, 0, val);
}

void mfx_build_bool(mfx_builder *b, const char *key, bool val) {
    push_key_if_needed(b, key);
    push_value(b, (build_value){ .type = MFX_FBT_BOOL, .min_bit_width = 0, .val.u = val ? 1u : 0u });
}

void mfx_build_key_value(mfx_builder *b, const char *key, const char *val) {
    push_key_if_needed(b, key);
    push_value(b, (build_value){ .type = MFX_FBT_KEY, .min_bit_width = 0, .val.offset = write_key(b, val) });
}

void mfx_build_string(mfx_builder *b, const char *key, const char *val) {
    uint32_t len;
    uint32_t offset;
    push_key_if_needed(b, key);
    len = (uint32_t)strlen(val);
    offset = write_string_n(b, val, 0);
    push_value(b, (build_value){ .type = MFX_FBT_STRING, .min_bit_width = width_for_uint(len), .val.offset = offset });
}

void mfx_build_string_n(mfx_builder *b, const char *key, const char *val, uint32_t fixed_len) {
    uint32_t offset;
    push_key_if_needed(b, key);
    offset = write_string_n(b, val, fixed_len);
    push_value(b, (build_value){ .type = MFX_FBT_STRING, .min_bit_width = width_for_uint(fixed_len), .val.offset = offset });
}

void mfx_build_blob(mfx_builder *b, const char *key, const uint8_t *data, uint32_t size) {
    uint32_t offset;
    push_key_if_needed(b, key);
    offset = write_blob(b, data, size);
    push_value(b, (build_value){ .type = MFX_FBT_BLOB, .min_bit_width = width_for_uint(size), .val.offset = offset });
}

void mfx_build_blob_n(mfx_builder *b, const char *key, uint32_t size) {
    uint32_t offset;
    push_key_if_needed(b, key);
    offset = write_blob(b, NULL, size);
    push_value(b, (build_value){ .type = MFX_FBT_BLOB, .min_bit_width = width_for_uint(size), .val.offset = offset });
}

void mfx_build_map_start(mfx_builder *b, const char *key) {
    push_key_if_needed(b, key);
    push_container(b, FRAME_MAP, 0, 0);
}

void mfx_build_map_end(mfx_builder *b) {
    end_container(b, FRAME_MAP);
}

void mfx_build_vector_start(mfx_builder *b, const char *key) {
    push_key_if_needed(b, key);
    push_container(b, FRAME_VECTOR, 0, 0);
}

void mfx_build_vector_end(mfx_builder *b) {
    end_container(b, FRAME_VECTOR);
}

void mfx_build_typed_vector_start(mfx_builder *b, const char *key, mfx_type elem_type) {
    push_key_if_needed(b, key);
    push_container(b, FRAME_TYPED_VECTOR, (uint8_t)elem_type, 0);
}

void mfx_build_typed_vector_end(mfx_builder *b) {
    end_container(b, FRAME_TYPED_VECTOR);
}

void mfx_build_fixed_vector_start(mfx_builder *b, const char *key, mfx_type elem_type, uint32_t arity) {
    if (arity < 2 || arity > 4) {
        builder_fail_msg(b, "fixed vector arity must be 2, 3, or 4");
        return;
    }
    push_key_if_needed(b, key);
    push_container(b, FRAME_FIXED_TYPED_VECTOR, (uint8_t)elem_type, (uint8_t)arity);
}

void mfx_build_fixed_vector_end(mfx_builder *b) {
    end_container(b, FRAME_FIXED_TYPED_VECTOR);
}

uint8_t *mfx_build_finish(mfx_builder *b, uint32_t *out_size) {
    build_value root;
    uint8_t root_bw_log2;
    uint8_t root_bw;
    uint8_t *result;

    if (out_size) *out_size = 0;
    if (!b) return NULL;
    if (b->failed) return NULL;
    if (b->containers_size != 0) {
        builder_fail_msg(b, "unclosed container");
        return NULL;
    }
    if (b->values_size != 1) {
        builder_fail_msg(b, "builder must contain exactly one root value");
        return NULL;
    }

    root = b->values[0];
    root_bw_log2 = root.min_bit_width;
    if (type_is_offset_encoded(root.type)) {
        uint8_t off_w = width_for_offset(root.val.offset, b->buf_size);
        if (off_w > root_bw_log2) root_bw_log2 = off_w;
    }
    root_bw = bw_from_log2(root_bw_log2);

    buf_pad_to_align(b, root_bw);
    if (b->failed) return NULL;
    write_value_at_width(b, root, root_bw);
    buf_write_byte(b, packed_type(root.type, root_bw_log2));
    buf_write_byte(b, root_bw);
    if (b->failed) return NULL;

    result = b->buf;
    if (out_size) *out_size = b->buf_size;

    b->buf = NULL;
    b->buf_size = 0;
    b->buf_cap = 0;
    b->values_size = 0;
    b->containers_size = 0;
    b->failed = false;
    b->error = NULL;

    return result;
}

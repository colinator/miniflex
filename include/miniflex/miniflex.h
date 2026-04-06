#ifndef MINIFLEX_H
#define MINIFLEX_H

/*
 * miniflex — Minimal FlexBuffers reader/writer in pure C.
 *
 * Supports the full FlexBuffers wire format, plus miniflex-specific mutable
 * string/blob allocation helpers.
 *
 * Wire-compatible with Google's C++ flexbuffers::Builder output.
 *
 * Reader: zero-allocation — all references point into the original buffer.
 * Writer: uses malloc for a growable output buffer.
 * Mutation: in-place, same-size-or-smaller only.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ── FlexBuffer value types ─────────────────────────────────────── */

typedef enum {
    MFX_FBT_NULL            = 0,
    MFX_FBT_INT             = 1,
    MFX_FBT_UINT            = 2,
    MFX_FBT_FLOAT           = 3,
    MFX_FBT_KEY             = 4,
    MFX_FBT_STRING          = 5,
    MFX_FBT_INDIRECT_INT    = 6,
    MFX_FBT_INDIRECT_UINT   = 7,
    MFX_FBT_INDIRECT_FLOAT  = 8,
    MFX_FBT_MAP             = 9,
    MFX_FBT_VECTOR          = 10,
    MFX_FBT_VECTOR_INT      = 11,
    MFX_FBT_VECTOR_UINT     = 12,
    MFX_FBT_VECTOR_FLOAT    = 13,
    MFX_FBT_VECTOR_KEY      = 14,
    MFX_FBT_VECTOR_STRING_DEPRECATED = 15,
    MFX_FBT_VECTOR_INT2     = 16,
    MFX_FBT_VECTOR_UINT2    = 17,
    MFX_FBT_VECTOR_FLOAT2   = 18,
    MFX_FBT_VECTOR_INT3     = 19,
    MFX_FBT_VECTOR_UINT3    = 20,
    MFX_FBT_VECTOR_FLOAT3   = 21,
    MFX_FBT_VECTOR_INT4     = 22,
    MFX_FBT_VECTOR_UINT4    = 23,
    MFX_FBT_VECTOR_FLOAT4   = 24,
    MFX_FBT_BLOB            = 25,
    MFX_FBT_BOOL            = 26,
    MFX_FBT_VECTOR_BOOL     = 36,
} mfx_type;

/* ── Reader ─────────────────────────────────────────────────────── */

/* A reference into a FlexBuffer. Lightweight, no allocation. */
typedef struct {
    const uint8_t* data;        /* pointer into the buffer */
    uint32_t       parent_width;/* byte width of the parent container */
    uint8_t        byte_width;  /* byte width of this value */
    uint8_t        type;        /* mfx_type */
} mfx_ref;

/* Get the root reference from a FlexBuffer byte array. */
mfx_ref mfx_root(const uint8_t* buf, uint32_t size);

/* Type checking */
mfx_type mfx_type_of(mfx_ref ref);
bool     mfx_is_null(mfx_ref ref);
bool     mfx_is_int(mfx_ref ref);
bool     mfx_is_uint(mfx_ref ref);
bool     mfx_is_float(mfx_ref ref);
bool     mfx_is_numeric(mfx_ref ref);
bool     mfx_is_string(mfx_ref ref);
bool     mfx_is_blob(mfx_ref ref);
bool     mfx_is_map(mfx_ref ref);
bool     mfx_is_vector(mfx_ref ref);
bool     mfx_is_typed_vector(mfx_ref ref);
bool     mfx_is_fixed_typed_vector(mfx_ref ref);
bool     mfx_is_bool(mfx_ref ref);

/* Scalar reads */
int64_t  mfx_int(mfx_ref ref);
uint64_t mfx_uint(mfx_ref ref);
float    mfx_float(mfx_ref ref);
double   mfx_double(mfx_ref ref);
bool     mfx_bool(mfx_ref ref);

/* String/blob reads — returned pointer is into the original buffer */
const char*    mfx_string(mfx_ref ref, uint32_t* out_len);
const char*    mfx_key(mfx_ref ref, uint32_t* out_len);
const uint8_t* mfx_blob(mfx_ref ref, uint32_t* out_size);

/* Vector access */
uint32_t mfx_vec_size(mfx_ref ref);
mfx_ref  mfx_vec_at(mfx_ref ref, uint32_t index);
mfx_type mfx_vec_elem_type(mfx_ref ref);

/* Map access */
uint32_t    mfx_map_size(mfx_ref ref);
mfx_ref     mfx_map_at(mfx_ref ref, const char* key);
mfx_ref     mfx_map_at_index(mfx_ref ref, uint32_t index);
const char* mfx_map_key_at(mfx_ref ref, uint32_t index);

/* In-place mutation (same size only — cannot grow values) */
bool mfx_mutate_int(mfx_ref ref, int64_t val);
bool mfx_mutate_uint(mfx_ref ref, uint64_t val);
bool mfx_mutate_float(mfx_ref ref, double val);
bool mfx_mutate_bool(mfx_ref ref, bool val);
bool mfx_mutate_string(mfx_ref ref, const char* val, uint32_t max_len);
bool mfx_mutate_blob(mfx_ref ref, const uint8_t* data, uint32_t size);

/* Optional upfront verification for untrusted buffers. */
bool mfx_verify(const uint8_t* buf, uint32_t size);

/* ── Writer ─────────────────────────────────────────────────────── */

typedef struct mfx_builder mfx_builder;

mfx_builder* mfx_builder_create(void);
void         mfx_builder_destroy(mfx_builder* b);
void         mfx_builder_clear(mfx_builder* b);
bool         mfx_builder_ok(const mfx_builder* b);
const char*  mfx_builder_error(const mfx_builder* b);

/* Building values — key may be NULL for vector elements */
void mfx_build_null(mfx_builder* b, const char* key);
void mfx_build_int(mfx_builder* b, const char* key, int64_t val);
void mfx_build_uint(mfx_builder* b, const char* key, uint64_t val);
void mfx_build_float(mfx_builder* b, const char* key, float val);
void mfx_build_double(mfx_builder* b, const char* key, double val);
void mfx_build_indirect_int(mfx_builder* b, const char* key, int64_t val);
void mfx_build_indirect_uint(mfx_builder* b, const char* key, uint64_t val);
void mfx_build_indirect_float(mfx_builder* b, const char* key, float val);
void mfx_build_indirect_double(mfx_builder* b, const char* key, double val);
void mfx_build_bool(mfx_builder* b, const char* key, bool val);
void mfx_build_key_value(mfx_builder* b, const char* key, const char* val);
void mfx_build_string(mfx_builder* b, const char* key, const char* val);
/* Write a fixed-length null-padded string (for pre-allocated mutable fields). */
void mfx_build_string_n(mfx_builder* b, const char* key, const char* val, uint32_t fixed_len);
void mfx_build_blob(mfx_builder* b, const char* key, const uint8_t* data, uint32_t size);
/* Pre-allocate a zero-filled blob of given size (for later in-place mutation). */
void mfx_build_blob_n(mfx_builder* b, const char* key, uint32_t size);

/* Containers */
void mfx_build_map_start(mfx_builder* b, const char* key);
void mfx_build_map_end(mfx_builder* b);
void mfx_build_vector_start(mfx_builder* b, const char* key);
void mfx_build_vector_end(mfx_builder* b);
void mfx_build_typed_vector_start(mfx_builder* b, const char* key, mfx_type elem_type);
void mfx_build_typed_vector_end(mfx_builder* b);
void mfx_build_fixed_vector_start(mfx_builder* b, const char* key, mfx_type elem_type, uint32_t arity);
void mfx_build_fixed_vector_end(mfx_builder* b);

/* Finalize — returns malloc'd buffer. Caller owns it. Resets builder. */
uint8_t* mfx_build_finish(mfx_builder* b, uint32_t* out_size);

#endif /* MINIFLEX_H */

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "miniflex/miniflex.h"

static uint8_t *read_file(const char *path, uint32_t *out_size) {
    FILE *f;
    long len;
    uint8_t *buf;

    *out_size = 0;
    f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    len = ftell(f);
    if (len < 0) {
        fclose(f);
        return NULL;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }

    buf = (uint8_t *)malloc((size_t)len);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    if (len > 0 && fread(buf, 1, (size_t)len, f) != (size_t)len) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *out_size = (uint32_t)len;
    return buf;
}

static bool write_file(const char *path, const uint8_t *buf, uint32_t size) {
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    if (size > 0 && fwrite(buf, 1, size, f) != size) {
        fclose(f);
        return false;
    }
    fclose(f);
    return true;
}

static uint8_t *build_case(const char *name, uint32_t *out_size) {
    mfx_builder *b = mfx_builder_create();
    uint8_t *buf = NULL;
    uint8_t blob[4] = {1, 2, 3, 4};

    if (strcmp(name, "scalar_int") == 0) {
        mfx_build_int(b, NULL, -12345);
    } else if (strcmp(name, "scalar_null") == 0) {
        mfx_build_null(b, NULL);
    } else if (strcmp(name, "scalar_uint") == 0) {
        mfx_build_uint(b, NULL, 1234567890123ULL);
    } else if (strcmp(name, "scalar_float") == 0) {
        mfx_build_double(b, NULL, 3.25);
    } else if (strcmp(name, "scalar_bool") == 0) {
        mfx_build_bool(b, NULL, true);
    } else if (strcmp(name, "scalar_string") == 0) {
        mfx_build_string(b, NULL, "hello");
    } else if (strcmp(name, "scalar_blob") == 0) {
        mfx_build_blob(b, NULL, blob, 4);
    } else if (strcmp(name, "vector_mixed") == 0) {
        mfx_build_vector_start(b, NULL);
        mfx_build_int(b, NULL, 7);
        mfx_build_string(b, NULL, "two");
        mfx_build_bool(b, NULL, true);
        mfx_build_vector_end(b);
    } else if (strcmp(name, "typed_vector_int") == 0) {
        mfx_build_typed_vector_start(b, NULL, MFX_FBT_INT);
        mfx_build_int(b, NULL, -1);
        mfx_build_int(b, NULL, 2);
        mfx_build_int(b, NULL, 300);
        mfx_build_typed_vector_end(b);
    } else if (strcmp(name, "typed_vector_uint") == 0) {
        mfx_build_typed_vector_start(b, NULL, MFX_FBT_UINT);
        mfx_build_uint(b, NULL, 1);
        mfx_build_uint(b, NULL, 2);
        mfx_build_uint(b, NULL, 4000000000ULL);
        mfx_build_typed_vector_end(b);
    } else if (strcmp(name, "typed_vector_float") == 0) {
        mfx_build_typed_vector_start(b, NULL, MFX_FBT_FLOAT);
        mfx_build_double(b, NULL, 1.25);
        mfx_build_double(b, NULL, 2.5);
        mfx_build_double(b, NULL, 8.75);
        mfx_build_typed_vector_end(b);
    } else if (strcmp(name, "fixed_vector_int2") == 0) {
        mfx_build_fixed_vector_start(b, NULL, MFX_FBT_INT, 2);
        mfx_build_int(b, NULL, -7);
        mfx_build_int(b, NULL, 11);
        mfx_build_fixed_vector_end(b);
    } else if (strcmp(name, "fixed_vector_float3") == 0) {
        mfx_build_fixed_vector_start(b, NULL, MFX_FBT_FLOAT, 3);
        mfx_build_float(b, NULL, 1.5f);
        mfx_build_float(b, NULL, 2.5f);
        mfx_build_float(b, NULL, 3.5f);
        mfx_build_fixed_vector_end(b);
    } else if (strcmp(name, "fixed_vector_uint4") == 0) {
        mfx_build_fixed_vector_start(b, NULL, MFX_FBT_UINT, 4);
        mfx_build_uint(b, NULL, 4);
        mfx_build_uint(b, NULL, 5);
        mfx_build_uint(b, NULL, 6);
        mfx_build_uint(b, NULL, 7);
        mfx_build_fixed_vector_end(b);
    } else if (strcmp(name, "vector_bool") == 0) {
        mfx_build_typed_vector_start(b, NULL, MFX_FBT_BOOL);
        mfx_build_bool(b, NULL, true);
        mfx_build_bool(b, NULL, false);
        mfx_build_bool(b, NULL, true);
        mfx_build_typed_vector_end(b);
    } else if (strcmp(name, "vector_key") == 0) {
        mfx_build_typed_vector_start(b, NULL, MFX_FBT_KEY);
        mfx_build_key_value(b, NULL, "a");
        mfx_build_key_value(b, NULL, "b");
        mfx_build_typed_vector_end(b);
    } else if (strcmp(name, "vector_string_deprecated") == 0) {
        mfx_build_typed_vector_start(b, NULL, MFX_FBT_STRING);
        mfx_build_string(b, NULL, "alpha");
        mfx_build_string(b, NULL, "beta");
        mfx_build_typed_vector_end(b);
    } else if (strcmp(name, "map_nested") == 0) {
        mfx_build_map_start(b, NULL);
        mfx_build_uint(b, "count", 2);
        mfx_build_vector_start(b, "items");
        mfx_build_map_start(b, NULL);
        mfx_build_string(b, "name", "first");
        mfx_build_bool(b, "ok", true);
        mfx_build_map_end(b);
        mfx_build_map_start(b, NULL);
        mfx_build_string(b, "name", "second");
        mfx_build_bool(b, "ok", false);
        mfx_build_map_end(b);
        mfx_build_vector_end(b);
        mfx_build_map_end(b);
    } else if (strcmp(name, "indirect_scalars_map") == 0) {
        mfx_build_map_start(b, NULL);
        mfx_build_indirect_int(b, "i", -77);
        mfx_build_indirect_uint(b, "u", 99999);
        mfx_build_indirect_double(b, "f", 6.5);
        mfx_build_map_end(b);
    } else {
        mfx_builder_destroy(b);
        return NULL;
    }

    buf = mfx_build_finish(b, out_size);
    mfx_builder_destroy(b);
    return buf;
}

static bool check_case(const char *name, const uint8_t *buf, uint32_t size) {
    mfx_ref root;

    if (!mfx_verify(buf, size)) return false;
    root = mfx_root(buf, size);

    if (strcmp(name, "scalar_int") == 0) return mfx_int(root) == -12345;
    if (strcmp(name, "scalar_null") == 0) return mfx_is_null(root);
    if (strcmp(name, "scalar_uint") == 0) return mfx_uint(root) == 1234567890123ULL;
    if (strcmp(name, "scalar_float") == 0) return fabs(mfx_double(root) - 3.25) < 0.000001;
    if (strcmp(name, "scalar_bool") == 0) return mfx_bool(root) == true;
    if (strcmp(name, "scalar_string") == 0) return strcmp(mfx_string(root, NULL), "hello") == 0;
    if (strcmp(name, "scalar_blob") == 0) {
        uint32_t n = 0;
        const uint8_t *p = mfx_blob(root, &n);
        return n == 4 && p && p[0] == 1 && p[3] == 4;
    }
    if (strcmp(name, "vector_mixed") == 0) {
        return mfx_vec_size(root) == 3 &&
               mfx_int(mfx_vec_at(root, 0)) == 7 &&
               strcmp(mfx_string(mfx_vec_at(root, 1), NULL), "two") == 0 &&
               mfx_bool(mfx_vec_at(root, 2)) == true;
    }
    if (strcmp(name, "typed_vector_int") == 0) {
        return mfx_is_typed_vector(root) &&
               mfx_vec_elem_type(root) == MFX_FBT_INT &&
               mfx_vec_size(root) == 3 &&
               mfx_int(mfx_vec_at(root, 2)) == 300;
    }
    if (strcmp(name, "typed_vector_uint") == 0) {
        return mfx_is_typed_vector(root) &&
               mfx_vec_elem_type(root) == MFX_FBT_UINT &&
               mfx_vec_size(root) == 3 &&
               mfx_uint(mfx_vec_at(root, 2)) == 4000000000ULL;
    }
    if (strcmp(name, "typed_vector_float") == 0) {
        return mfx_is_typed_vector(root) &&
               mfx_vec_elem_type(root) == MFX_FBT_FLOAT &&
               mfx_vec_size(root) == 3 &&
               fabs(mfx_double(mfx_vec_at(root, 2)) - 8.75) < 0.000001;
    }
    if (strcmp(name, "fixed_vector_int2") == 0) {
        return mfx_is_fixed_typed_vector(root) &&
               mfx_vec_elem_type(root) == MFX_FBT_INT &&
               mfx_vec_size(root) == 2 &&
               mfx_int(mfx_vec_at(root, 0)) == -7 &&
               mfx_int(mfx_vec_at(root, 1)) == 11;
    }
    if (strcmp(name, "fixed_vector_float3") == 0) {
        return mfx_is_fixed_typed_vector(root) &&
               mfx_vec_elem_type(root) == MFX_FBT_FLOAT &&
               mfx_vec_size(root) == 3 &&
               fabs(mfx_double(mfx_vec_at(root, 1)) - 2.5) < 0.000001;
    }
    if (strcmp(name, "fixed_vector_uint4") == 0) {
        return mfx_is_fixed_typed_vector(root) &&
               mfx_vec_elem_type(root) == MFX_FBT_UINT &&
               mfx_vec_size(root) == 4 &&
               mfx_uint(mfx_vec_at(root, 0)) == 4 &&
               mfx_uint(mfx_vec_at(root, 3)) == 7;
    }
    if (strcmp(name, "vector_bool") == 0) {
        return mfx_is_typed_vector(root) &&
               mfx_vec_elem_type(root) == MFX_FBT_BOOL &&
               mfx_vec_size(root) == 3 &&
               mfx_bool(mfx_vec_at(root, 0)) == true &&
               mfx_bool(mfx_vec_at(root, 1)) == false &&
               mfx_bool(mfx_vec_at(root, 2)) == true;
    }
    if (strcmp(name, "vector_key") == 0) {
        return strcmp(mfx_key(mfx_vec_at(root, 0), NULL), "a") == 0 &&
               strcmp(mfx_key(mfx_vec_at(root, 1), NULL), "b") == 0;
    }
    if (strcmp(name, "vector_string_deprecated") == 0) {
        return strcmp(mfx_string(mfx_vec_at(root, 0), NULL), "alpha") == 0 &&
               strcmp(mfx_string(mfx_vec_at(root, 1), NULL), "beta") == 0;
    }
    if (strcmp(name, "map_nested") == 0) {
        mfx_ref items = mfx_map_at(root, "items");
        return mfx_is_map(root) &&
               mfx_uint(mfx_map_at(root, "count")) == 2 &&
               mfx_vec_size(items) == 2 &&
               strcmp(mfx_string(mfx_map_at(mfx_vec_at(items, 0), "name"), NULL), "first") == 0 &&
               mfx_bool(mfx_map_at(mfx_vec_at(items, 0), "ok")) == true &&
               strcmp(mfx_string(mfx_map_at(mfx_vec_at(items, 1), "name"), NULL), "second") == 0 &&
               mfx_bool(mfx_map_at(mfx_vec_at(items, 1), "ok")) == false;
    }
    if (strcmp(name, "indirect_scalars_map") == 0) {
        return mfx_int(mfx_map_at(root, "i")) == -77 &&
               mfx_uint(mfx_map_at(root, "u")) == 99999 &&
               fabs(mfx_double(mfx_map_at(root, "f")) - 6.5) < 0.000001;
    }

    return false;
}

int main(int argc, char **argv) {
    uint32_t size = 0;
    uint8_t *buf = NULL;

    if (argc != 4) {
        fprintf(stderr, "usage: %s <emit|check> <case> <path>\n", argv[0]);
        return 2;
    }

    if (strcmp(argv[1], "emit") == 0) {
        buf = build_case(argv[2], &size);
        if (!buf) return 1;
        if (!write_file(argv[3], buf, size)) {
            free(buf);
            return 1;
        }
        free(buf);
        return 0;
    }

    if (strcmp(argv[1], "check") == 0) {
        buf = read_file(argv[3], &size);
        if (!buf) return 1;
        if (!check_case(argv[2], buf, size)) {
            free(buf);
            return 1;
        }
        free(buf);
        return 0;
    }

    return 2;
}

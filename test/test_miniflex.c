/*
 * Core C tests for miniflex.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "miniflex/miniflex.h"

static int tests_run = 0;
static int tests_passed = 0;

#define ASSERT(cond, msg) do { \
    tests_run++; \
    if (!(cond)) { \
        fprintf(stderr, "  FAIL: %s (line %d): %s\n", msg, __LINE__, #cond); \
    } else { \
        tests_passed++; \
    } \
} while (0)

static void test_simple_map(void) {
    uint32_t size = 0;
    uint8_t *data;
    mfx_builder *b = mfx_builder_create();
    mfx_ref root;

    printf("test_simple_map...\n");

    mfx_build_map_start(b, NULL);
    mfx_build_int(b, "count", 42);
    mfx_build_double(b, "temperature", 98.6);
    mfx_build_string(b, "name", "sensor_1");
    mfx_build_map_end(b);

    data = mfx_build_finish(b, &size);
    mfx_builder_destroy(b);

    ASSERT(data != NULL, "builder produced data");
    ASSERT(size > 0, "builder produced non-zero size");
    ASSERT(mfx_verify(data, size), "buffer verifies");

    root = mfx_root(data, size);
    ASSERT(mfx_is_map(root), "root is map");
    ASSERT(mfx_int(mfx_map_at(root, "count")) == 42, "count == 42");
    ASSERT(fabs(mfx_double(mfx_map_at(root, "temperature")) - 98.6) < 0.001, "temperature ~= 98.6");
    ASSERT(strcmp(mfx_string(mfx_map_at(root, "name"), NULL), "sensor_1") == 0, "name == sensor_1");
    ASSERT(mfx_is_null(mfx_map_at(root, "nonexistent")), "missing key is null");

    free(data);
}

static void test_vector(void) {
    uint32_t size = 0;
    uint8_t *data;
    mfx_builder *b = mfx_builder_create();
    mfx_ref root;

    printf("test_vector...\n");

    mfx_build_vector_start(b, NULL);
    mfx_build_int(b, NULL, 10);
    mfx_build_int(b, NULL, 20);
    mfx_build_int(b, NULL, 30);
    mfx_build_vector_end(b);

    data = mfx_build_finish(b, &size);
    mfx_builder_destroy(b);

    root = mfx_root(data, size);
    ASSERT(mfx_is_vector(root), "root is vector");
    ASSERT(!mfx_is_typed_vector(root), "root is untyped vector");
    ASSERT(mfx_vec_size(root) == 3, "vector size == 3");
    ASSERT(mfx_int(mfx_vec_at(root, 0)) == 10, "vec[0] == 10");
    ASSERT(mfx_int(mfx_vec_at(root, 1)) == 20, "vec[1] == 20");
    ASSERT(mfx_int(mfx_vec_at(root, 2)) == 30, "vec[2] == 30");

    free(data);
}

static void test_nested_map(void) {
    uint32_t size = 0;
    uint8_t *data;
    uint8_t guid[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    mfx_builder *b = mfx_builder_create();
    mfx_ref root;
    mfx_ref meta;

    printf("test_nested_map...\n");

    mfx_build_map_start(b, NULL);
    mfx_build_vector_start(b, "_meta");
    mfx_build_double(b, NULL, 1234567890.123);
    mfx_build_uint(b, NULL, 0xFFFFFFFFFFFFFFFFULL);
    mfx_build_blob(b, NULL, guid, 16);
    mfx_build_string(b, NULL, "test_node");
    mfx_build_string(b, NULL, "core");
    mfx_build_string(b, NULL, "TestMessage");
    mfx_build_vector_end(b);
    mfx_build_int(b, "value", -100);
    mfx_build_map_end(b);

    data = mfx_build_finish(b, &size);
    mfx_builder_destroy(b);

    root = mfx_root(data, size);
    meta = mfx_map_at(root, "_meta");

    ASSERT(mfx_is_map(root), "root is map");
    ASSERT(mfx_is_vector(meta), "_meta is vector");
    ASSERT(mfx_vec_size(meta) == 6, "_meta has 6 elements");
    ASSERT(fabs(mfx_double(mfx_vec_at(meta, 0)) - 1234567890.123) < 0.001, "timestamp correct");
    ASSERT(mfx_uint(mfx_vec_at(meta, 1)) == 0xFFFFFFFFFFFFFFFFULL, "counter correct");

    {
        uint32_t blob_size = 0;
        const uint8_t *blob_data = mfx_blob(mfx_vec_at(meta, 2), &blob_size);
        ASSERT(blob_size == 16, "guid blob size == 16");
        ASSERT(blob_data != NULL && blob_data[0] == 1 && blob_data[15] == 16, "guid data correct");
    }

    ASSERT(strcmp(mfx_string(mfx_vec_at(meta, 4), NULL), "core") == 0, "module name correct");
    ASSERT(mfx_int(mfx_map_at(root, "value")) == -100, "value == -100");

    free(data);
}

static void test_blob(void) {
    uint32_t size = 0;
    uint8_t img_data[256];
    uint8_t *data;
    mfx_builder *b = mfx_builder_create();
    mfx_ref root;
    mfx_ref img;

    printf("test_blob...\n");

    for (int i = 0; i < 256; i++) img_data[i] = (uint8_t)i;

    mfx_build_map_start(b, NULL);
    mfx_build_blob(b, "image", img_data, 256);
    mfx_build_uint(b, "width", 16);
    mfx_build_uint(b, "height", 16);
    mfx_build_map_end(b);

    data = mfx_build_finish(b, &size);
    mfx_builder_destroy(b);

    root = mfx_root(data, size);
    img = mfx_map_at(root, "image");

    {
        uint32_t img_size = 0;
        const uint8_t *img_ptr = mfx_blob(img, &img_size);
        ASSERT(img_size == 256, "blob size == 256");
        ASSERT(img_ptr != NULL, "blob pointer not null");
        ASSERT(img_ptr[0] == 0 && img_ptr[255] == 255, "blob data correct");
    }

    ASSERT(mfx_uint(mfx_map_at(root, "width")) == 16, "width == 16");
    ASSERT(mfx_uint(mfx_map_at(root, "height")) == 16, "height == 16");

    free(data);
}

static void test_mutation(void) {
    uint32_t size = 0;
    uint8_t *data;
    mfx_builder *b = mfx_builder_create();
    mfx_ref root;
    mfx_ref val;
    mfx_ref cnt;

    printf("test_mutation...\n");

    mfx_build_map_start(b, NULL);
    mfx_build_double(b, "val", 1.0);
    mfx_build_uint(b, "count", 0);
    mfx_build_bool(b, "flag", false);
    mfx_build_map_end(b);

    data = mfx_build_finish(b, &size);
    mfx_builder_destroy(b);

    root = mfx_root(data, size);
    val = mfx_map_at(root, "val");
    ASSERT(mfx_mutate_float(val, 99.9), "mutate float succeeded");

    root = mfx_root(data, size);
    val = mfx_map_at(root, "val");
    ASSERT(fabs(mfx_double(val) - 99.9) < 0.001, "mutated value correct");

    cnt = mfx_map_at(root, "count");
    ASSERT(mfx_mutate_uint(cnt, 42), "mutate uint succeeded");
    root = mfx_root(data, size);
    cnt = mfx_map_at(root, "count");
    ASSERT(mfx_uint(cnt) == 42, "mutated count correct");

    ASSERT(mfx_mutate_bool(mfx_map_at(root, "flag"), true), "mutate bool succeeded");
    root = mfx_root(data, size);
    ASSERT(mfx_bool(mfx_map_at(root, "flag")), "mutated bool correct");

    free(data);
}

static void test_string_n(void) {
    uint32_t size = 0;
    uint8_t *data;
    mfx_builder *b = mfx_builder_create();
    mfx_ref root;
    mfx_ref name;

    printf("test_string_n...\n");

    mfx_build_map_start(b, NULL);
    mfx_build_string_n(b, "name", "hello", 32);
    mfx_build_map_end(b);

    data = mfx_build_finish(b, &size);
    mfx_builder_destroy(b);

    root = mfx_root(data, size);
    name = mfx_map_at(root, "name");

    {
        uint32_t len = 0;
        const char *s = mfx_string(name, &len);
        ASSERT(len == 32, "fixed-length string reports len == 32");
        ASSERT(strncmp(s, "hello", 5) == 0, "string content correct");
    }

    ASSERT(mfx_mutate_string(name, "world_updated", 32), "mutate string succeeded");
    root = mfx_root(data, size);
    name = mfx_map_at(root, "name");
    ASSERT(strncmp(mfx_string(name, NULL), "world_updated", 13) == 0, "mutated string correct");

    free(data);
}

static void test_blob_n(void) {
    uint32_t size = 0;
    uint8_t *data;
    mfx_builder *b = mfx_builder_create();
    mfx_ref root;
    mfx_ref guid;

    printf("test_blob_n...\n");

    mfx_build_map_start(b, NULL);
    mfx_build_blob_n(b, "guid", 16);
    mfx_build_map_end(b);

    data = mfx_build_finish(b, &size);
    mfx_builder_destroy(b);

    root = mfx_root(data, size);
    guid = mfx_map_at(root, "guid");

    {
        uint32_t blob_size = 0;
        const uint8_t *blob_ptr = mfx_blob(guid, &blob_size);
        int all_zero = 1;
        ASSERT(blob_size == 16, "pre-allocated blob size == 16");
        ASSERT(blob_ptr != NULL, "blob pointer not null");
        for (uint32_t i = 0; i < 16; i++) if (blob_ptr[i] != 0) all_zero = 0;
        ASSERT(all_zero, "pre-allocated blob is zeroed");
    }

    {
        uint8_t new_data[16] = {0xAA, 0xBB, 0xCC, 0xDD, 0, 0, 0, 0,
                                0, 0, 0, 0, 0, 0, 0, 0xFF};
        ASSERT(mfx_mutate_blob(guid, new_data, 16), "mutate blob succeeded");
    }

    root = mfx_root(data, size);
    guid = mfx_map_at(root, "guid");
    ASSERT(mfx_blob(guid, NULL)[0] == 0xAA && mfx_blob(guid, NULL)[15] == 0xFF, "mutated blob correct");

    free(data);
}

static void test_indirect_scalars(void) {
    uint32_t size = 0;
    uint8_t *data;
    mfx_builder *b = mfx_builder_create();
    mfx_ref root;

    printf("test_indirect_scalars...\n");

    mfx_build_map_start(b, NULL);
    mfx_build_indirect_int(b, "a", -12345);
    mfx_build_indirect_uint(b, "b", 0x12345678u);
    mfx_build_indirect_double(b, "c", 3.141592653589793);
    mfx_build_map_end(b);

    data = mfx_build_finish(b, &size);
    mfx_builder_destroy(b);

    root = mfx_root(data, size);
    ASSERT(mfx_int(mfx_map_at(root, "a")) == -12345, "indirect int read");
    ASSERT(mfx_uint(mfx_map_at(root, "b")) == 0x12345678u, "indirect uint read");
    ASSERT(fabs(mfx_double(mfx_map_at(root, "c")) - 3.141592653589793) < 1e-12, "indirect float read");

    ASSERT(mfx_mutate_int(mfx_map_at(root, "a"), -2222), "indirect int mutate");
    ASSERT(mfx_mutate_uint(mfx_map_at(root, "b"), 77), "indirect uint mutate");
    ASSERT(mfx_mutate_float(mfx_map_at(root, "c"), 2.5), "indirect float mutate");

    root = mfx_root(data, size);
    ASSERT(mfx_int(mfx_map_at(root, "a")) == -2222, "indirect int mutated value");
    ASSERT(mfx_uint(mfx_map_at(root, "b")) == 77, "indirect uint mutated value");
    ASSERT(fabs(mfx_double(mfx_map_at(root, "c")) - 2.5) < 1e-12, "indirect float mutated value");

    free(data);
}

static void test_typed_vector_int(void) {
    uint32_t size = 0;
    uint8_t *data;
    mfx_builder *b = mfx_builder_create();
    mfx_ref root;

    printf("test_typed_vector_int...\n");

    mfx_build_typed_vector_start(b, NULL, MFX_FBT_INT);
    mfx_build_int(b, NULL, -1);
    mfx_build_int(b, NULL, 2);
    mfx_build_int(b, NULL, 300);
    mfx_build_typed_vector_end(b);

    data = mfx_build_finish(b, &size);
    mfx_builder_destroy(b);

    root = mfx_root(data, size);
    ASSERT(mfx_is_typed_vector(root), "typed vector detected");
    ASSERT(mfx_vec_elem_type(root) == MFX_FBT_INT, "typed vector element type");
    ASSERT(mfx_vec_size(root) == 3, "typed vector size");
    ASSERT(mfx_int(mfx_vec_at(root, 0)) == -1, "typed vec[0]");
    ASSERT(mfx_int(mfx_vec_at(root, 1)) == 2, "typed vec[1]");
    ASSERT(mfx_int(mfx_vec_at(root, 2)) == 300, "typed vec[2]");

    free(data);
}

static void test_fixed_typed_vector_float(void) {
    uint32_t size = 0;
    uint8_t *data;
    mfx_builder *b = mfx_builder_create();
    mfx_ref root;

    printf("test_fixed_typed_vector_float...\n");

    mfx_build_fixed_vector_start(b, NULL, MFX_FBT_FLOAT, 3);
    mfx_build_float(b, NULL, 1.5f);
    mfx_build_float(b, NULL, 2.5f);
    mfx_build_float(b, NULL, 3.5f);
    mfx_build_fixed_vector_end(b);

    data = mfx_build_finish(b, &size);
    mfx_builder_destroy(b);

    root = mfx_root(data, size);
    ASSERT(mfx_is_fixed_typed_vector(root), "fixed typed vector detected");
    ASSERT(mfx_vec_elem_type(root) == MFX_FBT_FLOAT, "fixed typed vector element type");
    ASSERT(mfx_vec_size(root) == 3, "fixed typed vector size");
    ASSERT(fabs(mfx_double(mfx_vec_at(root, 0)) - 1.5) < 0.001, "fixed vec[0]");
    ASSERT(fabs(mfx_double(mfx_vec_at(root, 1)) - 2.5) < 0.001, "fixed vec[1]");
    ASSERT(fabs(mfx_double(mfx_vec_at(root, 2)) - 3.5) < 0.001, "fixed vec[2]");

    free(data);
}

static void test_vector_bool(void) {
    uint32_t size = 0;
    uint8_t *data;
    mfx_builder *b = mfx_builder_create();
    mfx_ref root;

    printf("test_vector_bool...\n");

    mfx_build_typed_vector_start(b, NULL, MFX_FBT_BOOL);
    mfx_build_bool(b, NULL, true);
    mfx_build_bool(b, NULL, false);
    mfx_build_bool(b, NULL, true);
    mfx_build_typed_vector_end(b);

    data = mfx_build_finish(b, &size);
    mfx_builder_destroy(b);

    root = mfx_root(data, size);
    ASSERT(mfx_is_typed_vector(root), "bool vector is typed");
    ASSERT(mfx_vec_elem_type(root) == MFX_FBT_BOOL, "bool vector element type");
    ASSERT(mfx_vec_size(root) == 3, "bool vector size");
    ASSERT(mfx_bool(mfx_vec_at(root, 0)) == true, "bool vec[0]");
    ASSERT(mfx_bool(mfx_vec_at(root, 1)) == false, "bool vec[1]");
    ASSERT(mfx_bool(mfx_vec_at(root, 2)) == true, "bool vec[2]");
    ASSERT(mfx_mutate_bool(mfx_vec_at(root, 1), true), "bool vector element mutate");
    root = mfx_root(data, size);
    ASSERT(mfx_bool(mfx_vec_at(root, 1)) == true, "bool vector element mutated value");

    free(data);
}

static void test_vector_string_deprecated(void) {
    uint32_t size = 0;
    uint8_t *data;
    mfx_builder *b = mfx_builder_create();
    mfx_ref root;

    printf("test_vector_string_deprecated...\n");

    mfx_build_typed_vector_start(b, NULL, MFX_FBT_STRING);
    mfx_build_string(b, NULL, "alpha");
    mfx_build_string(b, NULL, "beta");
    mfx_build_typed_vector_end(b);

    data = mfx_build_finish(b, &size);
    mfx_builder_destroy(b);

    root = mfx_root(data, size);
    ASSERT(mfx_is_typed_vector(root), "deprecated string vector is typed");
    ASSERT(mfx_vec_size(root) == 2, "deprecated string vector size");
    ASSERT(strcmp(mfx_string(mfx_vec_at(root, 0), NULL), "alpha") == 0, "deprecated string vec[0]");
    ASSERT(strcmp(mfx_string(mfx_vec_at(root, 1), NULL), "beta") == 0, "deprecated string vec[1]");

    free(data);
}

static void test_vector_key(void) {
    uint32_t size = 0;
    uint8_t *data;
    mfx_builder *b = mfx_builder_create();
    mfx_ref root;

    printf("test_vector_key...\n");

    mfx_build_typed_vector_start(b, NULL, MFX_FBT_KEY);
    mfx_build_key_value(b, NULL, "k0");
    mfx_build_key_value(b, NULL, "k1");
    mfx_build_typed_vector_end(b);

    data = mfx_build_finish(b, &size);
    mfx_builder_destroy(b);

    root = mfx_root(data, size);
    ASSERT(mfx_is_typed_vector(root), "key vector is typed");
    ASSERT(mfx_vec_elem_type(root) == MFX_FBT_KEY, "key vector element type");
    ASSERT(strcmp(mfx_key(mfx_vec_at(root, 0), NULL), "k0") == 0, "key vec[0]");
    ASSERT(strcmp(mfx_key(mfx_vec_at(root, 1), NULL), "k1") == 0, "key vec[1]");

    free(data);
}

static void test_verify_failures(void) {
    uint32_t size = 0;
    uint8_t *data;
    uint8_t *bad;
    mfx_builder *b = mfx_builder_create();

    printf("test_verify_failures...\n");

    mfx_build_map_start(b, NULL);
    mfx_build_uint(b, "x", 1);
    mfx_build_string(b, "y", "hello");
    mfx_build_map_end(b);
    data = mfx_build_finish(b, &size);
    mfx_builder_destroy(b);

    ASSERT(mfx_verify(data, size), "valid buffer verifies");
    ASSERT(!mfx_verify(data, size - 1), "truncated buffer fails verify");

    bad = (uint8_t *)malloc(size);
    memcpy(bad, data, size);
    bad[size - 1] = 3;
    ASSERT(!mfx_verify(bad, size), "bad root width fails verify");
    free(bad);
    free(data);
}

static void test_mutation_failures(void) {
    uint32_t size = 0;
    uint8_t *data;
    uint8_t bytes[8] = {0};
    mfx_builder *b = mfx_builder_create();
    mfx_ref root;

    printf("test_mutation_failures...\n");

    mfx_build_map_start(b, NULL);
    mfx_build_uint(b, "small", 255);
    mfx_build_int(b, "tiny", 127);
    mfx_build_string_n(b, "name", "abc", 4);
    mfx_build_blob_n(b, "blob", 4);
    mfx_build_map_end(b);

    data = mfx_build_finish(b, &size);
    mfx_builder_destroy(b);
    root = mfx_root(data, size);

    ASSERT(!mfx_mutate_uint(mfx_map_at(root, "small"), 256), "uint mutation overflow rejected");
    ASSERT(!mfx_mutate_int(mfx_map_at(root, "tiny"), 128), "int mutation overflow rejected");
    ASSERT(!mfx_mutate_string(mfx_map_at(root, "name"), "toolong", 4), "string mutation overflow rejected");
    ASSERT(!mfx_mutate_blob(mfx_map_at(root, "blob"), bytes, 8), "blob mutation overflow rejected");

    free(data);
}

static void test_builder_errors(void) {
    mfx_builder *b = mfx_builder_create();
    uint32_t size = 0;

    printf("test_builder_errors...\n");

    mfx_build_fixed_vector_start(b, NULL, MFX_FBT_INT, 300);
    ASSERT(!mfx_builder_ok(b), "builder error state set");
    ASSERT(strstr(mfx_builder_error(b), "arity") != NULL, "builder error message mentions arity");
    ASSERT(mfx_build_finish(b, &size) == NULL, "finish fails after builder error");
    ASSERT(size == 0, "size stays zero on builder failure");

    mfx_builder_destroy(b);
}

int main(void) {
    test_simple_map();
    test_vector();
    test_nested_map();
    test_blob();
    test_mutation();
    test_string_n();
    test_blob_n();
    test_indirect_scalars();
    test_typed_vector_int();
    test_fixed_typed_vector_float();
    test_vector_bool();
    test_vector_string_deprecated();
    test_vector_key();
    test_verify_failures();
    test_mutation_failures();
    test_builder_errors();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}

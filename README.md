# miniflex

`miniflex` is a compact pure-C implementation of the FlexBuffers wire format.

Reading is, for the most part, zero-copy: `mfx_ref` values point back into the original buffer, and string/blob accessors return pointers into that same storage.

FlexBuffers itself is part of the official [FlatBuffers repository](https://github.com/google/flatbuffers).

It can:

1. read the full FlexBuffers wire format
2. write the full FlexBuffers wire format
3. mutate fixed-width scalar values in place
4. mutate preallocated strings and blobs in place
5. optionally verify buffers before reading them

The public API lives in [include/miniflex/miniflex.h](/Users/colinprepscius/code/miniflex/include/miniflex/miniflex.h).

## Build

### With CMake

The default project build is C-only, with optional extra test/conformance targets.

Configure:

```sh
cmake -S . -B build
```

Build just the libraries:

```sh
cmake --build build --target miniflex miniflex_static
```

This produces:

1. `build/libminiflex.dylib`
2. `build/libminiflex.a`

Install headers, libraries, and the CMake package:

```sh
cmake --install build --prefix /your/prefix
```

If you want the standard C test suite:

```sh
cmake --build build --target test_miniflex
ctest --test-dir build --output-on-failure
```

For the separate oracle-backed conformance flow, see [test/conformance/README.md](/Users/colinprepscius/code/miniflex/test/conformance/README.md).

### Without CMake

You do not need CMake to use `miniflex`.

The library itself is just:

1. [include/miniflex/miniflex.h](/Users/colinprepscius/code/miniflex/include/miniflex/miniflex.h)
2. [src/miniflex.c](/Users/colinprepscius/code/miniflex/src/miniflex.c)

So you can compile it directly with any normal C toolchain. CMake is mainly here as the repository's reference build, test, conformance, and install system.

For example, building the static library directly with a C compiler:

```sh
cc -Iinclude -c src/miniflex.c -o miniflex.o
ar rcs libminiflex.a miniflex.o
```

Or on macOS, building a shared library directly:

```sh
cc -Iinclude -dynamiclib src/miniflex.c -o libminiflex.dylib
```

## Usage

The API has two halves:

1. a zero-allocation reader based on `mfx_ref`
2. a builder for creating FlexBuffers values

### Read A Buffer

Start from the root ref, then inspect and descend:

```c
#include "miniflex/miniflex.h"

void read_example(const uint8_t *buf, uint32_t size) {
    mfx_ref root = mfx_root(buf, size);

    if (!mfx_is_map(root)) return;

    int64_t count = mfx_int(mfx_map_at(root, "count"));
    const char *name = mfx_string(mfx_map_at(root, "name"), NULL);

    mfx_ref items = mfx_map_at(root, "items");
    if (mfx_is_vector(items)) {
        for (uint32_t i = 0; i < mfx_vec_size(items); i++) {
            mfx_ref item = mfx_vec_at(items, i);
            /* inspect item */
        }
    }
}
```

### Build A Buffer

Build bottom-up, then call `mfx_build_finish()` to get the final byte buffer:

```c
#include <stdlib.h>
#include "miniflex/miniflex.h"

uint8_t *build_example(uint32_t *out_size) {
    mfx_builder *b = mfx_builder_create();

    mfx_build_map_start(b, NULL);
    mfx_build_int(b, "count", 42);
    mfx_build_string(b, "name", "sensor_1");

    mfx_build_vector_start(b, "values");
    mfx_build_double(b, NULL, 1.5);
    mfx_build_double(b, NULL, 2.5);
    mfx_build_double(b, NULL, 3.5);
    mfx_build_vector_end(b);

    mfx_build_map_end(b);

    uint8_t *buf = mfx_build_finish(b, out_size);
    mfx_builder_destroy(b);
    return buf;  /* caller owns it */
}
```

### Nested Example

This is a more realistic “map containing a vector of maps” example:

```c
mfx_builder *b = mfx_builder_create();
uint32_t size = 0;

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

uint8_t *buf = mfx_build_finish(b, &size);
mfx_builder_destroy(b);

mfx_ref root = mfx_root(buf, size);
mfx_ref items = mfx_map_at(root, "items");

for (uint32_t i = 0; i < mfx_vec_size(items); i++) {
    mfx_ref item = mfx_vec_at(items, i);
    const char *name = mfx_string(mfx_map_at(item, "name"), NULL);
    bool ok = mfx_bool(mfx_map_at(item, "ok"));
    (void)name;
    (void)ok;
}
```

### Typed Vectors

Use typed/fixed typed vector builders when you want the compact typed-vector encodings from FlexBuffers:

```c
mfx_builder *b = mfx_builder_create();

mfx_build_typed_vector_start(b, NULL, MFX_FBT_INT);
mfx_build_int(b, NULL, 10);
mfx_build_int(b, NULL, 20);
mfx_build_int(b, NULL, 30);
mfx_build_typed_vector_end(b);

uint8_t *buf = mfx_build_finish(b, NULL);
```

And on read:

```c
mfx_ref root = mfx_root(buf, size);

if (mfx_is_typed_vector(root)) {
    mfx_type elem_type = mfx_vec_elem_type(root);
    uint32_t n = mfx_vec_size(root);
    (void)elem_type;
    (void)n;
}
```

### In-Place Mutation

For fixed-width scalars and preallocated string/blob storage, you can mutate the original buffer in place:

```c
mfx_builder *b = mfx_builder_create();

mfx_build_map_start(b, NULL);
mfx_build_uint(b, "count", 0);
mfx_build_string_n(b, "name", "hello", 32);
mfx_build_blob_n(b, "blob", 16);
mfx_build_map_end(b);

uint32_t size = 0;
uint8_t *buf = mfx_build_finish(b, &size);
mfx_builder_destroy(b);

mfx_ref root = mfx_root(buf, size);
mfx_mutate_uint(mfx_map_at(root, "count"), 42);
mfx_mutate_string(mfx_map_at(root, "name"), "updated", 32);
```

Mutation does not resize the buffer. New values must fit in the existing storage.

### Verify Untrusted Input

For untrusted data, verify once before reading:

```c
if (!mfx_verify(buf, size)) {
    /* reject malformed buffer */
    return;
}

mfx_ref root = mfx_root(buf, size);
```

## API Notes

1. `mfx_ref` is a lightweight view into an existing FlexBuffers buffer. It does not own memory.
2. Reader access is zero-allocation. Returned strings/blobs point into the original buffer.
3. `mfx_build_finish()` returns a heap buffer owned by the caller. Free it with `free()`.
4. `mfx_verify()` is optional. Use it for untrusted input; skip it for trusted/internal buffers if you want the lowest overhead.
5. `mfx_is_vector()` is true for untyped vectors, typed vectors, and fixed typed vectors. Use `mfx_is_typed_vector()`, `mfx_is_fixed_typed_vector()`, and `mfx_vec_elem_type()` when you need the exact vector kind.
6. `mfx_string()` reads strings only. Use `mfx_key()` for key refs and `mfx_map_key_at()` for map key iteration.
7. In-place mutation never changes buffer size, layout, offsets, or container structure. It only overwrites existing storage.
8. Numeric mutation succeeds only when the new value fits in the existing stored width.
9. `mfx_build_string_n()` and `mfx_build_blob_n()` are miniflex extensions for preallocated mutable fields.

### Builder Errors

Builder APIs accumulate errors internally. If construction fails, `mfx_build_finish()` returns `NULL`.

You can query the builder state directly:

```c
if (!mfx_builder_ok(b)) {
    const char *why = mfx_builder_error(b);
    (void)why;
}
```

### Installed CMake Usage

After installing, a CMake consumer can do:

```cmake
find_package(miniflex REQUIRED CONFIG)
target_link_libraries(your_target PRIVATE miniflex::miniflex)
```

## Tests

There are two test layers:

1. core C tests in [test/test_miniflex.c](/Users/colinprepscius/code/miniflex/test/test_miniflex.c)
2. opt-in conformance tests against the official FlexBuffers implementation in [test/conformance/](/Users/colinprepscius/code/miniflex/test/conformance)

If you want the normal C test binary:

```sh
cmake --build build --target test_miniflex
ctest --test-dir build --output-on-failure
```

The conformance tests are separate on purpose and are not part of the default build. See [test/conformance/README.md](/Users/colinprepscius/code/miniflex/test/conformance/README.md).

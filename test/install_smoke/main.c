#include <stdint.h>
#include <stdlib.h>

#include "miniflex/miniflex.h"

int main(void) {
    mfx_builder *b = mfx_builder_create();
    uint32_t size = 0;
    uint8_t *buf;
    mfx_ref root;

    mfx_build_map_start(b, NULL);
    mfx_build_uint(b, "count", 42);
    mfx_build_map_end(b);

    buf = mfx_build_finish(b, &size);
    mfx_builder_destroy(b);
    if (!buf) return 1;

    if (!mfx_verify(buf, size)) {
        free(buf);
        return 2;
    }

    root = mfx_root(buf, size);
    if (mfx_uint(mfx_map_at(root, "count")) != 42) {
        free(buf);
        return 3;
    }

    free(buf);
    return 0;
}

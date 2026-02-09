#ifndef LZ4_LEGACY_H
#define LZ4_LEGACY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Decompress a single LZ4 block (legacy/raw block format).
// Returns decompressed size, or -1 on error.
int lz4_legacy_decompress_block(const uint8_t* src, int src_size,
				uint8_t* dst, int dst_capacity);

#ifdef __cplusplus
}
#endif

#endif

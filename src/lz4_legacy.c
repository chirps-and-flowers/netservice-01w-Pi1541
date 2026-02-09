// Legacy LZ4 block wrapper using the official LZ4 decoder.

#include "lz4_legacy.h"

#include "lz4.h"

int lz4_legacy_decompress_block(const uint8_t* src, int src_size,
				uint8_t* dst, int dst_capacity)
{
	if (!src || !dst || src_size < 0 || dst_capacity < 0)
		return -1;

	return LZ4_decompress_safe((const char*)src, (char*)dst, src_size, dst_capacity);
}

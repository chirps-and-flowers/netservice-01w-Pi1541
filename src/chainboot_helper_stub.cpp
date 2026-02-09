// chainboot_helper_stub.cpp
//
// Legacy emulator -> service transition:
// Read the chainloader kernel from SD into RAM at a fixed address and jump.
//
// The chainloader is linked for 0x01000000 and then loads kernel_srv.* into
// 0x8000 and transfers control to the service kernel.

#include "chainboot_helper_stub.h"

#include "ff-local.h"
extern "C" {
#include "startup.h"
}

#include <string.h>

namespace {
// 0x01000000 is a safe staging area on Pi Zero W:
// - leaves 0x8000 free for the service kernel image
// - avoids overlapping the legacy emulator kernel image
static const unsigned kChainloaderAddr = 0x01000000;
static const unsigned kMaxChainloaderSize = 2 * 1024 * 1024;
}

static bool ReadChainloaderRaw(const char* name, unsigned char* dest, unsigned int* outSize)
{
	if (!name || !dest || !outSize)
		return false;

	FIL file;
	if (f_open(&file, name, FA_READ) != FR_OK)
		return false;

	const unsigned int fileSize = f_size(&file);
	if (fileSize == 0 || fileSize > kMaxChainloaderSize)
	{
		f_close(&file);
		return false;
	}

	unsigned int offset = 0;
	const unsigned int chunk = 64 * 1024;
	while (offset < fileSize)
	{
		const unsigned int remaining = fileSize - offset;
		const unsigned int toRead = remaining < chunk ? remaining : chunk;
		UINT br = 0;
		if (f_read(&file, dest + offset, toRead, &br) != FR_OK || br == 0)
		{
			f_close(&file);
			return false;
		}
		offset += br;
	}

	f_close(&file);
	if (offset != fileSize)
		return false;

	*outSize = fileSize;
	return true;
}

extern "C" void ChainBootChainloader(const char* chainloaderName)
{
	unsigned int fileSize = 0;
	if (!ReadChainloaderRaw(chainloaderName, reinterpret_cast<unsigned char*>(kChainloaderAddr), &fileSize))
		return;

	_disable_interrupts();
	_data_memory_barrier();
	_clean_invalidate_dcache();
	_invalidate_icache();
	_invalidate_dtlb();
	_data_memory_barrier();

	_chainboot_to_address(reinterpret_cast<void*>(kChainloaderAddr));

	while (1) { }
}

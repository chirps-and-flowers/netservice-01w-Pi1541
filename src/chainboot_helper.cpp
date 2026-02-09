// Minimal chainboot chainloader kernel (legacy toolchain).
//
// This kernel is loaded by the emulator kernel into RAM at 0x01000000.
// Its only job is to load kernel_srv.* into 0x8000 and jump there with a clean
// CPU/IRQ state.

#include <string.h>

#include "chainboot_legacy.h"
#include "diskio.h"
#include "emmc.h"
#include "ff-local.h"

extern "C" {
#include "cache.h"
#include "rpiHardware.h"
#include "startup.h"
}

namespace {
static CEMMCDevice g_emmc;
static FATFS g_fs;
} // namespace

extern "C" {
DWORD get_fattime(void)
{
	// No RTC in helper environment; return a stable timestamp.
	return 0;
}

// Needed by rpi-mailbox.c (and other low-level code) in helper builds.
void usDelay(unsigned nMicroSeconds)
{
	for (u32 count = 0; count < nMicroSeconds; ++count)
	{
		const unsigned before = read32(ARM_SYSTIMER_CLO);
		unsigned after;
		do
		{
			after = read32(ARM_SYSTIMER_CLO);
		} while (after == before);
	}
}

void MsDelay(unsigned nMilliSeconds)
{
	usDelay(nMilliSeconds * 1000);
}
}

extern "C" void kernel_main(unsigned int r0, unsigned int r1, unsigned int atags)
{
	(void)r0;
	(void)r1;
	(void)atags;

	// Bring up SD (EMMC) and mount FatFS.
	g_emmc.Initialize();
	disk_setEMM(&g_emmc);
	if (f_mount(&g_fs, "SD:", 1) != FR_OK)
	{
		while (1) { }
	}

	// Match the emulator defaults for performance before the large read.
	enable_MMU_and_IDCaches();
	_enable_unaligned_access();

	// Load + jump to service kernel (tries /kernel_srv.lz4 first, then /kernel_srv.img).
	ChainBootLegacy("/kernel_srv.img");

	// Should never return.
	while (1) { }
}

// chainboot_legacy.cpp
//
// Load a service kernel image into 0x8000 and chainboot to it.
// Supported inputs:
// - raw kernel image (e.g. /kernel_srv.img)
// - legacy LZ4 stream (e.g. /kernel_srv.lz4), tried first when available
//
// This is used by the legacy chainloader kernel only (not linked into the
// cycle-exact emulator build).
#include "chainboot_legacy.h"
#include "ff-local.h"
extern "C" {
#include "startup.h"
#include "emmc.h"
#include "rpiHardware.h"
}
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#include "lz4_legacy.h"

// Where the Circle service kernel expects to start.
static const unsigned kServiceEntryAddr = 0x8000;
// Circle default KERNEL_MAX_SIZE is 8 MiB; use a slightly generous guard.
static const unsigned kMaxKernelSize = 8 * 1024 * 1024;
static const uint32_t kLz4LegacyMagic = 0x184C2102; // LZ4 legacy (little-endian)

// Keep helper lean: no LCD/UI calls from the helper chainboot path.
#if defined(PI1541_CHAINBOOT_HELPER)
static inline unsigned now_us(void)
{
	return read32(ARM_SYSTIMER_CLO);
}

static inline unsigned delta_us(unsigned start, unsigned end)
{
	return end - start; // wraps naturally for unsigned 32-bit
}

static void HelperLog(const char* line0, const char* line1)
{
	if (!line0 && !line1)
		return;

	FIL file;
	if (f_open(&file, "SD:/pi1541_helper.log", FA_OPEN_ALWAYS | FA_WRITE) != FR_OK)
		return;

	// Append to end.
	f_lseek(&file, f_size(&file));

	char buf[96];
	if (line0 && line1)
		snprintf(buf, sizeof(buf), "%s | %s\r\n", line0, line1);
	else if (line0)
		snprintf(buf, sizeof(buf), "%s\r\n", line0);
	else
		snprintf(buf, sizeof(buf), "%s\r\n", line1);

	UINT bw = 0;
	f_write(&file, buf, (UINT)strlen(buf), &bw);
	f_close(&file);
}

static void HelperLogKV(const char* tag, unsigned t0, unsigned t1, unsigned a, unsigned b)
{
	char msg[120];
	snprintf(msg, sizeof(msg), "%s t0=%u t1=%u dt_us=%u a=%u b=%u",
		 tag ? tag : "?", t0, t1, delta_us(t0, t1), a, b);
	HelperLog(msg, 0);
}
#endif

static void ChainbootShow2(const char* line0, const char* line1)
{
#if defined(PI1541_CHAINBOOT_HELPER)
	HelperLog(line0, line1);
#else
	(void)line0;
	(void)line1;
#endif
}

static int ReadKernelFileRaw(const char* kernelName, unsigned char* dest, unsigned int* outSize)
{
	FIL file;
	FRESULT res;
	UINT bytesRead = 0;
	char line0[32];
	char line1[32];

	if (!kernelName || !dest || !outSize)
		return 0;

#if defined(PI1541_CHAINBOOT_HELPER)
	const unsigned t_raw0 = now_us();
#endif
	snprintf(line0, sizeof(line0), "CHAINBOOT");
	snprintf(line1, sizeof(line1), "OPEN %s", kernelName);
	ChainbootShow2(line0, line1);

	res = f_open(&file, kernelName, FA_READ);
	if (res != FR_OK)
	{
		ChainbootShow2("CHAINBOOT ERR", "OPEN");
#if defined(PI1541_CHAINBOOT_HELPER)
		HelperLogKV("raw_open_fail", t_raw0, now_us(), (unsigned)res, 0);
#endif
		return 0;
	}

	unsigned int fileSize = f_size(&file);
	if (fileSize == 0 || fileSize > kMaxKernelSize)
	{
		ChainbootShow2("CHAINBOOT ERR", "SIZE");
		f_close(&file);
#if defined(PI1541_CHAINBOOT_HELPER)
		HelperLogKV("raw_size_bad", t_raw0, now_us(), fileSize, kMaxKernelSize);
#endif
		return 0;
	}

	snprintf(line0, sizeof(line0), "CHAINBOOT");
	snprintf(line1, sizeof(line1), "SIZE %uK", (unsigned int)((fileSize + 1023U) / 1024U));
	ChainbootShow2(line0, line1);

	// Read via FatFs in chunks so the OLED can show progress.
	unsigned int offset = 0;
	unsigned int lastShown = 0;
	const unsigned int chunk = 64 * 1024;
#if defined(PI1541_CHAINBOOT_HELPER)
	const unsigned t_read0 = now_us();
#endif
	while (offset < fileSize)
	{
		const unsigned int remaining = fileSize - offset;
		const unsigned int toRead = remaining < chunk ? remaining : chunk;
		UINT br = 0;
		res = f_read(&file, dest + offset, toRead, &br);
		if (res != FR_OK || br == 0)
		{
			f_close(&file);
			ChainbootShow2("CHAINBOOT ERR", "READ");
			return 0;
		}
		offset += br;
		bytesRead = offset;

		// Update every 256 KiB (or at end) to keep overhead low.
		if ((offset - lastShown) >= (256 * 1024) || offset == fileSize)
		{
			lastShown = offset;
			const unsigned int pct = (unsigned int)(((unsigned long long)offset * 100ULL) / (unsigned long long)fileSize);
			snprintf(line0, sizeof(line0), "CHAINBOOT READ");
			snprintf(line1, sizeof(line1), "%u%% %uK/%uK", pct,
					 (unsigned int)((offset + 1023U) / 1024U),
					 (unsigned int)((fileSize + 1023U) / 1024U));
			ChainbootShow2(line0, line1);
		}
	}
	f_close(&file);
	if (res != FR_OK || bytesRead != fileSize)
	{
		ChainbootShow2("CHAINBOOT ERR", "READ");
#if defined(PI1541_CHAINBOOT_HELPER)
		HelperLogKV("raw_read_bad", t_read0, now_us(), bytesRead, fileSize);
#endif
		return 0;
	}

#if defined(PI1541_CHAINBOOT_HELPER)
	HelperLogKV("raw_read_ok", t_read0, now_us(), bytesRead, 0);
#endif
	*outSize = fileSize;
	return 1;
}

static int ReadKernelFileLz4Legacy(const char* kernelName, unsigned char* dest, unsigned int* outSize)
{
	FIL file;
	FRESULT res;
	UINT br = 0;
	char line0[32];
	char line1[32];

	if (!kernelName || !dest || !outSize)
		return 0;

#if defined(PI1541_CHAINBOOT_HELPER)
	const unsigned t_open0 = now_us();
#endif
	res = f_open(&file, kernelName, FA_READ);
	if (res != FR_OK)
		return 0; // not present; caller may fall back to raw

#if defined(PI1541_CHAINBOOT_HELPER)
	HelperLogKV("lz4_open_ok", t_open0, now_us(), (unsigned)f_size(&file), 0);
#endif
	snprintf(line0, sizeof(line0), "CHAINBOOT LZ4");
	snprintf(line1, sizeof(line1), "OPEN %s", kernelName);
	ChainbootShow2(line0, line1);

	uint32_t magic = 0;
	res = f_read(&file, &magic, sizeof(magic), &br);
	if (res != FR_OK || br != sizeof(magic) || magic != kLz4LegacyMagic)
	{
		ChainbootShow2("CHAINBOOT LZ4", "BAD MAGIC");
		f_close(&file);
#if defined(PI1541_CHAINBOOT_HELPER)
		HelperLogKV("lz4_magic_bad", t_open0, now_us(), magic, kLz4LegacyMagic);
#endif
		return -1;
	}

	const unsigned int fileSize = f_size(&file);
	unsigned int inOffset = sizeof(magic);
	unsigned int outOffset = 0;
	unsigned int lastShown = 0;
#if defined(PI1541_CHAINBOOT_HELPER)
	const unsigned t_stream0 = now_us();
	unsigned read_us_acc = 0;
	unsigned dec_us_acc = 0;
	unsigned blocks = 0;
#endif

	while (1)
	{
		uint32_t blockSize = 0;
		if (inOffset + sizeof(blockSize) > fileSize)
			break; // legacy stream can end without a zero terminator
		res = f_read(&file, &blockSize, sizeof(blockSize), &br);
		if (res != FR_OK || br != sizeof(blockSize))
		{
			ChainbootShow2("CHAINBOOT LZ4", "READ ERR");
			f_close(&file);
			return -1;
		}
		inOffset += sizeof(blockSize);
		if (blockSize == 0)
			break;

		const unsigned int uncompressed = (blockSize & 0x80000000u) != 0;
		blockSize &= 0x7FFFFFFFu;
		if (blockSize > (fileSize - inOffset))
		{
			ChainbootShow2("CHAINBOOT LZ4", "TRUNCATED");
			f_close(&file);
			return -1;
		}
		if (blockSize == 0 || blockSize > kMaxKernelSize)
		{
			ChainbootShow2("CHAINBOOT LZ4", "BAD SIZE");
			f_close(&file);
			return -1;
		}

		unsigned char* block = (unsigned char*)malloc(blockSize);
		if (!block)
		{
			ChainbootShow2("CHAINBOOT LZ4", "NO MEM");
			f_close(&file);
#if defined(PI1541_CHAINBOOT_HELPER)
			HelperLogKV("lz4_nomem", t_stream0, now_us(), blockSize, 0);
#endif
			return -1;
		}

#if defined(PI1541_CHAINBOOT_HELPER)
		const unsigned t_blk_read0 = now_us();
#endif
		res = f_read(&file, block, blockSize, &br);
		if (res != FR_OK || br != blockSize)
		{
			free(block);
			ChainbootShow2("CHAINBOOT LZ4", "READ ERR");
			f_close(&file);
#if defined(PI1541_CHAINBOOT_HELPER)
			HelperLogKV("lz4_read_fail", t_blk_read0, now_us(), (unsigned)res, blockSize);
#endif
			return -1;
		}
#if defined(PI1541_CHAINBOOT_HELPER)
		const unsigned t_blk_read1 = now_us();
		read_us_acc += delta_us(t_blk_read0, t_blk_read1);
		const unsigned t_blk_dec0 = now_us();
#endif
		inOffset += blockSize;

		if (uncompressed)
		{
			if (outOffset + blockSize > kMaxKernelSize)
			{
				free(block);
				ChainbootShow2("CHAINBOOT LZ4", "TOO BIG");
				f_close(&file);
				return -1;
			}
			memcpy(dest + outOffset, block, blockSize);
			outOffset += blockSize;
		}
		else
		{
			const int dec = lz4_legacy_decompress_block(block, (int)blockSize,
								    dest + outOffset,
								    (int)(kMaxKernelSize - outOffset));
			if (dec < 0)
			{
				free(block);
				ChainbootShow2("CHAINBOOT LZ4", "DECOMP ERR");
				f_close(&file);
				return -1;
			}
			outOffset += (unsigned int)dec;
		}
#if defined(PI1541_CHAINBOOT_HELPER)
		const unsigned t_blk_dec1 = now_us();
		dec_us_acc += delta_us(t_blk_dec0, t_blk_dec1);
		blocks++;
#endif
		free(block);

		if ((inOffset - lastShown) >= (256 * 1024) || inOffset >= fileSize)
		{
			lastShown = inOffset;
			const unsigned int pct = fileSize ? (unsigned int)(((unsigned long long)inOffset * 100ULL) / (unsigned long long)fileSize) : 0;
			snprintf(line0, sizeof(line0), "CHAINBOOT LZ4");
			snprintf(line1, sizeof(line1), "%u%% %uK", pct, (unsigned int)((outOffset + 1023U) / 1024U));
			ChainbootShow2(line0, line1);
		}
	}

	f_close(&file);
#if defined(PI1541_CHAINBOOT_HELPER)
	{
		const unsigned t_stream1 = now_us();
		HelperLogKV("lz4_done", t_stream0, t_stream1, outOffset, blocks);
		HelperLogKV("lz4_read_us", t_stream0, t_stream0 + read_us_acc, read_us_acc, 0);
		HelperLogKV("lz4_dec_us", t_stream0, t_stream0 + dec_us_acc, dec_us_acc, 0);
		const unsigned total_us = delta_us(t_stream0, t_stream1);
		const unsigned overhead_us = (total_us > (read_us_acc + dec_us_acc))
						     ? (total_us - read_us_acc - dec_us_acc)
						     : 0;
		HelperLogKV("lz4_overhead_us", t_stream0, t_stream0 + overhead_us, overhead_us, 0);
		const unsigned read_rate = read_us_acc
					    ? (unsigned)((unsigned long long)fileSize * 1000000ULL / read_us_acc)
					    : 0;
		HelperLogKV("lz4_read_rate", t_stream0, t_stream0, read_rate, fileSize);
	}
#endif
	*outSize = outOffset;
	return 1;
}

static int TryReadKernelFileLz4(const char* kernelName, unsigned char* dest, unsigned int* outSize)
{
	char lz4Name[64];
	const char* ext = kernelName ? strrchr(kernelName, '.') : 0;
	if (kernelName && ext && strcmp(ext, ".img") == 0)
	{
		const size_t baseLen = (size_t)(ext - kernelName);
		if (baseLen + 4 < sizeof(lz4Name))
		{
			memcpy(lz4Name, kernelName, baseLen);
			memcpy(lz4Name + baseLen, ".lz4", 5);
			const int res = ReadKernelFileLz4Legacy(lz4Name, dest, outSize);
			if (res != 0)
				return res;
		}
	}

	if (kernelName && (strlen(kernelName) + 4) < sizeof(lz4Name))
	{
		snprintf(lz4Name, sizeof(lz4Name), "%s.lz4", kernelName);
		const int res = ReadKernelFileLz4Legacy(lz4Name, dest, outSize);
		if (res != 0)
			return res;
	}

	return 0;
}

void ChainBootLegacy(const char* kernelName)
{
	unsigned char* dest = (unsigned char*)kServiceEntryAddr;
	unsigned int fileSize = 0;

#if defined(PI1541_CHAINBOOT_HELPER)
	const unsigned t_cb0 = now_us();
#endif
	const int lz4res = TryReadKernelFileLz4(kernelName, dest, &fileSize);
	if (lz4res < 0)
		return;
	if (lz4res == 0)
	{
		if (!ReadKernelFileRaw(kernelName, dest, &fileSize))
			return;
	}
#if defined(PI1541_CHAINBOOT_HELPER)
	HelperLogKV("load_done", t_cb0, now_us(), fileSize, kServiceEntryAddr);
#endif

	// Make the new image visible to the CPU, then jump with a clean CPU state.
	// Silent teardown+jump to avoid OLED flicker.
#if defined(PI1541_CHAINBOOT_HELPER)
	const unsigned t_td0 = now_us();
#endif
	_disable_interrupts();
	_data_memory_barrier();
	_clean_invalidate_dcache();
	_invalidate_icache();
	_invalidate_dtlb();
	_data_memory_barrier();
#if defined(PI1541_CHAINBOOT_HELPER)
	HelperLogKV("teardown", t_td0, now_us(), 0, 0);
#endif

	// Disable all interrupt sources in the ARM interrupt controller.
	// We mask IRQ/FIQ in CPSR above, but Circle may enable interrupts early
	// during bring-up. If legacy left any sources enabled/pending, Circle can
	// vector into the *old* handler table (or take an interrupt before it has
	// installed its own vectors), causing an immediate crash or reboot.
	//
	// This makes the handoff closer to a cold boot where no IRQ sources are
	// enabled yet.
	write32(ARM_IC_DISABLE_IRQS_1, 0xFFFFFFFF);
	write32(ARM_IC_DISABLE_IRQS_2, 0xFFFFFFFF);
	write32(ARM_IC_DISABLE_BASIC_IRQS, 0xFFFFFFFF);
	write32(ARM_IC_FIQ_CONTROL, 0);

	// NOTE: Do NOT power off the EMMC/SD bus here.
	//
	// On Pi Zero W, Circle's WLAN driver uses the EMMC controller (SDIO) to talk
	// to the BCM4343. If we clear the SD Bus Power bit during chainboot, Circle
	// can still mount the SD card (it uses SDHOST when USE_SDHOST is enabled),
	// but Wi-Fi will often come up "with an IP" yet the host is unreachable
	// (no ping/HTTP) because the SDIO controller never re-powers in the WLAN
	// init path.
	//
	// If we ever need stricter SD teardown, we should reset/idle the *SD card*
	// side without cutting EMMC power globally.

	_chainboot_to_address((void*)kServiceEntryAddr);

	// Should never return
	while (1) { }
}

// Pi1541 - A Commodore 1541 disk drive emulator
// Copyright(C) 2018 Stephen White
//
// This file is part of Pi1541.
// 
// Pi1541 is free software : you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// Pi1541 is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with Pi1541. If not, see <http://www.gnu.org/licenses/>.

#include <string.h>
#include <stdlib.h>
#include "defs.h"
#include "DiskCaddy.h"
#include "debug.h"
#if !defined(__CIRCLE__)
#if defined(__PICO2__) || defined(ESP32)
#include "ff.h"
#else
#include "ff-local.h"
#endif
extern "C"
{
#include "rpi-gpio.h"	// For SetACTLed
}
#endif

extern u8 deviceID;

static const u32 screenPosXCaddySelections = 240;
static const u32 screenPosYCaddySelections = 280;
static char buffer[256] = { 0 };
static u32 white = RGBA(0xff, 0xff, 0xff, 0xff);
static u32 red = RGBA(0xff, 0, 0, 0xff);
static u32 redDark = RGBA(0x88, 0, 0, 0xff);
static u32 redMid = RGBA(0xcc, 0, 0, 0xff);
static u32 grey = RGBA(0x88, 0x88, 0x88, 0xff);
static u32 greyDark = RGBA(0x44, 0x44, 0x44, 0xff);

static const char kModifiedListDir[] = "/1541/_active_mount";
static const char kModifiedListPath[] = "/1541/_active_mount/dirty.lst";
static const char kModifiedListTmpPath[] = "/1541/_active_mount/dirty.lst.tmp";
static const char kModifiedListRenameFailedPath[] = "/1541/_active_mount/dirty.tmp.failed";
static const unsigned kModifiedMax = 32;

static bool EnsureDirLegacy(const char *path)
{
	if (!path || !path[0])
		return false;
	FILINFO fi;
	if (f_stat(path, &fi) == FR_OK)
		return (fi.fattrib & AM_DIR) != 0;
	FRESULT fr = f_mkdir(path);
	return fr == FR_OK || fr == FR_EXIST;
}

static bool EnsureModifiedListDir(void)
{
	// Create /1541 and /1541/_active_mount if missing.
	return EnsureDirLegacy("/1541") && EnsureDirLegacy(kModifiedListDir);
}

static void TrimLine(char *s)
{
	if (!s)
		return;
	size_t len = strlen(s);
	while (len && (s[len - 1] == '\n' || s[len - 1] == '\r' || s[len - 1] == ' ' || s[len - 1] == '\t'))
		s[--len] = '\0';
	size_t start = 0;
	while (s[start] == ' ' || s[start] == '\t')
		++start;
	if (start)
		memmove(s, s + start, strlen(s + start) + 1);
}

static bool LoadLines(const char *path, char out[][256], unsigned &out_count, unsigned max_count)
{
	out_count = 0;
	FIL fp;
	if (f_open(&fp, path, FA_READ) != FR_OK)
		return false;

	// Lines are bounded at 255 chars because our writer enforces that limit.
	// Read-side truncation therefore matches write-side constraints.
	char line[256];
	unsigned pos = 0;
	UINT br = 0;
	char ch = 0;
	while (f_read(&fp, &ch, 1, &br) == FR_OK && br == 1)
	{
		if (ch == '\r')
			continue;
		if (ch == '\n')
		{
			line[pos] = '\0';
			TrimLine(line);
			pos = 0;
			if (line[0] && out_count < max_count)
			{
				snprintf(out[out_count], 256, "%s", line);
				++out_count;
			}
			continue;
		}
		if (pos + 1 < sizeof(line))
			line[pos++] = ch;
	}
	if (pos)
	{
		line[pos] = '\0';
		TrimLine(line);
		if (line[0] && out_count < max_count)
		{
			snprintf(out[out_count], 256, "%s", line);
			++out_count;
		}
	}
	f_close(&fp);
	return true;
}

static bool WriteLinesToFileLegacy(const char *path, char lines[][256], unsigned count)
{
	FIL fp;
	FRESULT fr = f_open(&fp, path, FA_CREATE_ALWAYS | FA_WRITE);
	if (fr != FR_OK)
		return false;

	UINT bw = 0;
	for (unsigned i = 0; i < count; ++i)
	{
		if (!lines[i][0])
			continue;
		fr = f_write(&fp, lines[i], (UINT)strlen(lines[i]), &bw);
		if (fr != FR_OK)
		{
			f_close(&fp);
			return false;
		}
		const char nl = '\n';
		fr = f_write(&fp, &nl, 1, &bw);
		if (fr != FR_OK)
		{
			f_close(&fp);
			return false;
		}
	}

	fr = f_sync(&fp);
	f_close(&fp);
	return fr == FR_OK;
}

static void WriteRenameFailedMarker(FRESULT rename_fr)
{
	FIL fp;
	if (f_open(&fp, kModifiedListRenameFailedPath, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
		return;

	char msg[32];
	snprintf(msg, sizeof(msg), "rename=%d\n", static_cast<int>(rename_fr));
	UINT bw = 0;
	(void) f_write(&fp, msg, (UINT) strlen(msg), &bw);
	(void) f_sync(&fp);
	(void) f_close(&fp);
}

static bool WriteLinesAtomic(const char *tmp_path, const char *final_path, char lines[][256], unsigned count)
{
	FIL fp;
	FRESULT fr = f_open(&fp, tmp_path, FA_CREATE_ALWAYS | FA_WRITE);
	if (fr != FR_OK)
		return false;

	UINT bw = 0;
	for (unsigned i = 0; i < count; ++i)
	{
		if (!lines[i][0])
			continue;
		fr = f_write(&fp, lines[i], (UINT)strlen(lines[i]), &bw);
		if (fr != FR_OK)
		{
			f_close(&fp);
			return false;
		}
		const char nl = '\n';
		fr = f_write(&fp, &nl, 1, &bw);
		if (fr != FR_OK)
		{
			f_close(&fp);
			return false;
		}
	}
	fr = f_sync(&fp);
	if (fr != FR_OK)
	{
		f_close(&fp);
		return false;
	}
	f_close(&fp);

	// Prefer legacy behavior: remove destination then rename.
	(void) f_unlink(final_path);
	fr = f_rename(tmp_path, final_path);
	if (fr == FR_OK)
	{
		(void) f_unlink(kModifiedListRenameFailedPath);
		return true;
	}

	// Fallback: write the final file directly so the service kernel still sees it.
	if (WriteLinesToFileLegacy(final_path, lines, count))
	{
		WriteRenameFailedMarker(fr);
		(void) f_unlink(tmp_path);
		return true;
	}

	// Leave the tmp file in place so the service kernel can still read it.
	WriteRenameFailedMarker(fr);
	return false;
}

static void UpdateModifiedList(char modifiedPaths[][256], unsigned modifiedCount)
{
	if (!modifiedCount)
		return;
	if (!EnsureModifiedListDir())
		return;

	char existing[kModifiedMax][256];
	unsigned existingCount = 0;
	LoadLines(kModifiedListPath, existing, existingCount, kModifiedMax);

	bool changed = false;
	for (unsigned i = 0; i < modifiedCount; ++i)
	{
		if (!modifiedPaths[i][0])
			continue;
		bool found = false;
		for (unsigned j = 0; j < existingCount; ++j)
		{
			if (strcmp(existing[j], modifiedPaths[i]) == 0)
			{
				found = true;
				break;
			}
		}
		if (!found && existingCount < kModifiedMax)
		{
			snprintf(existing[existingCount], 256, "%s", modifiedPaths[i]);
			++existingCount;
			changed = true;
		}
	}

	if (changed)
	{
		if (!WriteLinesAtomic(kModifiedListTmpPath, kModifiedListPath, existing, existingCount))
			return;
	}
}

bool DiskCaddy::Empty()
{
	int x;
	int y;
	int index;
	bool anyDirty = false;
	char modifiedPaths[kModifiedMax][256];
	unsigned modifiedCount = 0;
	for (unsigned i = 0; i < kModifiedMax; ++i)
		modifiedPaths[i][0] = '\0';

#if not defined(EXPERIMENTALZERO)
	if (screen)
		screen->Clear(RGBA(0x40, 0x31, 0x8D, 0xFF));
#endif

	for (index = 0; index < (int)disks.size(); ++index)
	{
		if (disks[index]->IsDirty())
		{
			anyDirty = true;
			// Record the full path as seen by the emulator. This list is used by
			// the service kernel to offer modified disk export/download.
			const char *nm = disks[index]->GetName();
			if (nm && nm[0] && modifiedCount < kModifiedMax)
			{
				snprintf(modifiedPaths[modifiedCount], 256, "%s", nm);
				++modifiedCount;
			}
#if not defined(EXPERIMENTALZERO)
			if (screen)
			{
				x = screen->ScaleX(screenPosXCaddySelections);
				y = screen->ScaleY(screenPosYCaddySelections);

				snprintf(buffer, 256, "Saving %s", disks[index]->GetName());
				screen->PrintText(false, x, y, buffer, RGBA(0xff, 0xff, 0xff, 0xff), red);
			}
#endif
#if !defined(__PICO2__)
			if (screenLCD)
			{
				RGBA BkColour = RGBA(0, 0, 0, 0xFF);
				screenLCD->Clear(BkColour);
				x = 0;
				y = 0;

				snprintf(buffer, 256, "Saving");
				screenLCD->PrintText(false, x, y, buffer, RGBA(0xff, 0xff, 0xff, 0xff), BkColour);
				y += screenLCD->GetFontHeight();
				snprintf(buffer, 256, "%s                ", disks[index]->GetName());
				screenLCD->PrintText(false, x, y, buffer, RGBA(0xff, 0xff, 0xff, 0xff), red);
				screenLCD->SwapBuffers();
			}
#endif
		}
		disks[index]->Close();
		delete disks[index];
	}

	if (anyDirty)
	{
#if not defined(EXPERIMENTALZERO)
		if (screen)
		{
			x = screen->ScaleX(screenPosXCaddySelections);
			y = screen->ScaleY(screenPosYCaddySelections);

			snprintf(buffer, 256, "                     Saving Complete                    ");
			screen->PrintText(false, x, y, buffer, RGBA(0xff, 0xff, 0xff, 0xff), red);
		}
#endif
#if !defined(__PICO2__)
		if (screenLCD)
		{
			RGBA BkColour = RGBA(0, 0, 0, 0xFF);
			screenLCD->Clear(BkColour);
			x = 0;
			y = 0;

			snprintf(buffer, 256, "Saving");
			screenLCD->PrintText(false, x, y, buffer, RGBA(0xff, 0xff, 0xff, 0xff), BkColour);
			y += screenLCD->GetFontHeight();
			snprintf(buffer, 256, "Complete                ");
			screenLCD->PrintText(false, x, y, buffer, RGBA(0xff, 0xff, 0xff, 0xff), red);
			screenLCD->SwapBuffers();
		}
#endif
	}

	disks.clear();
	selectedIndex = 0;
	oldCaddyIndex = 0;

	// Persist modified disks outside emulation loop.
	if (anyDirty)
		UpdateModifiedList(modifiedPaths, modifiedCount);

	return anyDirty;
}

bool DiskCaddy::Insert(const FILINFO* fileInfo, bool readOnly)
{
	int x;
	int y;
	bool success;
	FIL fp;
	FRESULT res = f_open(&fp, fileInfo->fname, FA_READ);
	if (res == FR_OK)
	{
#if not defined(EXPERIMENTALZERO)
		if (screen)
		{
			x = screen->ScaleX(screenPosXCaddySelections);
			y = screen->ScaleY(screenPosYCaddySelections);

			snprintf(buffer, 256, "                                                        ");
			screen->PrintText(false, x, y, buffer, RGBA(0xff, 0xff, 0xff, 0xff), red);

			snprintf(buffer, 256, "Loading %s", fileInfo->fname);
			screen->PrintText(false, x, y, buffer, RGBA(0xff, 0xff, 0xff, 0xff), red);
		}
#endif
#if !defined(__PICO2__)
		if (screenLCD)
		{
			RGBA BkColour = RGBA(0, 0, 0, 0xFF);
			screenLCD->Clear(BkColour);
			x = 0;
			y = 0;

			snprintf(buffer, 256, "Loading");
			screenLCD->PrintText(false, x, y, buffer, RGBA(0xff, 0xff, 0xff, 0xff), BkColour);
			y += screenLCD->GetFontHeight();
			snprintf(buffer, 256, "%s                ", fileInfo->fname);
			screenLCD->PrintText(false, x, y, buffer, RGBA(0xff, 0xff, 0xff, 0xff), red);
			screenLCD->SwapBuffers();
		}
#endif
		UINT bytesRead;
		SetACTLed(true);
		memset(DiskImage::readBuffer, 0xff, READBUFFER_SIZE);
		f_read(&fp, DiskImage::readBuffer, READBUFFER_SIZE, &bytesRead);
		SetACTLed(false);
		f_close(&fp);

		DiskImage::DiskType diskType = DiskImage::GetDiskImageTypeViaExtention(fileInfo->fname);
		switch (diskType)
		{
			case DiskImage::D64:
				success = InsertD64(fileInfo, (unsigned char*)DiskImage::readBuffer, bytesRead, readOnly);
				break;
			case DiskImage::G64:
				success = InsertG64(fileInfo, (unsigned char*)DiskImage::readBuffer, bytesRead, readOnly);
				break;
			case DiskImage::NIB:
				success = InsertNIB(fileInfo, (unsigned char*)DiskImage::readBuffer, bytesRead, readOnly);
				break;
			case DiskImage::NBZ:
				success = InsertNBZ(fileInfo, (unsigned char*)DiskImage::readBuffer, bytesRead, readOnly);
				break;
#if defined(PI1581SUPPORT)				
			case DiskImage::D81:
				success = InsertD81(fileInfo, (unsigned char*)DiskImage::readBuffer, bytesRead, readOnly);
				break;
#endif				
			case DiskImage::T64:
				success = InsertT64(fileInfo, (unsigned char*)DiskImage::readBuffer, bytesRead, readOnly);
				break;
			case DiskImage::PRG:
				success = InsertPRG(fileInfo, (unsigned char*)DiskImage::readBuffer, bytesRead, readOnly);
				break;
			default:
				success = false;
				break;
		}
		if (success)
		{
			DEBUG_LOG("Mounted into caddy %s - %d\r\n", fileInfo->fname, bytesRead);
		}
	}
	else
	{
		DEBUG_LOG("Failed to open %s\r\n", fileInfo->fname);
		success = false;
	}

	oldCaddyIndex = 0;

	return success;
}

bool DiskCaddy::InsertD64(const FILINFO* fileInfo, unsigned char* diskImageData, unsigned size, bool readOnly)
{
	DiskImage* diskImage = new DiskImage();
	if (diskImage->OpenD64(fileInfo, diskImageData, size))
	{
		diskImage->SetReadOnly(readOnly);
		disks.push_back(diskImage);
		selectedIndex = disks.size() - 1;
		return true;
	}
	delete diskImage;
	return false;
}

bool DiskCaddy::InsertG64(const FILINFO* fileInfo, unsigned char* diskImageData, unsigned size, bool readOnly)
{
	DiskImage* diskImage = new DiskImage();
	if (diskImage->OpenG64(fileInfo, diskImageData, size))
	{
		diskImage->SetReadOnly(readOnly);
		disks.push_back(diskImage);
		//DEBUG_LOG("Disks size = %d\r\n", disks.size());
		selectedIndex = disks.size() - 1;
		return true;
	}
	delete diskImage;
	return false;
}

bool DiskCaddy::InsertNIB(const FILINFO* fileInfo, unsigned char* diskImageData, unsigned size, bool readOnly)
{
	DiskImage* diskImage = new DiskImage();
	if (diskImage->OpenNIB(fileInfo, diskImageData, size))
	{
		// At the moment we cannot write out NIB files.
		diskImage->SetReadOnly(true);// readOnly);
		disks.push_back(diskImage);
		selectedIndex = disks.size() - 1;
		return true;
	}
	delete diskImage;
	return false;
}

bool DiskCaddy::InsertNBZ(const FILINFO* fileInfo, unsigned char* diskImageData, unsigned size, bool readOnly)
{
	DiskImage* diskImage = new DiskImage();
	if (diskImage->OpenNBZ(fileInfo, diskImageData, size))
	{
		// At the moment we cannot write out NIB files.
		diskImage->SetReadOnly(true);// readOnly);
		disks.push_back(diskImage);
		selectedIndex = disks.size() - 1;
		return true;
	}
	delete diskImage;
	return false;
}

bool DiskCaddy::InsertD81(const FILINFO* fileInfo, unsigned char* diskImageData, unsigned size, bool readOnly)
{
	DiskImage* diskImage = new DiskImage();
	if (diskImage->OpenD81(fileInfo, diskImageData, size))
	{
		diskImage->SetReadOnly(readOnly);
		disks.push_back(diskImage);
		selectedIndex = disks.size() - 1;
		return true;
	}
	delete diskImage;
	return false;
}

bool DiskCaddy::InsertT64(const FILINFO* fileInfo, unsigned char* diskImageData, unsigned size, bool readOnly)
{
	DiskImage* diskImage = new DiskImage();
	if (diskImage->OpenT64(fileInfo, diskImageData, size))
	{
		diskImage->SetReadOnly(readOnly);
		disks.push_back(diskImage);
		selectedIndex = disks.size() - 1;
		return true;
	}
	delete diskImage;
	return false;
}

bool DiskCaddy::InsertPRG(const FILINFO* fileInfo, unsigned char* diskImageData, unsigned size, bool readOnly)
{
	DiskImage* diskImage = new DiskImage();
	if (diskImage->OpenPRG(fileInfo, diskImageData, size))
	{
		diskImage->SetReadOnly(readOnly);
		disks.push_back(diskImage);
		selectedIndex = disks.size() - 1;
		return true;
	}
	delete diskImage;
	return false;
}

void DiskCaddy::Display()
{
	unsigned numberOfImages = GetNumberOfImages();
	unsigned caddyIndex;
	int x;
	int y;
#if not defined(EXPERIMENTALZERO)
	if (screen)
	{
		x = screen->ScaleX(screenPosXCaddySelections);
		y = screen->ScaleY(screenPosYCaddySelections);

		snprintf(buffer, 256, "                                                        ");
		screen->PrintText(false, x, y, buffer, RGBA(0xff, 0xff, 0xff, 0xff), redDark);
		snprintf(buffer, 256, "  Emulating");
		screen->PrintText(false, x, y, buffer, RGBA(0xff, 0xff, 0xff, 0xff), redDark);
		y += 16;

		for (caddyIndex = 0; caddyIndex < numberOfImages; ++caddyIndex)
		{
			DiskImage* image = GetImage(caddyIndex);
			if (image)
			{
				const char* name = image->GetName();
				if (name)
				{
					snprintf(buffer, 256, "                                                        ");
					screen->PrintText(false, x, y, buffer, grey, greyDark);
					snprintf(buffer, 256, "  %d %s", caddyIndex + 1, name);
					screen->PrintText(false, x, y, buffer, grey, greyDark);
					y += 16;
				}
			}
		}
	}
#endif
	ShowSelectedImage(0);
}

void DiskCaddy::ShowSelectedImage(u32 index)
{
	DiskImage* image = GetImage(index);
	
	if (image == 0)
	{
		return;
	}
	u32 x;
	u32 y;
#if not defined(EXPERIMENTALZERO)
	if (screen)
	{
		x = screen->ScaleX(screenPosXCaddySelections);
		y = screen->ScaleY(screenPosYCaddySelections) + 16 + 16 * index;
		const char* name = image->GetName();
		if (name)
		{
			snprintf(buffer, 256, "* %d %s", index + 1, name);
		}
		screen->PrintText(false, x, y, buffer, white, red);
	}
#endif

#if !defined(__PICO2__) && !defined(ESP32)
	if (screenLCD)
	{
		unsigned numberOfImages = GetNumberOfImages();
		unsigned numberOfDisplayedImages = (screenLCD->Height()/screenLCD->GetFontHeight())-1;
		unsigned caddyIndex;

		RGBA BkColour = RGBA(0, 0, 0, 0xFF);
		//screenLCD->Clear(BkColour);
		x = 0;
		y = 0;

		snprintf(buffer, 256, "D%02d D%d/%d %c %s"
			, deviceID
			, index + 1
			, numberOfImages
			, GetImage(index)->GetReadOnly() ? 'R' : ' '
			, roms ? (image->IsD81() ? roms->ROMName1581 : roms->GetSelectedROMName()) : ""
			);
		screenLCD->PrintText(false, x, y, buffer, 0, RGBA(0xff, 0xff, 0xff, 0xff));
		y += screenLCD->GetFontHeight();

		if (numberOfImages > numberOfDisplayedImages && index > numberOfDisplayedImages-1)
		{
			if (numberOfImages - index < numberOfDisplayedImages)
				caddyIndex = numberOfImages - numberOfDisplayedImages;
			else
				caddyIndex = index;
		}
		else
		{
			caddyIndex = 0;
		}

		for (; caddyIndex < numberOfImages; ++caddyIndex)
		{
			DiskImage* image = GetImage(caddyIndex);
			if (image)
			{
				const char* name = image->GetName();
				if (name)
				{
					memset(buffer, ' ', screenLCD->Width() / screenLCD->GetFontWidth());
					screenLCD->PrintText(false, x, y, buffer, BkColour, BkColour);
					snprintf(buffer, 256, "%d %s", caddyIndex + 1, name);
					screenLCD->PrintText(false, x, y, buffer, 0, caddyIndex == index ? RGBA(0xff, 0xff, 0xff, 0xff) : BkColour);
					y += screenLCD->GetFontHeight();
				}
				if (y >= screenLCD->Height())
					break;
			}
		}
		while (y < screenLCD->Height()) {
			memset(buffer, ' ',  screenLCD->Width()/screenLCD->GetFontWidth());
			screenLCD->PrintText(false, x, y, buffer, BkColour, BkColour);
			y += screenLCD->GetFontHeight();
		}
		screenLCD->SwapBuffers();
	}
#endif
}

bool DiskCaddy::Update()
{
	u32 y;
	u32 x;
	u32 caddyIndex = GetSelectedIndex();
	if (caddyIndex != oldCaddyIndex)
	{
#if not defined(EXPERIMENTALZERO)
		if (screen)
		{
			x = screen->ScaleX(screenPosXCaddySelections);
			y = screen->ScaleY(screenPosYCaddySelections) + 16 + 16 * oldCaddyIndex;
			DiskImage* image = GetImage(oldCaddyIndex);
			if (image)
			{
				const char* name = image->GetName();
				if (name)
				{
					snprintf(buffer, 256, "                                                        ");
					screen->PrintText(false, x, y, buffer, grey, greyDark);
					snprintf(buffer, 256, "  %d %s", oldCaddyIndex + 1, name);
					screen->PrintText(false, x, y, buffer, grey, greyDark);
				}
			}
		}
#endif

		oldCaddyIndex = caddyIndex;
		ShowSelectedImage(oldCaddyIndex);
#if !defined(__PICO2__)
		if (screenLCD)
		{
			
		}
#endif
		return true;
	}
	return false;
}

// service/service.cpp - Pi Zero W "service mode" implementation
//
// S1 goal: cold-bootable service kernel that can:
// - mount SD (done by kernel init)
// - load options.txt
// - initialize OLED (SSD1306) if enabled
// - show a banner and idle deterministically
//
// Networking and HTTP endpoints come in later steps.

#include "service.h"

#include <fatfs/ff.h>
#include <stdio.h>
#include <string.h>

#include "kernel.h"
#include "options.h"
#include "ScreenLCD.h"

// Globals referenced by existing OLED/font rendering code.
unsigned char *CBMFont = nullptr;

// Service-mode globals. Keep them local to the service kernel build.
static ScreenLCD *g_screenLCD = nullptr;
static Options g_options;

static void ServiceShow2(const char *line0, const char *line1)
{
	if (!g_screenLCD)
	{
		return;
	}

	g_screenLCD->Clear(0);

	const u32 fh = g_screenLCD->GetFontHeight();
	if (line0)
	{
		g_screenLCD->PrintText(false, 0, 0, (char *) line0);
	}
	if (line1)
	{
		g_screenLCD->PrintText(false, 0, fh, (char *) line1);
	}

	g_screenLCD->RefreshScreen();
}

static void ServiceLoadOptions(void)
{
	FIL fp;
	if (f_open(&fp, "SD:/options.txt", FA_READ) != FR_OK)
	{
		Kernel.log("service: options.txt not found (defaults)");
		return;
	}

	static unsigned char buf[16 * 1024];
	UINT bytesRead = 0;
	memset(buf, 0, sizeof(buf));

	(void) f_read(&fp, buf, sizeof(buf) - 1, &bytesRead);
	f_close(&fp);

	g_options.Process((char *) buf);
	Kernel.log("service: options loaded");
}

static void ServiceLoadFontROM(void)
{
	const char *fontName = g_options.GetRomFontName();
	if (!fontName)
	{
		return;
	}

	// Match legacy behavior: allow absolute path, or look under /roms/.
	char altPath[256] = "/roms/";
	if (fontName[0] != '/')
	{
		strncat(altPath, fontName, sizeof(altPath) - strlen(altPath) - 1);
	}
	else
	{
		altPath[0] = '\0';
	}

	FIL fp;
	if ((FR_OK != f_open(&fp, fontName, FA_READ)) &&
	    (altPath[0] == '\0' || FR_OK != f_open(&fp, altPath, FA_READ)))
	{
		Kernel.log("service: font ROM not found");
		return;
	}

	static unsigned char fontBuf[4096];
	UINT bytesRead = 0;
	(void) f_read(&fp, fontBuf, sizeof(fontBuf), &bytesRead);
	f_close(&fp);

	if (bytesRead == sizeof(fontBuf))
	{
		CBMFont = fontBuf;
		Kernel.log("service: font ROM loaded");
	}
	else
	{
		Kernel.log("service: font ROM wrong size (%u)", (unsigned) bytesRead);
	}
}

static void ServiceEnsureLCD(void)
{
	if (g_screenLCD)
	{
		return;
	}

	LCD_MODEL model = g_options.I2CLcdModel();
	if (!model)
	{
		Kernel.log("service: LCD disabled by options");
		return;
	}

	int width = 128;
	int height = 64;
	if (model == LCD_1306_128x32)
	{
		height = 32;
	}
	if (model == LCD_1107_128x128)
	{
		height = 128;
	}

	g_screenLCD = new ScreenLCD();
	g_screenLCD->Open(width, height, 1,
			  g_options.I2CBusMaster(),
			  g_options.I2CLcdAddress(),
			  g_options.I2CLcdFlip(),
			  model,
			  g_options.I2cLcdUseCBMChar());
	g_screenLCD->SetContrast(g_options.I2CLcdOnContrast());
	g_screenLCD->ClearInit(0);
	g_screenLCD->Clear(RGBA(0, 0, 0, 0xFF));
	g_screenLCD->RefreshScreen();
}

void service_init(void)
{
	Kernel.log("service: init");
	ServiceLoadOptions();
	ServiceLoadFontROM();
	ServiceEnsureLCD();
	ServiceShow2("MINI SERVICE", "READY");
}

void service_run(void)
{
	Kernel.log("service: run");
	ServiceEnsureLCD();
	ServiceShow2("MINI SERVICE", "IDLE");

	for (;;)
	{
		Kernel.get_timer()->MsDelay(250);
	}
}


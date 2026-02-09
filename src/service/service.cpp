// service/service.cpp - service-kernel app: options + OLED status.
//
// See docs/service-http.md for the HTTP control plane contract.
//
// Uses the existing SSD1306/ScreenLCD + CBM font ROM rendering path.

#include "service.h"

#include <circle/string.h>
#include <fatfs/ff.h>
#include <stdio.h>
#include <string.h>

#include "http_server.h"
#include "kernel.h"
#include "options.h"
#include "ScreenLCD.h"

// Globals referenced by existing OLED/font rendering code.
unsigned char *CBMFont = nullptr;

// Service-mode globals. Keep them local to the service kernel build.
static ScreenLCD *g_screenLCD = nullptr;
static CServiceHttpServer *g_http_server = nullptr;
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

	ServiceShow2("MINI SERVICE", "WLAN INIT");
	if (!Kernel.wifi_start())
	{
		ServiceShow2("MINI SERVICE", "WLAN FAIL");
		return;
	}

	// Association can take a bit. For S3 we only prove the SDIO/WLAN path is stable.
	// DHCP/IP display comes in the next slice.
	ServiceShow2("MINI SERVICE", "JOINING");
	for (unsigned i = 0; i < 150; ++i) // ~15s
	{
		if (Kernel.wifi_is_connected())
		{
			break;
		}
		Kernel.get_scheduler()->MsSleep(100);
	}

	if (!Kernel.wifi_is_connected())
	{
		ServiceShow2("MINI SERVICE", "NO LINK");
		return;
	}

	// Wait for DHCP (net subsystem running) and then show the IP.
	CNetSubSystem *net = Kernel.get_net();
	if (!net)
	{
		ServiceShow2("MINI SERVICE", "NO NET");
		return;
	}

	ServiceShow2("MINI SERVICE", "DHCP");
	for (unsigned i = 0; i < 150; ++i) // ~15s
	{
		if (net->IsRunning())
		{
			break;
		}
		Kernel.get_scheduler()->MsSleep(100);
	}

	CString ip;
	net->GetConfig()->GetIPAddress()->Format(&ip);
	ServiceShow2("MINI SERVICE", (const char *) ip);

	// HTTP control plane.
	if (!g_http_server)
	{
		g_http_server = new CServiceHttpServer(net);
		if (!g_http_server)
		{
			Kernel.log("service: http server alloc failed");
		}
	}
}

bool service_run(void)
{
	Kernel.log("service: run");
	ServiceEnsureLCD();

	for (;;)
	{
		if (CServiceHttpServer::IsTeardownRequested())
		{
			// Stop accepting new HTTP connections, clear the OLED, then give the
			// commit response a short moment to flush before reboot.
			if (g_http_server)
			{
				g_http_server->RequestStop();
			}

			// Clear the OLED right before reboot so we don't leave stale service UI.
			if (g_screenLCD)
			{
				g_screenLCD->Clear(0);
				g_screenLCD->RefreshScreen();
			}

			Kernel.get_scheduler()->MsSleep(150);
			return true;
		}

		Kernel.get_scheduler()->MsSleep(50);
	}

	return false;
}

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

static void ServiceDrawReadyScreen(const char *ip_line)
{
	if (!g_screenLCD)
	{
		return;
	}

	RGBA bk = RGBA(0, 0, 0, 0xFF);
	const u32 fh = g_screenLCD->GetFontHeight();

	// Match the old working layout:
	//   MINI SERVICE
	//   IP: ...
	//   PORT: <port>   (only if the LCD has room for 3 rows at this font height)
	char portLine[32];
	snprintf(portLine, sizeof(portLine), "PORT: %u", static_cast<unsigned>(kServiceHttpPort));

	g_screenLCD->Clear(bk);
	g_screenLCD->PrintText(false, 0, 0 * fh, (char *) "MINI SERVICE", 0, bk);
	if (ip_line)
	{
		g_screenLCD->PrintText(false, 0, 1 * fh, (char *) ip_line, 0, bk);
	}
	if (g_screenLCD->Height() >= fh * 3)
	{
		g_screenLCD->PrintText(false, 0, 2 * fh, portLine, 0, bk);
	}
	g_screenLCD->SwapBuffers();
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

	// Show READY-style screen while associating and waiting for DHCP.
	ServiceDrawReadyScreen("IP: (joining)");
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

	// Give DHCP time to come up.
	ServiceDrawReadyScreen("IP: (joining)");
	for (unsigned i = 0; i < 150; ++i) // ~15s
	{
		if (net->IsRunning() && !net->GetConfig()->GetIPAddress()->IsNull())
		{
			break;
		}
		Kernel.get_scheduler()->MsSleep(100);
	}

	if (!net->IsRunning() || net->GetConfig()->GetIPAddress()->IsNull())
	{
		ServiceShow2("MINI SERVICE", "NO DHCP");
		return;
	}

	// Now that we have an address, show it.
	CString ip;
	net->GetConfig()->GetIPAddress()->Format(&ip);
	// Omit the "IP:" prefix so IPv4 fits on one OLED line.
	ServiceDrawReadyScreen((const char *) ip);

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

const Options *service_options(void)
{
	return &g_options;
}

bool service_run(void)
{
	Kernel.log("service: run");
	ServiceEnsureLCD();
	bool shownJoining = false;
	bool shownReady = false;
	char shownIp[32] = {0};

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

		CNetSubSystem *net = Kernel.get_net();
		const bool linkUp = Kernel.wifi_is_connected();
		const bool dhcpReady = net && net->IsRunning() && !net->GetConfig()->GetIPAddress()->IsNull();

		if (!linkUp || !dhcpReady)
		{
			if (!shownJoining)
			{
				ServiceDrawReadyScreen("IP: (joining)");
				shownJoining = true;
				shownReady = false;
				shownIp[0] = '\0';
			}
		}
		else
		{
			CString ip;
			net->GetConfig()->GetIPAddress()->Format(&ip);
			const char *ipText = (const char *) ip;
			if (!shownReady || strcmp(shownIp, ipText) != 0)
			{
				ServiceDrawReadyScreen(ipText);
				strncpy(shownIp, ipText, sizeof(shownIp) - 1);
				shownIp[sizeof(shownIp) - 1] = '\0';
				shownReady = true;
				shownJoining = false;
			}

			// If service_init exited before DHCP came up, start the server now.
			if (!g_http_server)
			{
				g_http_server = new CServiceHttpServer(net);
				if (!g_http_server)
				{
					Kernel.log("service: http server alloc failed");
				}
			}
		}

		Kernel.get_scheduler()->MsSleep(50);
	}

	return false;
}

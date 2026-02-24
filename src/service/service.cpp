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

static const char kNetStatusPath[] = "SD:/wpa_net_status_log.txt";
static unsigned g_net_attempt = 0;
static unsigned g_boot_err_show_ms = 0;

static void ServiceNetStatusWrite(const char *code, const char *detail)
{
	if (!code || !code[0])
	{
		return;
	}

	FIL fp;
	if (f_open(&fp, kNetStatusPath, FA_OPEN_ALWAYS | FA_WRITE) != FR_OK)
	{
		return;
	}

	(void) f_lseek(&fp, f_size(&fp));

	char line[256];
	if (detail && detail[0])
	{
		snprintf(line, sizeof(line), "code=%s attempt=%u ticks=%u %s\r\n",
			 code, g_net_attempt, (unsigned) CTimer::GetClockTicks(), detail);
	}
	else
	{
		snprintf(line, sizeof(line), "code=%s attempt=%u ticks=%u\r\n",
			 code, g_net_attempt, (unsigned) CTimer::GetClockTicks());
	}

	UINT bw = 0;
	(void) f_write(&fp, line, (UINT) strlen(line), &bw);
	(void) f_sync(&fp);
	f_close(&fp);
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

static void ServiceDrawBootHandoffSplash(void)
{
	if (!g_screenLCD)
	{
		return;
	}

	// Match the legacy OLED fallback splash placement from src/main.cpp.
	char splash[32];
	snprintf(splash, sizeof(splash), "Pi1541 V1.25");

	const int fw = (int) g_screenLCD->GetFontWidth();
	const int fh = (int) g_screenLCD->GetFontHeight();
	int x = (g_screenLCD->Width() - (fw * (int) strlen(splash))) / 2;
	int y = (g_screenLCD->Height() - fh) / 2;
	if (x < 0)
	{
		x = 0;
	}
	if (y < 0)
	{
		y = 0;
	}

	g_screenLCD->Clear(0);
	g_screenLCD->PrintText(false, (u32) x, (u32) y, splash, 0x0);
	g_screenLCD->RefreshScreen();
}

static void ServiceBlinkScreen(void)
{
	if (!g_screenLCD)
	{
		return;
	}

	g_screenLCD->Clear(0);
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
	++g_net_attempt;
	if (!Kernel.wifi_start())
	{
		ServiceNetStatusWrite("WIFI_START_FAIL", "stage=init");
		ServiceEnsureLCD();
		ServiceDrawReadyScreen("ERR: WIFI_START_FAIL");
		g_boot_err_show_ms = 1500;
		return;
	}

	// Do not block service boot on link/DHCP; service_run() handles async readiness.
	ServiceEnsureLCD();
	ServiceDrawReadyScreen("IP: (joining)");
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
	bool shownHttpFail = false;
	bool shownRetry = false;
	unsigned retryBannerMs = 0;
	char shownIp[32] = {0};
	char lastNetErr[32] = {0};

	// If we don't get link+IP, periodically restart the Wi-Fi stack so service
	// mode can recover from flaky association / DHCP behavior.
	static constexpr unsigned kNetPollMs = 50;
	static constexpr unsigned kNetRetryMs = 30 * 1000;
	static constexpr unsigned kAssocFailMs = 15 * 1000;
	static constexpr unsigned kDhcpFailMs = 15 * 1000;
	static constexpr unsigned kErrShowMs = 1500;
	static constexpr unsigned kMaxLoggedRetries = 30;
	unsigned assocWaitMs = 0;
	unsigned dhcpWaitMs = 0;
	unsigned errShowMs = g_boot_err_show_ms;
	const bool useDhcp = g_options.GetDHCP();
	unsigned retryCount = 0;
	bool allowStatusLog = true;
	g_boot_err_show_ms = 0;

	auto SetNetErr = [&](const char *code, const char *detail) {
		if (!code || !code[0])
		{
			return;
		}

		if (strncmp(lastNetErr, code, sizeof(lastNetErr) - 1) == 0)
		{
			return;
		}

		snprintf(lastNetErr, sizeof(lastNetErr), "%s", code);
		if (allowStatusLog)
		{
			ServiceNetStatusWrite(code, detail);
		}

		if (g_screenLCD)
		{
			ServiceBlinkScreen();
			ServiceDrawReadyScreen(code);
			errShowMs = kErrShowMs;
			shownJoining = false;
			shownReady = false;
			shownHttpFail = false;
			shownRetry = false;
			retryBannerMs = 0;
			shownIp[0] = '\0';
		}
	};

	for (;;)
	{
		if (CServiceHttpServer::IsTeardownRequested())
		{
			// Stop accepting new HTTP connections, draw handoff splash, then give
			// the commit response a short moment to flush before reboot.
			if (g_http_server)
			{
				g_http_server->RequestStop();
			}

			ServiceDrawBootHandoffSplash();

			Kernel.get_scheduler()->MsSleep(150);
			return true;
		}

		CNetSubSystem *net = Kernel.get_net();
		const bool linkUp = Kernel.wifi_is_connected();
		const bool netRunning = net && net->IsRunning();
		const bool ipReady = netRunning && !net->GetConfig()->GetIPAddress()->IsNull();

		// Start HTTP only after both link and IP are ready.
		if (!g_http_server && linkUp && ipReady)
		{
			g_http_server = new CServiceHttpServer(net);
			if (!g_http_server)
			{
				Kernel.log("service: http server alloc failed");
				if (!shownHttpFail)
				{
					ServiceDrawReadyScreen("IP: (http fail)");
					shownHttpFail = true;
					shownReady = false;
					shownJoining = false;
					shownIp[0] = '\0';
				}
				Kernel.get_scheduler()->MsSleep(kNetPollMs);
				continue;
			}
		}

		if (!linkUp || !ipReady)
		{
			const bool inErrFlash = errShowMs != 0;
			const bool inBackoff = shownRetry && retryBannerMs != 0;

			// Only measure/judge association/DHCP while we are actively "joining".
			// During error flash/backoff we keep the UI stable and wait.
			if (!inErrFlash && !inBackoff)
			{
				if (!linkUp)
				{
					assocWaitMs += kNetPollMs;
					dhcpWaitMs = 0;
					if (assocWaitMs >= kAssocFailMs)
					{
						SetNetErr("WIFI_ASSOC_FAIL", "stage=assoc");
					}
				}
				else if (!ipReady)
				{
					assocWaitMs = 0;
					if (useDhcp)
					{
						dhcpWaitMs += kNetPollMs;
						if (dhcpWaitMs >= kDhcpFailMs)
						{
							SetNetErr("DHCP_TIMEOUT", "stage=dhcp");
						}
					}
				}
			}

			if (errShowMs)
			{
				if (errShowMs <= kNetPollMs)
				{
					errShowMs = 0;
				}
				else
				{
					errShowMs -= kNetPollMs;
				}
			}
			else if (shownRetry && retryBannerMs)
			{
				if (retryBannerMs <= kNetPollMs)
				{
					retryBannerMs = 0;
					shownRetry = false;
					lastNetErr[0] = '\0';
					assocWaitMs = 0;
					dhcpWaitMs = 0;
				}
				else
				{
					retryBannerMs -= kNetPollMs;
				}
			}
			else if (!shownJoining)
			{
				if (!g_http_server && lastNetErr[0] && !shownRetry)
				{
					++g_net_attempt;
					++retryCount;
					if (allowStatusLog)
					{
						ServiceNetStatusWrite("WIFI_RETRY", "stage=timer");
					}
					if (allowStatusLog && retryCount > kMaxLoggedRetries)
					{
						ServiceNetStatusWrite("LOG_SUPPRESSED", "max_retries=30");
						allowStatusLog = false;
					}

					char retryLine[32];
					const unsigned retrySeconds = (kNetRetryMs + 999) / 1000;
					snprintf(retryLine, sizeof(retryLine), "ERR: RETRY %uS", retrySeconds);
					ServiceBlinkScreen();
					ServiceDrawReadyScreen(retryLine);
					shownRetry = true;
					retryBannerMs = kNetRetryMs;
					shownJoining = false;
					shownReady = false;
					shownHttpFail = false;
					shownIp[0] = '\0';
					assocWaitMs = 0;
					dhcpWaitMs = 0;
				}
				else if (!shownRetry || retryBannerMs == 0)
				{
					ServiceDrawReadyScreen("IP: (joining)");
					shownJoining = true;
					shownReady = false;
					shownIp[0] = '\0';
				}
			}
		}
		else
		{
			assocWaitMs = 0;
			dhcpWaitMs = 0;

			shownHttpFail = false;
			shownRetry = false;

			CString ip;
			net->GetConfig()->GetIPAddress()->Format(&ip);
			const char *ipText = (const char *) ip;
			if (!shownReady || strcmp(shownIp, ipText) != 0)
			{
				char ipLine[40];
				const bool usePrefix = strlen(ipText) <= 12;
				snprintf(ipLine, sizeof(ipLine), usePrefix ? "IP: %s" : "%s", ipText);
				ServiceDrawReadyScreen(ipLine);
				strncpy(shownIp, ipText, sizeof(shownIp) - 1);
				shownIp[sizeof(shownIp) - 1] = '\0';
				shownReady = true;
				shownJoining = false;
			}
		}

		Kernel.get_scheduler()->Yield();
		Kernel.get_scheduler()->MsSleep(kNetPollMs);
	}

	return false;
}

// service/kernel.cpp - minimal Circle kernel implementation
//
// Provenance:
// - Initialization sequence derived from pottendo’s src/circle-kernel.cpp (Circle kernel bring-up).
// - Refactored into a standalone service-kernel class and app boundary (service_init/service_run).

#include "kernel.h"

#include <circle/startup.h>

#include <stdarg.h>
#include <stdio.h>

#include "service.h"
#include "shim.h"
#include "options.h"

static bool IsZeroIPv4(const u8 a[4])
{
	return a[0] == 0 && a[1] == 0 && a[2] == 0 && a[3] == 0;
}

#define _DRIVE "SD:"
#define _FIRMWARE_PATH _DRIVE "/firmware/"
#define _CONFIG_FILE _DRIVE "/wpa_supplicant.conf"

CServiceKernel::CServiceKernel(void)
	: m_Timer(&m_Interrupt),
	  m_Logger(m_Options.GetLogLevel(), &m_Timer),
	  m_EMMC(&m_Interrupt, &m_Timer, &m_ActLED),
	  m_WLAN(_FIRMWARE_PATH),
	  m_WPASupplicant(_CONFIG_FILE)
{
}

void CServiceKernel::log(const char *fmt, ...)
{
	char buf[512];

	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	m_Logger.Write("service", LogNotice, "%s", buf);
}

boolean CServiceKernel::Initialize(void)
{
	// Serial is our fallback logging target until the OLED is up.
	const boolean serialOK = m_Serial.Initialize(115200);

	CDevice *pTarget = serialOK ? static_cast<CDevice *>(&m_Serial) : nullptr;
	m_Logger.Initialize(pTarget);

	boolean ok = TRUE;

	if (!(ok = m_Interrupt.Initialize()))
	{
		m_Logger.Write("service", LogError, "Interrupt init failed");
		return FALSE;
	}
	if (!(ok = m_Timer.Initialize()))
	{
		m_Logger.Write("service", LogError, "Timer init failed");
		return FALSE;
	}

	// SD + FAT (needed for options, uploads, and file operations in service mode).
	if (!(ok = m_EMMC.Initialize()))
	{
		m_Logger.Write("service", LogError, "EMMC init failed");
		return FALSE;
	}

	if (f_mount(&m_FileSystem, _DRIVE, 1) != FR_OK)
	{
		m_Logger.Write("service", LogError, "Cannot mount drive: %s", _DRIVE);
		return FALSE;
	}

	m_Logger.Write("service", LogNotice, "mounted drive: %s", _DRIVE);
	return TRUE;
}

boolean CServiceKernel::wifi_start(void)
{
	if (m_Net)
	{
		m_Logger.Write("service", LogNotice, "wifi: cleaning up net stack");
		delete m_Net;
		m_Net = nullptr;
	}

	const Options *opt = service_options();
	const bool use_dhcp = !opt || opt->GetDHCP();

	const u8 *ip = service_static_ip();
	const u8 *nm = service_static_nm();
	const u8 *gw = service_static_gw();
	const u8 *dns = service_static_dns();

	if (!use_dhcp)
	{
		if (IsZeroIPv4(ip) || IsZeroIPv4(nm))
		{
			m_Logger.Write("service", LogError, "wifi: static IPv4 enabled but missing IPAdress/NetMask");
			// Keep hostname disabled in this variant to match the old working path.
			m_Net = new CNetSubSystem(0, 0, 0, 0, nullptr, NetDeviceTypeWLAN);
		}
		else
		{
			m_Logger.Write("service", LogNotice, "wifi: using static IPv4");
			m_Net = new CNetSubSystem(const_cast<u8 *>(ip),
						  const_cast<u8 *>(nm),
						  const_cast<u8 *>(gw),
						  const_cast<u8 *>(dns),
						  nullptr,
						  NetDeviceTypeWLAN);
		}
	}
	else
	{
		m_Net = new CNetSubSystem(0, 0, 0, 0, nullptr, NetDeviceTypeWLAN);
	}

	if (!m_Net)
	{
		m_Logger.Write("service", LogError, "wifi: net subsystem alloc failed");
		return FALSE;
	}

	boolean ok = TRUE;
	if (ok) ok = m_WLAN.Initialize();
	if (ok) ok = m_Net->Initialize(FALSE);
	if (ok) ok = m_WPASupplicant.Initialize();

	if (!ok)
	{
		m_Logger.Write("service", LogError, "wifi: bringup failed");
	}

	return ok;
}

TShutdownMode CServiceKernel::Run(void)
{
	service_init();
	const bool want_reboot = service_run();

	return want_reboot ? ShutdownReboot : ShutdownHalt;
}

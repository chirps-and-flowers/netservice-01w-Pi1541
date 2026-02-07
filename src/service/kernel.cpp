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

#define _DRIVE "SD:"

CServiceKernel::CServiceKernel(void)
	: m_Timer(&m_Interrupt),
	  m_Logger(m_Options.GetLogLevel(), &m_Timer),
	  m_EMMC(&m_Interrupt, &m_Timer, &m_ActLED)
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

TShutdownMode CServiceKernel::Run(void)
{
	service_init();
	service_run();

	// service_run is expected to loop forever.
	return ShutdownHalt;
}

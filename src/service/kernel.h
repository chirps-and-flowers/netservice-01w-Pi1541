// service/kernel.h - minimal Circle kernel for Pi Zero W.
// This is a standalone kernel image used for the network service image only.
// It intentionally does not link the Pi1541 emulator core objects (Drive/IEC/CPU/etc).
//
// Provenance:
// - Initialization sequence derived from pottendo’s src/circle-kernel.cpp (Circle kernel bring-up).
// - Refactored into a standalone service-kernel class and app boundary (service_init/service_run).

#ifndef NETSERVICE_KERNEL_SERVICE_H
#define NETSERVICE_KERNEL_SERVICE_H

#include <circle/actled.h>
#include <circle/devicenameservice.h>
#include <circle/exceptionhandler.h>
#include <circle/interrupt.h>
#include <circle/koptions.h>
#include <circle/logger.h>
#include <circle/net/netsubsystem.h>
#include <circle/sched/scheduler.h>
#include <circle/memory.h>
#include <circle/serial.h>
#include <circle/timer.h>
#include <circle/types.h>

#include <SDCard/emmc.h>
#include <fatfs/ff.h>
#include <wlan/bcm4343.h>
#include <wlan/hostap/wpa_supplicant/wpasupplicant.h>

enum TShutdownMode
{
	ShutdownNone,
	ShutdownHalt,
	ShutdownReboot
};

class CServiceKernel
{
public:
	CServiceKernel(void);

	boolean Initialize(void);
	TShutdownMode Run(void);
	int Cleanup(void) { return 0; }

	// Minimal surface needed by existing code (debug.h, service app).
	CTimer *get_timer(void) { return &m_Timer; }
	CScheduler *get_scheduler(void) { return &m_Scheduler; }
	void log(const char *fmt, ...);

	// Service networking (Wi-Fi + NetSubSystem + wpa_supplicant).
	boolean wifi_start(void);
	boolean wifi_is_connected(void) const { return m_WPASupplicant.IsConnected(); }

private:
	CMemorySystem m_Memory;
	CActLED m_ActLED;
	CKernelOptions m_Options;
	CDeviceNameService m_DeviceNameService;
	CSerialDevice m_Serial;
	CExceptionHandler m_ExceptionHandler;
	CInterruptSystem m_Interrupt;
	CTimer m_Timer;
	CScheduler m_Scheduler;
	CLogger m_Logger;

	CEMMCDevice m_EMMC;
	FATFS m_FileSystem;

	CBcm4343Device m_WLAN;
	CNetSubSystem *m_Net = nullptr;
	CWPASupplicant m_WPASupplicant;
};

// Global kernel instance used by various utility macros.
extern CServiceKernel Kernel;

#endif

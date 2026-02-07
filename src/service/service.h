// service/service.h - service-kernel "application" entrypoints
//
// This code is linked into the Circle "service" kernel only (not the emulator
// kernel). It owns the service-mode state machine (network bringup + control
// plane) and uses a narrow platform surface provided by the service kernel.

#ifndef NETSERVICE_SERVICE_APP_H
#define NETSERVICE_SERVICE_APP_H

// Initialize service-mode state (options, display, etc).
void service_init(void);

// Run the service loop (does not normally return).
void service_run(void);

#endif


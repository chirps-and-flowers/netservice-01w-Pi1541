// service/service.h - service-kernel app entrypoints.
//
// See docs/service-http.md for the HTTP control plane contract.

#ifndef NETSERVICE_SERVICE_APP_H
#define NETSERVICE_SERVICE_APP_H

class Options;

// Initialize service-mode state (options, display, etc).
void service_init(void);

// Run the service loop. Returns true if a reboot back to the emulator kernel
// was requested.
bool service_run(void);

// Access the parsed options.txt for service-only helpers.
const Options *service_options(void);

#endif

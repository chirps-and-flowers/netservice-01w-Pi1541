// service/main.cpp - Circle service-kernel entrypoint

#include "kernel.h"

#include <circle/startup.h>

// Global kernel instance used by various utility macros.
CServiceKernel Kernel;

int main(void)
{
	if (!Kernel.Initialize())
	{
		halt();
		return EXIT_HALT;
	}

	const TShutdownMode mode = Kernel.Run();
	Kernel.Cleanup();

	switch (mode)
	{
	case ShutdownReboot:
		reboot();
		return EXIT_REBOOT;
	case ShutdownHalt:
	default:
		halt();
		return EXIT_HALT;
	}
}


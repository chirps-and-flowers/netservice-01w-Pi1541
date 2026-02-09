#ifndef _CHAINBOOT_LEGACY_H
#define _CHAINBOOT_LEGACY_H

#ifdef __cplusplus
extern "C" {
#endif

// Loads the specified kernel file (e.g. "kernel_loader.img") into memory 
// and jumps to it, overwriting the current kernel.
// Does not return.
void ChainBootLegacy(const char* kernelName);

#ifdef __cplusplus
}
#endif

#endif

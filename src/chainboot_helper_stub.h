// chainboot_helper_stub.h
//
// Legacy emulator -> service transition:
// Load the minimal chainboot chainloader kernel from SD into RAM and jump to it.
// The chainloader then loads kernel_srv.* into 0x8000 and transfers control.

#ifndef CHAINBOOT_HELPER_STUB_H
#define CHAINBOOT_HELPER_STUB_H

#ifdef __cplusplus
extern "C" {
#endif

void ChainBootChainloader(const char* chainloaderName);

#ifdef __cplusplus
}
#endif

#endif

#ifndef __circle_types_h__
#define __circle_types_h__

#include <circle/types.h>

// Historically, lots of code pulls in the Circle kernel header via `types.h`.
// Keep that pattern, but select the correct kernel header per build target.
#if defined(NETSERVICE_TARGET_SERVICE)
#include "service/kernel.h"
#include "service/shim.h"
#else
#include "circle-kernel.h"
#endif
typedef unsigned char		u8;
typedef unsigned short		u16;
typedef unsigned int		u32;
typedef int TUSBKeyboardDevice;
//typedef char                s8;
//typedef short               s16;
//typedef int                 s32;
#if (AARCH == 64)
typedef unsigned long KTHType;
typedef long unsigned int   u64;
#else
typedef unsigned KTHType;
#endif
#endif

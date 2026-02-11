// Compile-time feature switches for optional UI/image functionality.
// Defaults are conservative for Pi Zero (EXPERIMENTALZERO builds).
#pragma once

#ifndef PI1541_ENABLE_STBI
#if defined(EXPERIMENTALZERO)
#define PI1541_ENABLE_STBI 0
#else
#define PI1541_ENABLE_STBI 1
#endif
#endif

#ifndef PI1541_ENABLE_OLED_BOOTLOGO
#if defined(EXPERIMENTALZERO)
#define PI1541_ENABLE_OLED_BOOTLOGO 0
#else
#define PI1541_ENABLE_OLED_BOOTLOGO 1
#endif
#endif

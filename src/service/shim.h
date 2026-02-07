// service/shim.h - minimal legacy compatibility layer for the service kernel
//
// This file provides a small compatibility surface so the service kernel can
// reuse existing Pi1541 support code without linking the emulator core.
//
// Provenance:
// - RPI_I2C* wrappers exist to support the existing SSD1306 code paths.
// - setIP/setNM/setGW/setDNS exist because options.cpp calls them under __CIRCLE__.
// - VolumeStr exists because Circle's FatFs port expects it.

#ifndef NETSERVICE_SHIM_SERVICE_H
#define NETSERVICE_SHIM_SERVICE_H

// I2C wrappers (Circle).
#define RPI_I2CInit i2c_init
#define RPI_I2CSetClock i2c_setclock
#define RPI_I2CRead i2c_read
#define RPI_I2CWrite i2c_write

void i2c_init(int BSCMaster, int fast);
void i2c_setclock(int BSCMaster, int clock_freq);
int i2c_read(int BSCMaster, unsigned char slaveAddress, void *buffer, unsigned count);
int i2c_write(int BSCMaster, unsigned char slaveAddress, void *buffer, unsigned count);

// Network config hooks (used by options parsing). Service networking will
// consume these later; for now they simply store parsed values.
void setIP(const char *ip);
void setNM(const char *nm);
void setGW(const char *gw);
void setDNS(const char *dns);

#endif

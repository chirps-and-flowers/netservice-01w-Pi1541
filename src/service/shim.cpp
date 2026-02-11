// service/shim.cpp - shims required for the standalone service kernel
//
// Provenance:
// - RPI_I2C* wrappers exist to support the existing SSD1306 code paths.
// - setIP/setNM/setGW/setDNS exist because options.cpp calls them under __CIRCLE__.
// - VolumeStr exists because Circle's FatFs port expects it.

#include "shim.h"

#include <circle/i2cmaster.h>
#include <circle/types.h>

#include <fatfs/ff.h>

#include <stdio.h>

// -----------------------------------------------------------------------------
// I2C wrappers (Circle-backed)

static CI2CMaster *g_i2c[2] = {nullptr, nullptr};

static CI2CMaster *GetMaster(int BSCMaster)
{
	if (BSCMaster < 0 || BSCMaster > 1)
	{
		return nullptr;
	}

	return g_i2c[BSCMaster];
}

void i2c_init(int BSCMaster, int fast)
{
	if (BSCMaster < 0 || BSCMaster > 1)
	{
		return;
	}

	if (!g_i2c[BSCMaster])
	{
		g_i2c[BSCMaster] = new CI2CMaster((unsigned) BSCMaster, fast ? TRUE : FALSE);
	}

	(void) g_i2c[BSCMaster]->Initialize();
}

void i2c_setclock(int BSCMaster, int clock_freq)
{
	CI2CMaster *m = GetMaster(BSCMaster);
	if (!m)
	{
		return;
	}

	if (clock_freq <= 0)
	{
		return;
	}

	m->SetClock((unsigned) clock_freq);
}

int i2c_read(int BSCMaster, unsigned char slaveAddress, void *buffer, unsigned count)
{
	CI2CMaster *m = GetMaster(BSCMaster);
	if (!m || !buffer || count == 0)
	{
		return -1;
	}

	return m->Read((u8) slaveAddress, buffer, count);
}

int i2c_write(int BSCMaster, unsigned char slaveAddress, void *buffer, unsigned count)
{
	CI2CMaster *m = GetMaster(BSCMaster);
	if (!m || !buffer || count == 0)
	{
		return -1;
	}

	return m->Write((u8) slaveAddress, buffer, count);
}

// -----------------------------------------------------------------------------
// Network config hooks (options parser)

static u8 g_ip[4]  = {0, 0, 0, 0};
static u8 g_nm[4]  = {0, 0, 0, 0};
static u8 g_gw[4]  = {0, 0, 0, 0};
static u8 g_dns[4] = {0, 0, 0, 0};

static void parse_ipv4(const char *s, u8 out[4])
{
	if (!s || !out)
	{
		return;
	}

	unsigned a = 0, b = 0, c = 0, d = 0;
	if (sscanf(s, "%u.%u.%u.%u", &a, &b, &c, &d) != 4)
	{
		return;
	}

	out[0] = (u8) (a & 0xFF);
	out[1] = (u8) (b & 0xFF);
	out[2] = (u8) (c & 0xFF);
	out[3] = (u8) (d & 0xFF);
}

void setIP(const char *ip) { parse_ipv4(ip, g_ip); }
void setNM(const char *nm) { parse_ipv4(nm, g_nm); }
void setGW(const char *gw) { parse_ipv4(gw, g_gw); }
void setDNS(const char *dns) { parse_ipv4(dns, g_dns); }

const u8 *service_static_ip(void) { return g_ip; }
const u8 *service_static_nm(void) { return g_nm; }
const u8 *service_static_gw(void) { return g_gw; }
const u8 *service_static_dns(void) { return g_dns; }

// -----------------------------------------------------------------------------
// FatFs glue required by Circle addon

const char *VolumeStr[FF_VOLUMES] = {"SD", "USB01", "USB02", "USB03", "USB04"};

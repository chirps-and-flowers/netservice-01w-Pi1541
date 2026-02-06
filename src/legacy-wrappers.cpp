//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
// written by pottendo
//

#include <circle/startup.h>
#include "circle-kernel.h"
#include "rpi-gpio.h"
#include <stdint.h>

u32 _ctb;

// newlib's getentropy() expects a platform hook named _getentropy.
// Circle/newlib does not currently provide one, but some consumers (e.g. WPA)
// rely on it existing. Provide a best-effort entropy source.
extern "C" int _getentropy(void *buf, size_t buflen)
{
	u8 *p = static_cast<u8 *>(buf);

	// Seed from timer ticks + pointer jitter; best-effort only.
	u32 x = CTimer::GetClockTicks();
	x ^= static_cast<u32>(reinterpret_cast<uintptr_t>(buf));
	x ^= static_cast<u32>(buflen);

	for (size_t i = 0; i < buflen; ++i)
	{
		// xorshift32
		x ^= x << 13;
		x ^= x >> 17;
		x ^= x << 5;
		p[i] = static_cast<u8>(x);
	}

	return 0;
}

/* wrappers */
void RPiConsole_put_pixel(uint32_t x, uint32_t y, uint16_t c) {	Kernel.set_pixel(x, y, c); }
void SetACTLed(int v) { Kernel.SetACTLed(v); }
void reboot_now(void) { reboot(); }
void i2c_init(int BSCMaster, int fast) { Kernel.i2c_init(BSCMaster, fast); }
void i2c_setclock(int BSCMaster, int clock_freq) { Kernel.i2c_setclock(BSCMaster, clock_freq); }
int i2c_read(int BSCMaster, unsigned char slaveAddress, void* buffer, unsigned count) { return Kernel.i2c_read(BSCMaster, slaveAddress, buffer, count); }
int i2c_write(int BSCMaster, unsigned char slaveAddress, void* buffer, unsigned count) { return Kernel.i2c_write(BSCMaster, slaveAddress, buffer, count); }
int i2c_scan(int BSCMaster, unsigned char slaveAddress) { return Kernel.i2c_scan(BSCMaster, slaveAddress); }
void USPiInitialize(void) 
{
	if (Kernel.usb_updatepnp() == false) 
	{
		Kernel.log("usb update failed");
	}
}
int USPiKeyboardAvailable(void) 
{ 
    int n = Kernel.usb_keyboard_available(); 
    Kernel.log("%s: %d keyboard(s) found", __FUNCTION__, n);
    return n;
}
void USPiKeyboardRegisterKeyStatusHandlerRaw(TKeyStatusHandlerRaw *handler) { Kernel.usb_reghandler(handler); }
TKernelTimerHandle TimerStartKernelTimer(unsigned long nDelay, TKernelTimerHandler *pHandler, void* pParam, void* pContext)
{
	return Kernel.timer_start(nDelay, pHandler, pParam, pContext);
}
void TimerCancelKernelTimer(TKernelTimerHandle hTimer) { Kernel.timer_cancel(hTimer); }
int GetTemperature(unsigned &value) { unsigned ret = CPUThrottle.GetTemperature(); if (ret) value = ret * 1000; return ret; }
int USPiMassStorageDeviceAvailable(void) { return Kernel.usb_massstorage_available(); }

void PlaySoundDMA(int play) { if (play == 0) Kernel.playsound(); }

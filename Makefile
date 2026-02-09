#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

# for Circle build, the RASPPI variable is fetched from Circle lib located at ../circle-stdlib/libs/circle/Config.mk
# -> make sure this circle-stdlib exists at this path, is configured and built
# to build
# 	make
#
# results in
#   kernel8-32.img for Raspberry 3 and Zero 2W
#   kernel7l.img for Raspberry 4

# for Legacy build:
# use RASPPI = 1BRev1 for Raspberry Pi 1B Rev 1 (26 IOports) (GPIO0/1/21)
# use RASPPI = 1BRev2 for Raspberry Pi 1B Rev 2 (26 IOports) (GPIO2/3/27)
# use RASPPI = 1BPlus for Raspberry Pi 1B+ (40 I/OPorts)
# use RASPPI = 0 for Raspberry Pi Zero
# use RASPPI = 3 for Raspberry Pi Zero 2W or Raspberry Pi 3
# use V = 1 optionally for verbose build
# e.g.
# 	make RASPPI=1BPlus V=1 legacy
#
# if you switch from legacy build to circle build 'make clean' is mandatory
#

CIRCLEBASE ?= ../circle-stdlib
CIRCLEHOME ?= $(CIRCLEBASE)/libs/circle

PI1541_CHAINBOOT_ENABLE ?= 1

LEGACY_OBJS = 	armc-start.o armc-cstartup.o armc-cstubs.o armc-cppstubs.o emmc.o ff.o \
			cache.o exception.o performance.o SpinLock.o rpi-interrupts.o Timer.o diskio.o \
			interrupt.o rpi-aux.o  rpi-i2c.o rpi-mailbox-interface.o rpi-mailbox.o rpi-gpio.o

# Minimal chainboot chainloader (loaded by the legacy emulator kernel into RAM,
# then loads kernel_srv.* into 0x8000 and jumps there).
CHAINLOADER_OBJS = armc-start.o armc-cstartup.o armc-cstubs.o armc-cppstubs.o emmc.o ff.o \
			cache.o exception.o performance.o SpinLock.o rpi-interrupts.o Timer.o diskio.o \
			interrupt.o rpi-aux.o rpi-i2c.o rpi-mailbox-interface.o rpi-mailbox.o rpi-gpio.o \
			lz4_legacy.o chainboot_legacy.o chainboot_helper.o
CHAINLOADER_VENDOR_OBJS = vendors/lz4/lz4.o

CIRCLE_OBJS = 	circle-main.o circle-kernel.o webserver.o legacy-wrappers.o logger.o #circle-hmi.o 

COMMON_OBJS = 	main.o Drive.o Pi1541.o DiskImage.o iec_bus.o iec_commands.o m6502.o m6522.o \
		gcr.o prot.o lz.o options.o Screen.o ScreenLCD.o \
		FileBrowser.o DiskCaddy.o ROMs.o InputMappings.o xga_font_data.o \
		m8520.o wd177x.o Pi1581.o Keyboard.o dmRotary.o SSD1306.o
SRCDIR   = src
OBJS_CIRCLE  := $(addprefix $(SRCDIR)/, $(CIRCLE_OBJS) $(COMMON_OBJS))
LEGACY_COLD_OBJS =
ifeq ($(PI1541_CHAINBOOT_ENABLE),1)
LEGACY_COLD_OBJS += chainboot_helper_stub.o
endif
OBJS_LEGACY  := $(addprefix $(SRCDIR)/, $(LEGACY_OBJS) $(COMMON_OBJS) $(LEGACY_COLD_OBJS))
OBJS_CHAINLOADER  := $(addprefix $(SRCDIR)/, $(CHAINLOADER_OBJS)) $(CHAINLOADER_VENDOR_OBJS)

LIBS     = uspi/libuspi.a
INCLUDE  = -Iuspi/include/ -Ivendors/lz4 -I$(CURDIR)

ifeq ($(RASPPI),)
include $(CIRCLEHOME)/Config.mk
ifeq ($(strip $(RASPPI)),1)
$(error RPi1 not supported for Circle builds)
else ifeq ($(strip $(RASPPI)),3)
ifeq ($(strip $(AARCH)),64)
TARGET_CIRCLE ?= kernel8.img
#XFLAGS += -DCIRCLE_GPIO=1
#XFLAGS += -DHEAP_DEBUG
else
TARGET_CIRCLE ?= kernel8-32.img
COMMON_OBJS += SpinLock.o
#XFLAGS += -DCIRCLE_GPIO=1
endif
else ifeq ($(strip $(RASPPI)),4)
ifeq ($(strip $(AARCH)),64)
TARGET_CIRCLE ?= kernel8-rpi4.img
#XFLAGS += -DCIRCLE_GPIO=1
#XFLAGS += -DHEAP_DEBUG
else
TARGET_CIRCLE ?= kernel7l.img
#XFLAGS += -DCIRCLE_GPIO=1
endif
else ifeq ($(strip $(RASPPI)),5)
ifeq ($(strip $(AARCH)),64)
TARGET_CIRCLE ?= kernel_2712.img
XFLAGS += -DCIRCLE_GPIO=1
else
$(error RPi5 supports only 64-bit)
endif
else
$(error Circle build only for RASPPI 3, Zero 2W or 4)
endif
else
include Makefile.rules
endif

ifneq ($(filter chainloader,$(MAKECMDGOALS)),)
CFLAGS += -DPI1541_CHAINBOOT_HELPER=1
CPPFLAGS += -DPI1541_CHAINBOOT_HELPER=1
endif

TARGET ?= kernel
CHAINLOADER_TARGET ?= kernel_chainloader
.PHONY: all $(LIBS) chainloader-clean

all: webcontent $(TARGET_CIRCLE)

legacy: $(TARGET)

chainloader: version $(OBJS_CHAINLOADER)
	@echo "  LINK $(CHAINLOADER_TARGET)"
	$(Q)$(CC) $(CFLAGS) -DPI1541_CHAINBOOT_HELPER=1 -o $(CHAINLOADER_TARGET).elf -Xlinker -Map=$(CHAINLOADER_TARGET).map -T linker-helper.ld -nostartfiles $(OBJS_CHAINLOADER)
	$(Q)$(PREFIX)objdump -d $(CHAINLOADER_TARGET).elf | $(PREFIX)c++filt > $(CHAINLOADER_TARGET).lst
	$(Q)$(PREFIX)objcopy $(CHAINLOADER_TARGET).elf -O binary $(CHAINLOADER_TARGET).img

chainloader-clean:
	$(Q)$(RM) $(OBJS_CHAINLOADER)

webcontent:
	$(MAKE) -C $(SRCDIR)/webcontent all

version: 
	@echo "#define PPI1541VERSION \"`git describe --tags`\"" > /tmp/__version_cmp
	@cmp -s /tmp/__version_cmp $(SRCDIR)/version.h || echo "#define PPI1541VERSION \"`git describe --tags`\"" > $(SRCDIR)/version.h 

$(TARGET_CIRCLE): version
	@$(MAKE) -C $(SRCDIR) -f Makefile.circle XFLAGS="$(XFLAGS)" COMMON_OBJS="$(COMMON_OBJS)" CIRCLE_OBJS="$(CIRCLE_OBJS)" 
	@cp $(SRCDIR)/$@ ./`basename $@ .img`$(TARGET_PZ2).img

$(TARGET): version $(OBJS_LEGACY) $(LIBS)
	@echo "  LINK $@"
	$(Q)$(CC) $(CFLAGS) -o $(TARGET).elf -Xlinker -Map=$(TARGET).map -T linker.ld -nostartfiles $(OBJS_LEGACY) $(LIBS)
	$(Q)$(PREFIX)objdump -d $(TARGET).elf | $(PREFIX)c++filt > $(TARGET).lst
	$(Q)$(PREFIX)objcopy $(TARGET).elf -O binary $(TARGET).img

uspi/libuspi.a:
	$(MAKE) -C uspi

clean:
	$(Q)$(RM) $(OBJS_LEGACY) $(OBJS_CIRCLE) $(OBJS_CHAINLOADER) $(TARGET).elf $(TARGET).map $(TARGET).lst $(TARGET).img $(TARGET_CIRCLE) *.img
	$(Q)$(RM) $(CHAINLOADER_TARGET).elf $(CHAINLOADER_TARGET).map $(CHAINLOADER_TARGET).lst $(CHAINLOADER_TARGET).img
	$(MAKE) -C uspi clean
	$(MAKE) -C $(SRCDIR) -f Makefile.circle clean 2>/dev/null || true
	$(MAKE) -C $(SRCDIR)/webcontent -f Makefile clean
	$(MAKE) -C CBM-FileBrowser_v1.6/sources clean

distclean: clean
	rm -f *.log

# Build Models (Pi Zero W Only)

This fork targets **Pi Zero W (01W) only**. We ship two kernels:

- **Emulation kernel (legacy Pi1541)**: builds with `make legacy RASPPI=0`
- **Service kernel (Circle)**: builds Circle + newlib with `circle-stdlib/configure -r 1`

## `RASPPI` Naming Gotcha

The symbol/flag `RASPPI` does **not** mean the same thing everywhere:

- **Legacy Pi1541 build**: `RASPPI=0` selects the Pi Zero / ARMv6 (ARM1176) code path.
- **Circle build**: `-r 1` / `RASPPI=1` targets the Pi 1 class (includes Pi Zero).
  Multicore support exists only when Circle defines `ARM_ALLOW_MULTI_CORE`.

## Multicore Policy (This Fork)

Pi Zero is single-core. In this fork:

- We treat the service kernel as **single-core only**
- We keep a tiny `Pi1541Cores` shim (no-op on Pi Zero) as an extension seam for
  potential Pi2+ forks, but we do not ship/enable multicore behavior here


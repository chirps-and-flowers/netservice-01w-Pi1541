# netservice-01w-Pi1541

Pi1541 legacy emulation plus a separate Circle "service kernel" for Raspberry Pi
Zero W (01W) only.

Provenance:
- Pi1541 v1.25f by Stephen White (inherited via pottendo upstream).
- Service kernel runs on Circle (bare-metal) by rsta2.
- This fork started from, and is heavily derived from, pottendo's Pi1541 Circle integration.
  See `CREDITS.md` and `3rdPartyFiles.txt` for links and full attribution.

Note: this is a forked codebase and still contains inherited, currently dormant/out-of-scope
code paths. The maintained target here is Pi Zero W (01W) only.
Pinned upstream base: `UPSTREAM.md`

## Scope
- Target: Raspberry Pi Zero W (01W) only.
- Kernels (output):
  - `kernel.img` (legacy emulator kernel, no networking)
  - `kernel_srv.lz4` (+ `kernel_srv.img` for debugging) (Circle service kernel)
  - `kernel_chainloader.img` (legacy chainloader used to enter service mode)
- MINI SERVICE hands off to emulation by writing `1541/_active_mount/ACTIVE.LST`.
  Service mode can be entered at boot (when ACTIVE.LST is missing) or by a long-press
  of ENTER/EJECT while mounted (chainloader).

## Config Files
- `config.txt`: Raspberry Pi boot config (Pi Zero W section).
- `options.txt`: minimal overrides.
- `options.txt.example`: full template (all knobs + notes).
- `wpa_supplicant.conf`: WiFi settings (WPA2-PSK only).

## MINI SERVICE
- Default HTTP port is `80` (`ServiceHttpPort` in `options.txt`).
- API and on-disk staging model: `docs/service-http.md`

## Build (Linux)

Build model:
- `build.sh` bootstraps lock-pinned dependencies (toolchain + Circle vendor tree).
- Circle bootstrap is lock-verified: `vendors/circle-stdlib.lock` (superproject commit) + `vendors/circle-stdlib/submodules.lock` (submodule commits).
- Vendor patching is lock-verified via `vendors/circle-stdlib/patches.lock`.
- The build is fail-closed: it checks the active `arm-none-eabi-gcc` path and version against `vendors/toolchain.lock`.
- Default mode reuses local cache/staging (`build/cache`, `build/staging`); clean flags force rebootstrap.

Common routes:

- Build all kernels:
```sh
./build.sh
```

- Build release SD root (no ROM binaries, standard WPA template):
```sh
./tools/build-sd-root.sh --clean --without-roms
```
- Uses `wpa_supplicant.conf.example` as `wpa_supplicant.conf`.
- Adds `ROMS_REQUIRED.txt` (required ROM/data names + hashes).

- Build dev SD root (ROMs + explicit local WPA file):
```sh
./tools/build-sd-root.sh --clean --with-roms --rom-source=/path/to/roms --wpa=/path/to/wpa_supplicant.conf
```

## Notes
- Full pottendo web UI migration is deferred: `docs/pottendo-full-webserver-migration.md`
- License: GPLv3 (see `LICENSE`).

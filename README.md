# netservice-01w-Pi1541

Pi1541 legacy emulation plus a separate Circle "service kernel" for Raspberry Pi
Zero W (01W) only.

Provenance:
- Pi1541 emulation core is by Stephen White.
- Service kernel runs on Circle (bare-metal) by rsta2.
- This fork started from, and is heavily derived from, pottendo's Pi1541 Circle integration.
  See `CREDITS.md` and `3rdPartyFiles.txt` for links and full attribution.

Pi1541 version note: pottendo refers to this integrated snapshot as Pi1541 v1.25f (not an upstream tag).

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
Host requirements:
- `bash`, `git`, `make`, `lz4`
- `curl`, `tar`, `unzip`
- GNU userland tools: `sha256sum`, `stat`, `awk`, `cut`, `grep`, `diff`, `mktemp`

Build model:
- `build.sh` bootstraps pinned dependencies from lockfiles (toolchain + Circle vendor tree).
- Circle bootstrap is lock-verified: `vendors/circle-stdlib.lock` (superproject commit) + `vendors/circle-stdlib/submodules.lock` (submodule commits).
- Vendor patching is lock-verified via `vendors/circle-stdlib/patches.lock`.
- The build is fail-closed: it checks the active `arm-none-eabi-gcc` path and version against `vendors/toolchain.lock`.
- Default mode reuses cached staging; clean flags force rebootstrap when cache is stale/corrupt.

Build all (service + emu + chainloader):
```sh
./build.sh
```

Common variants:
```sh
./build.sh --service          # service kernel only
./build.sh --pi1541           # emu + chainloader only
./build.sh --clean            # clean Circle staging + toolchain cache
./build.sh --clean-circle     # clean Circle staging only
./build.sh --clean-toolchain  # clean toolchain cache only
```

Artifacts:
- `build/out/kernel_pi1541.img`
- `build/out/kernel_service.img`
- `build/out/kernel_service.lz4`
- `build/out/kernel_chainloader.img`

## SD Root
Release-style SD root (no ROM binaries, no WiFi credentials):
```sh
./tools/build-sd-root.sh --clean --without-roms
```

Copy `build/sd-root/*` to the SD boot partition.

Local/dev SD root (include ROM/data during build):
```sh
./tools/build-sd-root.sh --clean --with-roms --rom-source=/path/to/roms
```

ROM/data guide:
- `--without-roms`: add ROM/data manually on SD (`ROMS_REQUIRED.txt` lists names + hashes).
- `--with-roms`: script copies ROM/data from repo root or `--rom-source`.

`wpa_supplicant.conf` guide:
- By default, `build-sd-root.sh` copies `wpa_supplicant.conf.example` to `wpa_supplicant.conf`.
- Edit `wpa_supplicant.conf` on SD with your local `country`, `ssid`, `psk`.
- Keep WiFi credentials local (do not commit real credentials to git).

## Notes
- Full pottendo web UI migration is deferred: `docs/pottendo-full-webserver-migration.md`
- License: `LICENSE`

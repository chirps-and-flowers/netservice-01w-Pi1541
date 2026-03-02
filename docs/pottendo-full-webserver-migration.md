# Pottendo Full Webserver Migration Plan

## Status
This is a migration reference document, not an active implementation task.
It may change as other planned features are implemented and stabilized first.
For the current roadmap, see `README.md`.
Once that work is stable, this document is the starting point for integrating
pottendo's full web UI on a second port.

## Goal
Run pottendo-style full web UI in service mode on a second port, while keeping MINI SERVICE separate and untouched.

## Source
- `src/webserver.h` (pottendo-derived baseline)
- `src/webserver.cpp` (pottendo-derived baseline)
- Local reference (old working tree):
  - `/home/jakob/Development/projects/pi1541/pi1541_01w_network_old_works/src/webserver.h`
  - `/home/jakob/Development/projects/pi1541/pi1541_01w_network_old_works/src/webserver.cpp`

## Group 1: Keep (direct reuse)
These are generic helpers or FatFS-based and can be reused with little/no change.
- Server shell: `CWebServer::CWebServer`, `~CWebServer`, `CreateWorker`
- Utility helpers: `urlEncode`, `urlDecode`, `endsWith`, `replaceAll`, `print_human_readable_time`, `extract_field`
- Filesystem helpers: `write_file`, `write_image`, `read_file`, `f_mkdir_full`, `f_unlink_full`, `download_file`
- Static asset handling in `GetContent`: css/logo/favicon/font/image-file serving

## Group 2: Keep but adapt
These should stay, but behavior should be made service-only.
- `direntry_table`: adapt to FatFS-only file manager UI (browse/upload/mkdir/delete/download only; no emulation mount actions).
- `GetContent` core pages (`/`, `/index.html`): adapt to FatFS-only service backend and active-list workflow (`_incoming` + `ACTIVE.LST` commit semantics).
- `GetContent` `/reset.html`: adapt to service teardown/reboot endpoint (service API), not emulation global flag.
- `GetContent` `/getindex.html`: adapt to local service-side extension filter (no `DiskImage` helper dependency).

## Group 3: Rewrite (remove hard couplings)
These depend on FileBrowser, IEC/emulation globals, or `DiskImage` class.
- `read_dir`: depends on `DiskImage` open/decode and `fileBrowser->DisplayDiskInfo`.
- `gen_index`: depends on `DiskImage::IsDiskImageExtention` / `IsLSTExtention`.
- `GetContent` branch `[NEWDISK]`: depends on `IEC_Commands`.
- `GetContent` branch `[NEWLST]`: depends on `fileBrowser->MakeLSTFromDir`.
- `GetContent` automount upload side effect: uses `webserver_upload`.
- `GetContent` `/mount-imgs.html`: depends on `mount_path`, `mount_img`, `mount_new`, and disk-preview path.

Service-mode replacement rule:
- "Mount" means writing/updating active set files (`ACTIVE.LST` flow), not setting runtime emulation globals.
- Pottendo "Mount" button must map 1:1 to MINI SERVICE mount semantics
  (same active-set pipeline: `/1541/_incoming` + `/1541/_active_mount` + `ACTIVE.LST`).

## Group 4: Optional/defer
Keep only if needed for first cut.
- `get_sector`, `D81DiskInfo` (raw D81 preview logic)
- Pages: `/pistats.html`, `/update.html`, `/options.html`, `/edit-config.html`, `/logger.html`

## Globals/Includes to remove in service port
- Globals: `webserver_upload`, `mount_img`, `mount_path`, `mount_new`, `reboot_req`
- Externals/includes: `FileBrowser.h`, `iec_commands.h`, `extern FileBrowser *fileBrowser`, `extern IEC_Commands *_m_IEC_Commands`
- `DiskImage` class usage paths

## Implementation order (high level)
1. Copy server shell + safe helpers into a new service-only webserver module.
2. Bring up static assets + browse/upload/mkdir/delete/download routes.
3. Implement service-mode mount semantics (`ACTIVE.LST` update flow).
4. Add optional pages only after base flow is stable.
5. Validate no `FileBrowser`, IEC/emulation global, or `DiskImage` dependencies remain.

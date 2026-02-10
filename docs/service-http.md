# Service HTTP (01W)

This is the **LAN control plane** exposed by the **service kernel** on **Pi Zero W (01W)**.

Goal: accept uploads + write SD manifests safely, then let the emulator kernel consume them.

At runtime the service kernel mounts the SD card, brings up Wi-Fi, starts the HTTP server, and waits for the frontend to upload and commit a new session.
Uploads are staged under `/1541/_incoming/` and become active only after commit moves them into `/1541/_active_mount/` and writes `ACTIVE.LST` via `ACTIVE.LST.tmp` + rename; modified disks are exposed via `/1541/_active_mount/dirty.lst`.

## On-SD Contract (FAT)

Created on demand:

- `/1541/`
- `/1541/_incoming/`       (upload staging)
- `/1541/_active_mount/`   (active queue + manifests)
- `/1541/_temp_dirty_disks/` (reserved for future retention/cache)

Manifests:

- `/1541/_active_mount/ACTIVE.LST` (active queue)
- `/1541/_active_mount/dirty.lst`  (modified disks)

Atomicity:

- Files are written to `*.tmp` then renamed to the final name.
- `ACTIVE.LST` is written as `ACTIVE.LST.tmp` then renamed.

## Pipelines (ASCII)

Service lifecycle (what happens end-to-end):

```
Boot service kernel (cold boot)
  |
  v
src/service/main.cpp: main()
  |
  v
CServiceKernel::Initialize()               (src/service/kernel.cpp)
  - SD + FAT mount
  |
  v
CServiceKernel::Run() -> service_init()    (src/service/kernel.cpp, src/service/service.cpp)
  - options + OLED
  - Kernel.wifi_start()
  - new CServiceHttpServer(...)            (src/service/http_server.cpp)
  |
  v
Frontend polls:
  - GET /hello                             (CServiceHttpServer::GetContent)
  |
  v
Frontend uploads:
  - PUT /upload/active                     (single-file shortcut: stage + commit)
    or:
    PUT /upload/active/add                 (stage/queue)
    POST /upload/active/commit             (commit queue)
  |
  v
Teardown: stop HTTP, clear OLED, reboot back to emulator kernel
```

Note (future improvement):

Today a successful commit requests teardown automatically. To avoid relying on a fixed
"flush delay" before reboot, we can switch to an explicit handshake:

- `POST /upload/active/commit` returns `{ok:true}` (no reboot)
- frontend then calls `POST /teardown`
- service replies `{ok:true}` and immediately reboots

That removes guessing/waits, but requires a frontend change and one extra request.

Upload/commit mechanics (SD staging + atomic manifests):

```
PUT /upload/active[/add]
  |
  v
CServiceHttpServer::HandleUpload()         (src/service/http_server.cpp)
  - EnsureServiceDirs()
  - write /1541/_incoming/<name>.tmp
  - rename to /1541/_incoming/<name>
  - remember <name> in RAM queue

POST /upload/active/commit
  |
  v
CommitPendingUploads()                     (src/service/http_server.cpp)
  - ClearActiveMountDir()
  - move queued files into /1541/_active_mount/<name>
  - WriteActiveListFromPending(): ACTIVE.LST.tmp -> rename ACTIVE.LST
```

Dirty disk pipeline (emulator -> service -> frontend download):

```
Emulator kernel
  -> writes /1541/_active_mount/dirty.lst (atomic)

Service kernel
  |
  v
GET /modified/list                         (CServiceHttpServer::GetContent)
  - ReadModifiedListSummary()
  |
  v
GET /modified/download/<i>                 (CServiceHttpServer::GetContent)
  - EnsureModifiedListLoaded(): Ensure summary/cache is current
  - PopulateModifiedListDirect(): resolve paths -> serve file bytes
```

## Transport

- TCP port: `15410` (dev default; may change later)
- JSON: `application/json`
- Downloads: `application/octet-stream`

## Nonce

- Service kernel generates a 32-bit nonce.
- Upload/commit requests must include `X-Nonce` matching the current nonce.
- Nonce is returned from `GET /hello`.

## Endpoints (v1)

Readiness:
- `GET /` -> mini LAN UI HTML
- `GET /C64_Pro_Mono-STYLE.ttf` -> UI font (ttf)
- `GET /hello` -> `{state, nonce, tcp_port, modified_count, modified_id, ...}`

Uploads:
- `PUT|POST /upload/active` -> single-file shortcut: stage + commit (replaces ACTIVE)
- `PUT|POST /upload/active/add` -> stage + queue (no commit)
- `POST /upload/active/commit` -> commit queued files into `_active_mount/` + write `ACTIVE.LST`

Active queue downloads:
- `GET /active/list`
- `GET /active/download/<i>`

Modified disk downloads:
- `GET /modified/list`
- `GET /modified/download/<i>`

## Upload Headers

Required:
- `Content-Type: application/octet-stream` (uploads are raw bytes)
- `X-Nonce`
- `X-Image-Size` (decimal or `0x...`; must match body length)
- `X-CRC32`      (decimal or `0x...`; CRC32 of body bytes)

Optional:
- `X-Image-Name` (suggested filename)
- `X-Image-Type` (extension hint like `d64` or `.d64`)

Tip: `curl --data-binary` defaults to `Content-Type: application/x-www-form-urlencoded`. For uploads,
always send `Content-Type: application/octet-stream` or you will get `BAD_CONTENT_TYPE`.

## PUT vs POST (why both exist)

`PUT /upload/active` is preferred because it is *idempotent* ("replace ACTIVE") so
clients can retry without creating extra state. `POST` is accepted for client
compatibility and for non-idempotent actions (`/commit`).

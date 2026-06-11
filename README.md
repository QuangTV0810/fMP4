# fMP4 Firmware Module

Firmware media module for camera product flow:

`app -> task -> service -> hal -> chipvendor`

Current focus:
- media capture/dispatch
- AAC encode
- fMP4 record from `H.264 + AAC`

## Architecture

- `sources/apps/app.cpp`
  - entrypoint `OS_Application_Startup()`
  - create OSAL tasks only
- `sources/apps/task/media-tasking.cpp`
  - media task
  - receive video/audio packets from `service_media`
- `sources/apps/task/record-tasking.cpp`
  - record task
  - receive `video main + audio aac`
  - feed recorder
- `sources/services/media`
  - manage media service flow
  - own AAC encode path
  - push packets to app/task by callback
- `sources/services/record`
  - fMP4 recorder
- `sources/hal`
  - HAL implementations per SoC
  - current tree includes `rk1106` and `rts3917n`
  - no arbitrary thread creation
- `sources/platform`
  - common definitions
  - osal glue layer
  - utils / ringbuffer helpers

## Design Rules

- App layer owns OSAL task lifecycle.
- HAL layer does not create thread freely.
- Service layer manages media logic and dispatches packets upward by callback.
- Video/audio config are separated in service config.
- Codec callbacks are separated by payload type:
  - `on_audio_pcm`
  - `on_audio_g711`
  - `on_audio_aac`
  - `on_video_main`
  - `on_video_sub`

## Source Tree

```text
sources/
  apps/
    app.cpp
    task/
      media-tasking.cpp
      record-tasking.cpp
  services/
    media/
    record/
  hal/
    rk1106/
    rts3917n/
  platform/
    common/
    osal/
    utils/
scripts/
  build_fdk-aac.sh
  build_media-server.sh
3rd/
  media-server/
  fdk-aac/
```

## Third-party Dependencies

This project uses the following third-party components:

### 1. OSAL

- Repo: `https://github.com/nasa/osal.git`
- Purpose:
  - task abstraction
  - OS API layer used by app/task startup flow
- Note:
  - this repo links against an installed OSAL package through `OSAL_INSTALL_DIR`
  - source is not built from local `3rd` in current Makefile

### 2. media-server

- Repo: `https://github.com/ireader/media-server.git`
- Local path: `3rd/media-server`
- Used libraries:
  - `libmov`
  - `libhls`
- Purpose:
  - fMP4 mux
  - fragment/segment writer backend

### 3. fdk-aac

- Repo: `https://github.com/mstorsjo/fdk-aac.git`
- Local path: `3rd/fdk-aac`
- Purpose:
  - AAC encoder backend

## Build

Required environment:

```bash
export SYSROOT=<path-to-staging>
export CROSS_COMPILE=<toolchain-prefix>
export OSAL_INSTALL_DIR=<path-to-osal-install>
```

Example:

```bash
export SYSROOT=/path/to/sysroot
export CROSS_COMPILE=arm-linux-
export OSAL_INSTALL_DIR=/path/to/osal/install
```

Build FDK-AAC first:

```bash
./scripts/build_fdk-aac.sh
```

Build media-server subset:

```bash
./scripts/build_media-server.sh
```

Then build project:

```bash
make
```

Output:

```text
build/bin/media-tasking
```

## Notes

- `scripts/build_media-server.sh` currently builds only the subset needed by this project:
  - `libmov`
  - `libhls`
- `scripts/build_fdk-aac.sh` builds and installs static `fdk-aac` into `3rd/fdk-aac/build/install`
- Recorder currently targets rolling `.mp4` output with 1-second fMP4 fragments inside each segment file.
- Record task and media task are separated to keep app/task responsibility clear.

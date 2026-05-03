# Homemade Alexa ESP32-S3-Box3

Voice assistant firmware for ESP32-S3-Box3. Detects the "Alexa" wake word, records speech, sends PCM to an STT server, and reacts to commands over WebSocket.

> This project is heavily based on [Willow](https://github.com/HeyWillow/willow) — an open-source voice assistant firmware for ESP32-S3-Box3. The build system, audio pipeline, and hardware abstraction layer are derived from that project.

## Requirements

- Docker (for building — includes IDF 5.3.3 + ADF)
- Python 3 + venv (for helper tools: esptool, nvs\_partition\_gen)
- `tio` — serial port monitor (`apt install tio` / `brew install tio`)

### Python environment setup

> Required before using `utils.sh flash`, `flash-app`, `erase-flash`, `monitor`, and `build_nvs.sh`.

```bash
source venv/bin/activate
pip install -r requirements.txt
```

## `utils.sh` tool

All build and flash operations are handled by `./utils.sh <command>`:

| Command | Description |
|---------|-------------|
| `setup` | Initializes sdkconfig (once, before the first build; automatically starts the Docker container) |
| `build` | Builds the firmware (automatically starts the Docker container) |
| `config` | Opens menuconfig (automatically starts the Docker container) |
| `clean` | Cleans the `build/` directory |
| `flash` | Flashes the full firmware (requires `PORT`) |
| `flash-app` | Flashes only the app binary — faster for code-only changes (requires `PORT`) |
| `erase-flash` | Erases the entire flash (requires `PORT`; must be run before the first `flash`) |
| `monitor` | Attaches the `tio` serial monitor (requires `PORT`) |
| `docker` | Enters the Docker container interactively |

## Building

Requires a running Docker daemon. The `willow:latest` image contains the pre-built IDF + ADF environment.

```bash
./utils.sh build
```

The resulting firmware is placed in `build/alexa.bin`.

### First build (clean environment)

```bash
./utils.sh setup         # set target to esp32s3, generate sdkconfig
./utils.sh config        # set WiFi credentials and server address
./utils.sh build         # build the firmware
```

## Flashing

Flashing is done from the host (outside the container). Requires the `PORT` environment variable:

```bash
export PORT=/dev/ttyUSB0    # or /dev/ttyACM0, check with: ls /dev/tty*
```

### First flash (clean device)

```bash
./utils.sh erase-flash   # erases the entire flash
./utils.sh flash         # flashes bootloader, partitions, firmware, and model
```

After `flash` completes, the serial monitor (`tio`) starts automatically.

### Firmware update (without changing partitions/model)

```bash
./utils.sh flash-app     # flashes only alexa.bin at address 0x30000
```

## WiFi and server configuration

### Build-time defaults (Kconfig)

WiFi credentials and server address are set before the first build via menuconfig:

```bash
./utils.sh config
```

Navigate to *Alexa Configuration* and set:

| Option | Description |
|--------|-------------|
| `WIFI_SSID` | WiFi network name |
| `WIFI_PASSWORD` | WiFi password |
| `SERVER_HOST` | Server host and port (e.g. `192.168.0.144:8080`) |

These values are stored in `sdkconfig` (gitignored) and baked into the firmware at build time.

## Tools

The `tools/` directory contains optional utilities not required for a standard setup.

### NVS runtime override

NVS values take precedence over Kconfig defaults and allow reconfiguring the device without rebuilding — useful for changing WiFi credentials in the field.

```bash
cp tools/nvs.csv.example tools/nvs.csv
# edit tools/nvs.csv
export PORT=/dev/ttyUSB0
./tools/build_nvs.sh
```

`tools/nvs.csv` is gitignored. The script generates `build/alexa_nvs.bin` and flashes it at address `0x9000`.

## Voice capture parameters

Key constants in `main/audio.c`:

| Constant | Value | Description |
|----------|-------|-------------|
| `DEFAULT_WAKE_WORD` | `"alexa"` | Wake word (WakeNet9 model) |
| `DEFAULT_WAKE_MODE` | `"2CH_90"` | Detection mode: 2 microphones, 90% sensitivity (lower = fewer false positives) |
| `DEFAULT_VAD_MODE` | `2` | VAD aggressiveness (0–3; higher = more aggressive silence trimming) |
| `DEFAULT_VAD_TIMEOUT` | `300` | Silence duration that ends recording [ms] |
| `DEFAULT_RECORD_BUFFER` | `12` | Recording buffer size |
| `DEFAULT_SPEAKER_VOLUME` | `60` | Volume (0–100) |

## Communication flow

### Device states

```
IDLE_MODE ──(wake word)──► LISTENING_MODE ──(VAD / timeout)──► REQUEST_MODE
    ▲                                                                │
    └──────────────── cmd: idle / cmd: listen ◄─────────────────────┘
                          (over WebSocket)
```

- **IDLE_MODE** — screen off, waiting for wake word
- **LISTENING_MODE** — screen on, actively recording speech
- **REQUEST_MODE** — recording sent to server, waiting for response

### Startup sequence

1. Device connects to WiFi (credentials from NVS).
2. Establishes WebSocket connection with the server (`/ws/internal`) and sends `{"hello": {"hostname": "...", "hw_type": "..."}}`.
3. Listens for the "Alexa" wake word via WakeNet9 (2 microphones).

### Voice command handling

1. Wake word detected → switch to LISTENING_MODE, VAD active.
2. After silence (`VAD_TIMEOUT` ms) or timeout → chunked HTTP POST with 16-bit 16 kHz PCM to `/api/internal/stt`.
3. Server responds `{"status": "ok"}` — device waits for a command over WebSocket.
4. Server processes audio (STT → LLM → TTS), plays the response through the speaker, then sends:
   - `{"cmd": "listen"}` — return to LISTENING_MODE (continue conversation)
   - `{"cmd": "idle"}` — return to IDLE_MODE

### WebSocket commands accepted by the device

| Command | Action |
|---------|--------|
| `{"cmd": "listen"}` | Triggers LISTENING_MODE |
| `{"cmd": "idle"}` | Returns to IDLE_MODE |
| `{"cmd": "display", "data": {"title": "...", "text": "..."}}` | Displays text, wakes the screen |
| `{"cmd": "ota_start", "ota_url": "..."}` | Downloads and installs new firmware |
| `{"cmd": "restart"}` | Restarts the device |

### Events sent by the device

| Event | When |
|-------|------|
| `{"hello": {"hostname": "...", "hw_type": "..."}}` | After WebSocket connection is established |
| `{"event": "wakeword"}` | After wake word detection (interrupts audio playback on the server) |

## Partition map

| Partition | Offset | Size | Description |
|-----------|--------|------|-------------|
| `nvs` | `0x9000` | 144 KB | configuration (WiFi, server URL) |
| `otadata` | `0x2D000` | 8 KB | OTA metadata |
| `ota_0` | `0x30000` | 3 MB | active application |
| `ota_1` | `0x330000` | 3 MB | OTA slot |
| `model` | `0x630000` | 6 MB | WakeNet model (SPIFFS) |
| `user` | `0xC30000` | 3.9 MB | user data (SPIFFS) |

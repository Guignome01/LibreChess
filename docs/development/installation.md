# Installation

How to set up the development environment, build the firmware, and flash it to an ESP32.

## Prerequisites

- [VS Code](https://code.visualstudio.com/) with the [PlatformIO IDE](https://platformio.org/install/ide?install=vscode) extension
- An ESP32 development board connected via USB
- **Optional**: Node.js with npm tools for web asset minification:
  ```
  npm install -g html-minifier-terser clean-css-cli terser
  ```
  These are not required — the `data/` directory ships pre-built with compressed web assets.

## Clone & Open

```bash
git clone https://github.com/Guignome01/LibreChess.git
```

Open the `LibreChess.code-workspace` file in VS Code. This configures the workspace with the correct folder structure and PlatformIO settings.

## Build & Flash

### Using VS Code

- **Build**: `Ctrl+Alt+B`
- **Upload (flash)**: `Ctrl+Alt+U`

### Using the CLI

PlatformIO's CLI (`pio`) is not on the system PATH by default. Use the full path:

| Platform | Path |
|----------|------|
| Windows | `%USERPROFILE%\.platformio\penv\Scripts\pio.exe` |
| Linux | `~/.platformio/penv/bin/pio` |

Commands:

```bash
# Build firmware
pio run

# Flash firmware to ESP32
pio run -t upload

# Open serial monitor (115200 baud)
pio device monitor
```

## Build Pipeline

PlatformIO runs three scripts as part of the build process, defined in `platformio.ini`:

### 1. Minification (`src/web/build/minify.py`) — pre-build
Minifies HTML, CSS, and JS source files from `src/web/` into `src/web/build/`. If the required npm tools (`html-minifier-terser`, `clean-css-cli`, `terser`) are not installed, this step is silently skipped and a warning is printed.

### 2. LittleFS Preparation (`src/web/build/prepare_littlefs.py`) — pre-build
Takes the minified files (or source files if minification was skipped) and gzip-compresses them into the `data/` directory with `.gz` extensions. Files with `.nogz.` in their name (e.g., `capture.nogz.mp3`) are copied without compression. After preparation, all intermediate minified files in `src/web/build/` are deleted.

### 3. Filesystem Upload (`src/web/build/upload_fs.py`) — on upload
Hooks into `pio run -t upload`. Computes a SHA-256 hash of the `data/` directory contents and compares it with a cached hash (`.littlefs_hash`). The LittleFS filesystem image is only re-uploaded when the hash changes, saving time on firmware-only updates.

The `data/` directory is committed to the repository, so the build works without npm minification tools. Edit source HTML, CSS, and JS files in `src/web/` — never modify files in `data/` directly.

## GPIO Configuration

If you're using different GPIO pins than the defaults, edit the pin definitions in `src/board/core/driver.h` before flashing. The default pins are documented in the [wiring guide](../hardware/wiring.md).

Note that the physical order of row pin connections does not matter — the calibration process maps pins to logical board coordinates.

## First Boot

1. **Calibration** — on first boot (no saved calibration data), the board enters the calibration process. This requires a serial monitor connection. See the [calibration guide](../guides/calibration.md) for the full procedure.

2. **WiFi AP** — the board starts a WiFi access point:
   - SSID: `LibreChess`
   - Password: `chess123`

3. **Connect and configure** — join the AP from a phone or computer, then open `http://librechess.local` (or `http://192.168.4.1`). Add your home WiFi network in the WiFi Settings section.

4. **AP shutdown** — approximately 10 seconds after a stable WiFi connection is established, the AP automatically shuts down. Access the board through its WiFi IP address or `http://librechess.local` (via mDNS).

5. **AP recovery** — if the WiFi connection is lost, the AP re-enables automatically so you can always reconfigure.

## Factory Reset

To erase all saved settings (WiFi credentials, Lichess token, OTA password, calibration data, LED brightness):

1. Add `-DFACTORY_RESET` to the `build_flags` line in `platformio.ini`
2. Build and flash the firmware
3. The ESP32 erases its entire NVS partition on boot
4. **Remove the flag** from `platformio.ini` after flashing, then flash again for normal operation

## OTA Firmware Updates

After the initial USB flash, subsequent firmware updates can be uploaded wirelessly through the web UI:

1. Build the firmware (`pio run`) — the output binary is at `.pio/build/esp32dev/firmware.bin`
2. Open the web UI settings page
3. Drag and drop the `.bin` file onto the OTA upload area (or click to browse)
4. If an OTA password is set, enter it when prompted
5. The upload progress is shown in real time
6. After a successful upload, the ESP32 reboots automatically and the page reconnects

The firmware validates the uploaded file by checking for the ESP32 magic byte (`0xE9`) at offset 0, rejecting non-firmware files.

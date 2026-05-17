# Troubleshooting

Diagnostic tools and solutions for common issues with LibreChess.

## Serial Monitor

The firmware outputs detailed diagnostic information over the serial connection. This is the primary debugging tool for most hardware and software issues.

**Setup:**
- Connect the ESP32 to your computer via USB
- Open a serial monitor at **115200 baud** (PlatformIO: `pio device monitor`, or use the VS Code PlatformIO serial monitor)

**What it shows:**
- Boot sequence progress (NVS init, LittleFS mount, WiFi connection, calibration)
- Sensor scan results during calibration
- Game state changes (piece lifts, move validation, turn changes)
- WiFi connection status and reconnection attempts
- API request/response details (Stockfish, Lichess)
- Error messages with context

The serial monitor is especially important during calibration, which currently uses serial prompts to guide the user through the process. See the [calibration guide](guides/calibration.md) for details.

## Sensor Test Mode

The built-in sensor test mode is a quick way to verify that all 64 sensors are working correctly:

1. Select **Sensor Test** from the main menu (red square)
2. Place pieces on the board — each detected square lights up in white
3. Move a piece across every square to confirm full coverage
4. The mode completes automatically when all 64 squares have been visited

This is useful for identifying dead sensors, weak magnets, or wiring issues. If a square never lights up, check:
- The hall-effect sensor orientation and solder joints
- The magnet strength (8×2mm magnets may need to be stacked)
- The iron disc placement (required to spread the magnetic field)
- The shift register output for that column

## Grid Scan Test Utility

For lower-level hardware debugging, the repository includes `grid_scan_test.cpp` at the project root. This is a standalone utility (not compiled in normal builds) that directly tests the shift register column scanning and row GPIO reads without any chess logic.

To use it:
1. Temporarily replace `src/main.cpp` with `grid_scan_test.cpp` (or adjust `platformio.ini` to point to it)
2. Flash to the ESP32
3. Monitor the serial output to see raw sensor readings per column and row

This is useful when the sensor grid isn't responding as expected and you need to isolate whether the issue is in the wiring, the shift register, or the firmware's scan logic.

## Common Issues

### WiFi Won't Connect
- Verify the SSID and password are correct in the web UI WiFi settings
- The ESP32 only supports 2.4 GHz networks (not 5 GHz)
- Check the serial monitor for connection error details
- If the AP doesn't appear, the board may already be connected to a saved network — try powering off and on, or use a factory reset

### AP Not Visible
The access point automatically disables approximately 10 seconds after a stable WiFi connection is established. If WiFi is lost, the AP re-enables immediately. If the AP doesn't appear at all:
- Check the serial monitor for boot errors
- Try a factory reset (add `-DFACTORY_RESET` to `build_flags` in `platformio.ini`, flash, then remove the flag)

### Sensors Not Detecting Pieces
- Run the sensor test mode to identify which squares are affected
- Ensure magnets are embedded in the chess pieces with the correct polarity facing the sensor
- Iron discs under the board are required — without them, the magnetic field may not reliably trigger the A3144 sensors
- Check that transistors powering each sensor column are wired correctly
- Use `grid_scan_test.cpp` for raw hardware debugging

### LEDs Not Lighting Up
- Verify the LED strip data pin is connected to GPIO 32
- Check that the logic level converter is properly shifting the 3.3V signal to 5V
- Ensure adequate power supply — 64 WS2812B LEDs at full brightness draw significant current
- If only some LEDs work, check for breaks in the LED strip serpentine routing

### Calibration Fails
- The board must be completely empty before calibration begins
- Place only one piece at a time when prompted
- If a sensor is detected on a square that was already mapped, the board flashes red and asks you to retry
- See the [calibration guide](guides/calibration.md) for the full process

### OTA Upload Fails
- Verify the file is a valid ESP32 firmware binary (`.bin` file starting with magic byte `0xE9`)
- If an OTA password is set, ensure it's entered correctly in the upload form
- Check that the ESP32 has enough free space for the update
- The web UI shows upload progress — if it stalls, check the serial monitor for errors

### Board Shows Red Flash on Mode Selection
A full-board red flash (3 times) typically indicates a connectivity issue:
- In bot mode: the ESP32 can't reach the Stockfish API — check WiFi connection
- In Lichess mode: the Lichess token is missing or invalid — configure it in the web UI settings

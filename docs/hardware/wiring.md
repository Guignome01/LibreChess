# Wiring

GPIO pin assignments and wiring connections for the LibreChess chessboard.

## Pin Configuration

All GPIO pins are defined in `src/board/runtime/driver.h`. The calibration system maps physical pin order to logical board coordinates, so the order in which row pins are connected does not matter — calibration handles the mapping.

### LED Strip

| Signal | GPIO | Notes |
|--------|------|-------|
| Data IN | 32 | WS2812B data line. Uses NeoPixelBus with I2S DMA output for non-blocking writes. Connect through a logic level converter (3.3V → 5V). |

### Shift Register (SN74HC595N)

The shift register scans the 8 sensor columns using only 3 GPIO pins:

| Signal | Connection | Notes |
|--------|-----------|-------|
| SER (serial data) | GPIO 33 | Data input — the ESP32 shifts a high bit through each column |
| SRCLK (shift clock) | GPIO 14 | Clocks data into the shift register |
| RCLK (latch clock) | GPIO 26 | Latches the shifted data to the output pins |
| SRCLR' (clear) | 5V | Active low — tied high to disable clearing |
| OE' (output enable) | GND | Active low — tied low to keep outputs always enabled |
| VCC | 5V | Power through logic level converter |
| GND | GND | Common ground with ESP32 |

Each of the 8 shift register outputs (QA–QH) powers one column of sensors through a transistor.

### Sensor Row Inputs

The 8 row GPIO pins read the sensor outputs for the currently active column:

| Row | GPIO |
|-----|------|
| 0 | 4 |
| 1 | 16 |
| 2 | 17 |
| 3 | 18 |
| 4 | 19 |
| 5 | 21 |
| 6 | 22 |
| 7 | 23 |

Each row pin has a 10kΩ pull-down resistor to ground. The row-to-rank mapping is determined during calibration, so the physical order of these connections does not affect functionality.

## Sensor Matrix

The 64 hall-effect sensors are arranged in an 8×8 matrix:

- **Columns** — each column of 8 sensors shares a 5V supply line, switched by a transistor connected to one shift register output
- **Rows** — each row of 8 sensors shares a signal output line, connected to one ESP32 GPIO input

During a scan cycle, the shift register activates one column at a time. The ESP32 reads all 8 row pins to determine which sensors in that column detect a magnet. This multiplexing scheme reads 64 sensors using only 11 GPIO pins (3 for the shift register + 8 for rows).

### Transistor Wiring (NPN — 2N2222)

For each of the 8 columns:

- **Collector** → sensor column 5V supply line
- **Emitter** → GND
- **Base** → shift register output (QA–QH) through a 1kΩ resistor

When the shift register output goes high, the transistor switches on, powering that column of sensors.

### Transistor Wiring (PNP — 2N2907)

Alternative configuration using PNP transistors:

- **Emitter** → 5V
- **Collector** → sensor column 5V supply line
- **Base** → shift register output through a 1kΩ resistor

The logic is inverted compared to NPN — the column is powered when the shift register output is low.

## Logic Level Converter

The ESP32 operates at 3.3V logic, but the WS2812B LEDs, SN74HC595N shift register, and transistors require 5V signals. A logic level converter bridges this gap:

- **SN74AHCT125N** (recommended) — quad buffer with 3.3V-compatible inputs and 5V outputs. Connect the LED data line, shift register SER, SRCLK, and RCLK through separate channels.
- **BSS138** — bidirectional level shifter module. Works but has slower rise times that may cause issues with the WS2812B data protocol at longer strip lengths.

## Power Injection

The WS2812B strip can experience voltage drop over its length, causing LEDs far from the power input to appear dimmer or show incorrect colors. The USB-C female port provides a secondary power injection point:

- Connect 5V and GND from the USB-C port to the LED strip at the midpoint or far end
- This supplements the power from the ESP32's USB connection
- A dedicated 5V 2A+ power supply through the injection port is recommended for full-brightness operation

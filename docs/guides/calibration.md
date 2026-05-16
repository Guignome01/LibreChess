# Calibration

The board calibration process maps physical hardware positions (sensor pins, shift register outputs, LED pixel indices) to logical board coordinates. It runs on first boot or when triggered manually.

## Purpose

The hardware layout of the chessboard doesn't inherently know which physical sensor or LED corresponds to which chess square. The shift register columns, GPIO row pins, and LED strip pixel order all depend on how the board was wired and assembled. Calibration creates a mapping from physical hardware positions to the logical 8×8 grid so the firmware can correctly interpret sensor data and control LEDs.

This mapping is saved to NVS (non-volatile storage) and persists across reboots — calibration only needs to be done once unless the hardware is rewired.

## When to Calibrate

- **First boot** — calibration runs automatically if no saved mapping exists
- **Manual trigger** — use the "Recalibrate" button in the web UI board settings, or send `POST /board-calibrate`. Calibration runs on the next reboot.
- **After hardware changes** — if sensors are rewired, the LED strip is re-routed, or the board orientation changes

## Current Process

The calibration is currently guided through **serial monitor output** (115200 baud). A computer connection is required to read the prompts and follow the instructions. This is a known area of improvement — see the [roadmap](../roadmap.md) for plans to implement a fully LED-guided process.

### Step 1 — LED Sweep

On startup, the board lights each of the 64 LEDs one by one in white, with a brief delay between each. This confirms the LED strip is functional and correctly connected.

After the sweep, a 5-second window allows you to type `skip` in the serial monitor to temporarily bypass calibration (useful for testing the web UI when sensors aren't connected). Skipping uses an identity mapping — sensors and LEDs won't correspond to the correct squares.

### Step 2 — Empty the Board

Remove all pieces from the board. The firmware polls all sensors and waits until no magnets are detected. If any sensors still read as occupied, the serial monitor reports which ones every 4 seconds. The board must be stable (all sensors clear) for 500ms to proceed.

### Step 3 — Rank Calibration

The serial monitor prompts you to place a piece on specific squares one at a time:

```
Place a piece on a1
```

Place a single piece on the requested square and wait. The firmware detects which sensor activates and maps that sensor position to the prompted rank. After detection (stable for 500ms), remove the piece and wait for the board to read empty again.

This repeats for squares a1 through a8, mapping all 8 ranks. The firmware auto-detects whether row GPIOs or shift register columns correspond to ranks.

**Errors**: if more than one sensor activates, or the sensor doesn't match expectations, all 64 LEDs flash red, the board waits for empty, and the step retries.

### Step 4 — File Calibration

Similar to rank calibration, but across files:

```
Place a piece on a1
Place a piece on b1
...
Place a piece on h1
```

The firmware cross-references with the rank calibration data to verify consistency. This maps the remaining axis (files) to physical positions.

### Step 5 — LED Mapping

For each of the 64 LED pixel indices (0 through 63):

1. The current pixel lights up in **white**
2. All previously mapped pixels light up in **green**
3. The serial monitor prompts: "Place a piece on the white LED"
4. Place a piece on the square where you see the white light
5. The firmware records which logical square (from the sensor mapping established in steps 3–4) corresponds to that LED pixel

**Duplicate detection**: if you place a piece on a square that was already mapped to a different LED, all LEDs flash red and the step retries.

After each mapping, remove the piece and wait for the board to read empty before the next pixel.

### Completion

After all 64 LEDs are mapped, the calibration data is saved to NVS. The serial monitor prints "Calibration complete" and the board proceeds to the game selection menu.

## Calibration Data

The calibration produces three mappings stored in NVS:

- **Axis swap flag** — whether ranks correspond to GPIO rows or shift register columns
- **Row/column mapping tables** — physical pin/output → logical row/column index
- **LED index map** — logical (row, col) coordinate → physical pixel index

`BoardCalibrationRunner` owns the serial-guided startup flow and writes the mapping to NVS before the renderer starts. External callers use `Board::resetCalibration()`, which clears the saved mapping and reboots so the startup runner executes again. `BoardDriver` applies that mapping for every sensor read and LED write operation throughout normal operation.

## Troubleshooting

- **Sensor not detected** — ensure only one piece is on the board and it's on the correct square. Check that the magnet is strong enough and the iron disc is in place.
- **Red flash during calibration** — either multiple sensors triggered simultaneously, or the detected position conflicts with prior calibration data. Empty the board and retry the step.
- **Skipped calibration** — if you typed `skip`, the board uses an identity mapping. Restart without skipping to run the full process.
- **Recalibration** — trigger via the web UI or API. The board re-enters the calibration flow on the next boot, overwriting the previous mapping.

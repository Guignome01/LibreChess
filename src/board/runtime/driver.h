#ifndef BOARD_DRIVER_H
#define BOARD_DRIVER_H

#include <NeoPixelBusLg.h>

#include "colors.h"
#include "helpers.h"

// ---------------------------
// Hardware Configuration
// ---------------------------

// ---------------------------
// WS2812B LED Data IN GPIO Pin
// The strip doesn't need to have a specific layout, calibration will map it correctly
// ---------------------------
#define LED_PIN 32
inline constexpr int NUM_ROWS = BoardHelpers::ROWS;
inline constexpr int NUM_COLS = BoardHelpers::COLS;
inline constexpr int LED_COUNT = BoardHelpers::SQUARES;
#define BRIGHTNESS 255 // LED brightness: 0-255 (0=off, 255=max). Current: 255 (100% max brightness)

// ---------------------------
// Shift Register (74HC595) Pins
// ---------------------------
// Pin 10 (SRCLR') 5V = don't clear the register
// Pin 13 (OE') GND = always enabled
// Pin 11 (SRCLK) GPIO = Shift Register Clock
#define SR_CLK_PIN 14
// Pin 12 (RCLK) GPIO = Latch Clock
#define SR_LATCH_PIN 26
// Pin 14 (SER) GPIO = Serial data input
#define SR_SER_DATA_PIN 33
// Set to 1 if the shift register outputs drive PNP transistors
#define SR_INVERT_OUTPUTS 0

// ---------------------------
// Row and column pins don't need to be in any particular order, calibration will map them correctly
// ---------------------------

// ---------------------------
// Row Input Pins (Safe GPIOs for ESP32: 4, 13, 14, [16-17], 18, 19, 21, 22, 23, 25, 26, 27, 32, 33)
// ---------------------------
#define ROW_PIN_0 4
#define ROW_PIN_1 16
#define ROW_PIN_2 17
#define ROW_PIN_3 18
#define ROW_PIN_4 19
#define ROW_PIN_5 21
#define ROW_PIN_6 22
#define ROW_PIN_7 23

// Shared logical row-to-GPIO lookup used by both the steady-state driver and
// startup calibration.
inline constexpr int rowPins[NUM_ROWS] = {
  ROW_PIN_0,
  ROW_PIN_1,
  ROW_PIN_2,
  ROW_PIN_3,
  ROW_PIN_4,
  ROW_PIN_5,
  ROW_PIN_6,
  ROW_PIN_7,
};

// ---------------------------
// Sensor Polling Delay and Debounce
// ---------------------------
#define SENSOR_READ_DELAY_MS 40
#define DEBOUNCE_MS 125
#define CALIBRATION_WARNING_INTERVAL_MS 4000

// ---------------------------
// Board Driver Class
// Logical board coordinates: row 0 = rank 8, column 0 = file a
// ---------------------------
class BoardDriver {
 private:
  NeoPixelBusLg<NeoGrbFeature, NeoEsp32I2s0800KbpsMethod, NeoGammaNullMethod> strip;
  bool sensorState[NUM_ROWS][NUM_COLS];
  bool sensorRaw[NUM_ROWS][NUM_COLS];
  unsigned long sensorDebounceTime[NUM_ROWS][NUM_COLS];
  int lastEnabledCol; // Tracks last enabled column for efficient sequential shifting

  // LED settings (persisted in NVS)
  uint8_t brightness;                       // Global brightness 0-255
  uint8_t dimMultiplier;                    // Dark square dim factor 0-100 (stored as percentage)
  LedRGB currentColors[NUM_ROWS][NUM_COLS]; // Track current colors for dim multiplier updates

  // Calibration data
  uint8_t swapAxes;
  uint8_t toLogicalRow[NUM_ROWS];
  uint8_t toLogicalCol[NUM_COLS];
  uint8_t ledIndexMap[NUM_ROWS][NUM_COLS];

  void loadLedSettings();
  void loadDefaultLedMapping();

  void loadShiftRegister(byte data, int bits = 8);
  void disableAllCols();
  void enableCol(int col);
  int getPixelIndex(int row, int col) const;

 public:
  BoardDriver();
  void begin();
  void readSensors();
  bool getSensorState(int row, int col) const;

  /// Read the current logical sensor matrix and seed the debounced state from
  /// it. Used once at runtime startup so the input baseline and driver debounce
  /// state agree before the poll task starts.
  void syncSensorBaseline(bool (&state)[NUM_ROWS][NUM_COLS]);

  /// Read the physical sensor matrix without debounce or logical mapping.
  void readRawSensors(bool (&rawState)[NUM_ROWS][NUM_COLS]);

  /// Set one physical LED pixel directly during calibration.
  void setRawCalibrationLED(int pixelIndex, LedRGB color);

  /// Clear all physical LED pixels directly during calibration.
  void clearRawCalibrationLEDs(bool show = true);

  /// Reset logical sensor mapping to identity before calibration or skip mode.
  void resetCalibrationMapping();

  /// Use raw row-major LED mapping when calibration is skipped.
  void loadRawIdentityCalibrationLedMapping();

  /// Return whether raw row/column axes are swapped by calibration.
  uint8_t calibrationSwapAxes() const { return swapAxes; }

  /// Store whether raw row/column axes are swapped by calibration.
  void setCalibrationSwapAxes(uint8_t value) { swapAxes = value; }

  /// Return logical row assigned to one raw row/column index.
  uint8_t logicalRowMapping(int rawIndex) const { return toLogicalRow[rawIndex]; }

  /// Assign logical row to one raw row/column index.
  void setLogicalRowMapping(int rawIndex, uint8_t logicalRow) { toLogicalRow[rawIndex] = logicalRow; }

  /// Return logical column assigned to one raw row/column index.
  uint8_t logicalColMapping(int rawIndex) const { return toLogicalCol[rawIndex]; }

  /// Assign logical column to one raw row/column index.
  void setLogicalColMapping(int rawIndex, uint8_t logicalCol) { toLogicalCol[rawIndex] = logicalCol; }

  /// Return physical LED pixel assigned to one logical square.
  uint8_t ledCalibrationIndex(int row, int col) const { return ledIndexMap[row][col]; }

  /// Assign physical LED pixel to one logical square.
  void setLedCalibrationIndex(int row, int col, uint8_t pixelIndex) { ledIndexMap[row][col] = pixelIndex; }

  void clearAllLEDs(bool show = true);
  void setSquareLED(int row, int col, LedRGB color);
  void showLEDs();

  // Board settings
  uint8_t getBrightness() const { return brightness; }
  uint8_t getDimMultiplier() const { return dimMultiplier; }
  void setBrightness(uint8_t value);
  void setDimMultiplier(uint8_t value);
  void saveLedSettings();
};

#endif // BOARD_DRIVER_H

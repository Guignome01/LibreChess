#ifndef BOARD_DRIVER_H
#define BOARD_DRIVER_H

#include "calibration.h"
#include "colors.h"
#include "lifecycle.h"
#include <NeoPixelBusLg.h>
#include <atomic>

// ---------------------------
// Hardware Configuration
// ---------------------------

// ---------------------------
// WS2812B LED Data IN GPIO Pin
// The strip doesn't need to have a specific layout, calibration will map it correctly
// ---------------------------
#define LED_PIN 32
#define NUM_ROWS 8
#define NUM_COLS 8
#define LED_COUNT (NUM_ROWS * NUM_COLS)
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
  friend class BoardCalibration;

 private:
  NeoPixelBusLg<NeoGrbFeature, NeoEsp32I2s0800KbpsMethod, NeoGammaNullMethod> strip;
  BoardCalibration calibration_;
  BoardAnimationLifecycle animationLifecycle_;
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
  bool calibrationLoaded;

  void loadLedSettings();
  void resetLogicalMapping();
  void loadDefaultLedMapping();
  void loadRawIdentityLedMapping();

  void loadShiftRegister(byte data, int bits = 8);
  void disableAllCols();
  void enableCol(int col);
  int getPixelIndex(int row, int col);

 public:
  BoardDriver();
  void begin();
  void readSensors();
  bool getSensorState(int row, int col);

  // LED Control
  void acquireLEDs(); // Block until LED strip available
  void releaseLEDs(); // Release LED strip

  // RAII guard for LED mutex — acquires on construction, releases on scope exit.
  // Use in scoped blocks for safe LED writes without manual acquire/release.
  struct LedGuard {
    BoardDriver* driver;
    explicit LedGuard(BoardDriver* bd) : driver(bd) { driver->acquireLEDs(); }
    ~LedGuard() { driver->releaseLEDs(); }
    LedGuard(const LedGuard&) = delete;
    LedGuard& operator=(const LedGuard&) = delete;
  };

  void clearAllLEDs(bool show = true);
  void setSquareLED(int row, int col, LedRGB color);
  void showLEDs();

  // Animation Functions (queued for async execution)
  void fireworkAnimation(LedRGB color = LedColors::White);
  void captureAnimation(int row, int col);
  void promotionAnimation(int col);
  void blinkSquare(int row, int col, LedRGB color, int times = 3, bool clearAfter = true, bool clearBefore = false);
  void showConnectingAnimation();
  void flashBoardAnimation(LedRGB color, int times = 3);

  // Start a cancellable animation. Returns a heap-allocated stop flag.
  // Caller owns the flag — must use stopAndWaitForAnimation() to cancel, wait for
  // completion, and free the flag. Never delete or set the flag directly.
  std::atomic<bool>* startThinkingAnimation();
  std::atomic<bool>* startWaitingAnimation();

  // Cancel a running cancellable animation: sets the stop flag, blocks until the
  // animation worker finishes and releases the LED mutex, then deletes the flag
  // and nulls the pointer. Safe to call with a null flag (no-op).
  void stopAndWaitForAnimation(std::atomic<bool>*& stopFlag);

  // Queue barrier: blocks the caller until all previously queued animations have
  // finished executing. Use before writing LEDs directly from the game loop to
  // prevent a stale queued animation from overwriting your changes.
  void waitForAnimationQueueDrain();

  // Board settings
  uint8_t getBrightness() const { return brightness; }
  uint8_t getDimMultiplier() const { return dimMultiplier; }
  void setBrightness(uint8_t value);
  void setDimMultiplier(uint8_t value);
  void saveLedSettings();
  void triggerCalibration();
};

#endif // BOARD_DRIVER_H

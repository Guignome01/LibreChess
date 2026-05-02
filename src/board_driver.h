#ifndef BOARD_DRIVER_H
#define BOARD_DRIVER_H

#include "led_colors.h"
#include <NeoPixelBusLg.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

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

// Animation job types for async queue
// SYNC is a no-op used as a queue barrier — waitForAnimationQueueDrain() enqueues it
// and blocks until the worker reaches it, guaranteeing all preceding animations are done.
enum class AnimationType : uint8_t { CAPTURE,
                                     PROMOTION,
                                     BLINK,
                                     WAITING,
                                     THINKING,
                                     FIREWORK,
                                     FLASH,
                                     SYNC };

// Animation job with parameters union for queue
struct AnimationJob {
  AnimationType type;
  std::atomic<bool>* stopFlag; // For cancellable animations
  union {
    struct {
      int row, col;
    } capture;
    struct {
      int col;
    } promotion;
    struct {
      int row, col;
      LedRGB color;
      int times;
      bool clearAfter;
      bool clearBefore;
    } blink;
    struct {
      LedRGB color;
      int times;
    } flash;
    struct {
      LedRGB color;
    } firework;
  } params;
};

// ---------------------------
// Board Driver Class
// Logical board coordinates: row 0 = rank 8, column 0 = file a
// ---------------------------
class BoardDriver {
 private:
  NeoPixelBusLg<NeoGrbFeature, NeoEsp32I2s0800KbpsMethod, NeoGammaNullMethod> strip;

  // Animation queue system
  static QueueHandle_t animationQueue;
  static TaskHandle_t animationTaskHandle;
  static SemaphoreHandle_t ledMutex;
  // Completion semaphore — signaled by the animation worker after finishing
  // a THINKING, WAITING, or SYNC job. Used by stopAndWaitForAnimation() and
  // waitForAnimationQueueDrain() to block until the animation is truly done.
  static SemaphoreHandle_t animationDoneSemaphore;
  static BoardDriver* instance;
  static void animationWorkerTask(void* param);
  void executeAnimation(const AnimationJob& job);
  void doCapture(int row, int col);
  void doPromotion(int col);
  void doBlink(int row, int col, LedRGB color, int times, bool clearAfter, bool clearBefore);
  void doWaiting(std::atomic<bool>* stopFlag);
  void doThinking(std::atomic<bool>* stopFlag);
  void doFirework(LedRGB color);
  void doFlash(LedRGB color, int times);
  bool sensorState[NUM_ROWS][NUM_COLS];
  bool sensorPrev[NUM_ROWS][NUM_COLS];
  bool sensorRaw[NUM_ROWS][NUM_COLS];
  unsigned long sensorDebounceTime[NUM_ROWS][NUM_COLS];
  int lastEnabledCol; // Tracks last enabled column for efficient sequential shifting

  enum class Axis : uint8_t {
    ROWS = 0,
    COLS = 1,
    UNKNOWN = 2,
  };
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

  bool loadCalibration();
  void saveCalibration();
  bool runCalibration();
  void loadLedSettings();
  void readRawSensors(bool rawState[NUM_ROWS][NUM_COLS]);
  bool waitForBoardEmpty(unsigned long stableMs = 500);
  bool waitForSingleRawPress(int& rawRow, int& rawCol, unsigned long stableMs = 500);
  void showCalibrationError();
  bool calibrateAxis(Axis axis, uint8_t* axisPinsOrder, size_t NUM_PINS, bool firstAxisSwapped);
  String axisToChessRankFile(Axis axis) const { return (axis == Axis::ROWS) ? "Rank" : ((axis == Axis::COLS) ? "File" : "Unknown"); };

  void loadShiftRegister(byte data, int bits = 8);
  void disableAllCols();
  void enableCol(int col);
  int getPixelIndex(int row, int col);

 public:
  BoardDriver();
  void begin();
  void readSensors();
  bool getSensorState(int row, int col);
  bool getSensorPrev(int row, int col);
  void updateSensorPrev();

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

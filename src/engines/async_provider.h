#ifndef ENGINES_ASYNC_PROVIDER_H
#define ENGINES_ASYNC_PROVIDER_H

#include "logger.h"
#include "provider.h"

#include <Arduino.h>
#include <atomic>

// Base context shared between the main loop and a FreeRTOS task.
// Derived providers add request-specific fields.
struct BaseTaskContext {
  virtual ~BaseTaskContext() = default;
  std::atomic<bool> cancel{false};
  std::atomic<bool> ready{false};
  EngineResult result;
  LibreChess::Log logger;
};

/// Firmware helper for providers that run one FreeRTOS background task.
class AsyncEngineProvider : public EngineProvider {
 public:
  explicit AsyncEngineProvider(LibreChess::ILogger* logger = nullptr) : logger_(logger) {}
  ~AsyncEngineProvider() override { cancelRequest(); }

  void cancelRequest() override {
    if (activeTask_) {
      activeTask_->cancel.store(true);
      unsigned long start = millis();
      while (!activeTask_->ready.load() && (millis() - start < 2000))
        delay(10);
      delete activeTask_;
      activeTask_ = nullptr;
    }
  }

 protected:
  LibreChess::Log logger_;
  BaseTaskContext* activeTask_ = nullptr;

  // Spawn a FreeRTOS task with the given context. Cancels any running task first.
  void spawnTask(BaseTaskContext* ctx, const char* name,
                 TaskFunction_t taskFn, uint32_t stackSize = 8192) {
    if (activeTask_) cancelRequest();
    ctx->logger = logger_;
    activeTask_ = ctx;
    if (xTaskCreate(taskFn, name, stackSize, ctx, 1, nullptr) != pdPASS) {
      logger_.error("AsyncEngineProvider: xTaskCreate failed (OOM?)");
      delete activeTask_;
      activeTask_ = nullptr;
      setImmediateResult(EngineResult{});
    } else {
      hasImmediateResult_ = false;
    }
  }

  // Poll for a completed result. Returns true and fills `result` when ready.
  // Deletes the task context — caller must do any post-processing before this.
  bool pollResult(EngineResult& result) {
    if (hasImmediateResult_) {
      result = immediateResult_;
      hasImmediateResult_ = false;
      return true;
    }
    if (!activeTask_ || !activeTask_->ready.load()) return false;
    result = activeTask_->result;
    delete activeTask_;
    activeTask_ = nullptr;
    return true;
  }

  // Like pollResult but does NOT delete the context, allowing the caller to
  // read provider-specific fields from the derived context first.
  // Caller MUST call finishTask() after reading extra fields.
  bool peekResult(EngineResult& result) {
    if (hasImmediateResult_) {
      result = immediateResult_;
      hasImmediateResult_ = false;
      return true;
    }
    if (!activeTask_ || !activeTask_->ready.load()) return false;
    result = activeTask_->result;
    return true;
  }

  // Delete the active task context after peekResult(). Must be called if
  // peekResult() returned true.
  void finishTask() {
    delete activeTask_;
    activeTask_ = nullptr;
  }

  // Publish a synchronous result when allocation or task creation fails
  // before a FreeRTOS task can own a context.
  void setImmediateResult(const EngineResult& result) {
    immediateResult_ = result;
    hasImmediateResult_ = true;
  }

 private:
  EngineResult immediateResult_;
  bool hasImmediateResult_ = false;
};

#endif  // ENGINES_ASYNC_PROVIDER_H

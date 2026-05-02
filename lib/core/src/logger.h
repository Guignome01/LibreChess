#ifndef LIBRECHESS_LOGGER_H
#define LIBRECHESS_LOGGER_H

#include <cstdarg>
#include <cstdio>

// Abstract logging interface — allows core library classes to emit diagnostic
// output without depending on Arduino Serial or any other concrete I/O.
namespace LibreChess {

class ILogger {
 public:
  virtual ~ILogger() = default;

  virtual void info(const char* message) = 0;
  virtual void error(const char* message) = 0;

  // Convenience formatted helpers (non-virtual).
  void infof(const char* fmt, ...) {
    char buf[192];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    info(buf);
  }

  void errorf(const char* fmt, ...) {
    char buf[192];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    error(buf);
  }
};

// Null-safe logger proxy — wraps ILogger* and guards every call.
// Skips formatting entirely when no logger is attached (zero wasted work).
// Use as a value member instead of ILogger* to eliminate null checks.
struct Log {
  ILogger* ptr;

  Log(ILogger* p = nullptr) : ptr(p) {}

  void info(const char* msg) {
    if (ptr) ptr->info(msg);
  }

  void error(const char* msg) {
    if (ptr) ptr->error(msg);
  }

  void infof(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
    if (!ptr) return;
    char buf[192];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    ptr->info(buf);
  }

  void errorf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
    if (!ptr) return;
    char buf[192];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    ptr->error(buf);
  }

  explicit operator bool() const { return ptr != nullptr; }
};

}  // namespace LibreChess

#endif  // CORE_LOGGER_H

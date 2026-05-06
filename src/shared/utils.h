#ifndef SHARED_UTILS_H
#define SHARED_UTILS_H

namespace SystemUtils {

/// Initialize NVS for ESP32 (required before Preferences.begin).
bool ensureNvsInitialized();

}  // namespace SystemUtils

#endif  // SHARED_UTILS_H

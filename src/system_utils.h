#ifndef SYSTEM_UTILS_H
#define SYSTEM_UTILS_H

namespace SystemUtils {

/// Initialize NVS for ESP32 (required before Preferences.begin).
bool ensureNvsInitialized();

} // namespace SystemUtils

#endif // SYSTEM_UTILS_H

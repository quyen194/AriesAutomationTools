#pragma once
#include <string>
#include <cstdint>

// Returns the platform-native virtual keycode for a named key string.
// Names are lowercase, e.g. "space", "enter", "f1", "a", "0", "ctrl", "shift".
// Returns 0 if the name is unknown.
uint32_t KeyNameToNative(const std::string& name);

// Returns the canonical lowercase name for a native keycode (for record mode).
std::string NativeToKeyName(uint32_t native_code);

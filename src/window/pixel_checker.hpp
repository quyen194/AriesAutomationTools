#pragma once
#include <cstdint>
#include <cstdlib>
#include <memory>

struct IPixelChecker {
    virtual ~IPixelChecker() = default;
    // returns packed 0xRRGGBB color at absolute screen position (x, y)
    virtual uint32_t GetPixelRGB(int x, int y) = 0;
};

// Utility: check if two colors match within a per-channel tolerance
inline bool ColorsMatch(uint32_t a, uint32_t b, int tolerance) {
    auto diff = [](uint8_t x, uint8_t y) -> int { return std::abs((int)x - (int)y); };
    return diff((a >> 16) & 0xFF, (b >> 16) & 0xFF) <= tolerance &&
           diff((a >>  8) & 0xFF, (b >>  8) & 0xFF) <= tolerance &&
           diff( a        & 0xFF,  b        & 0xFF) <= tolerance;
}

std::unique_ptr<IPixelChecker> CreatePixelChecker();

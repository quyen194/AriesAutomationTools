#pragma once
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <vector>

// A captured screen region: packed 0xRRGGBB pixels, row-major (top-left first).
struct PixelBuffer {
    int width  = 0;
    int height = 0;
    std::vector<uint32_t> pixels;

    bool Empty() const { return width <= 0 || height <= 0 || pixels.empty(); }
};

struct IPixelChecker {
    virtual ~IPixelChecker() = default;
    // returns packed 0xRRGGBB color at absolute screen position (x, y)
    virtual uint32_t GetPixelRGB(int x, int y) = 0;
    // captures a w x h screen region at absolute position (x, y);
    // returns an empty buffer on failure
    virtual PixelBuffer CaptureRegion(int x, int y, int w, int h) = 0;
    // captures the entire virtual screen; sets out_w/out_h; returns packed 0xRRGGBB row-major
    virtual std::vector<uint32_t> CaptureFullScreen(int& out_w, int& out_h) = 0;
};

// Utility: check if two colors match within a per-channel tolerance
inline bool ColorsMatch(uint32_t a, uint32_t b, int tolerance) {
    auto diff = [](uint8_t x, uint8_t y) -> int { return std::abs((int)x - (int)y); };
    return diff((a >> 16) & 0xFF, (b >> 16) & 0xFF) <= tolerance &&
           diff((a >>  8) & 0xFF, (b >>  8) & 0xFF) <= tolerance &&
           diff( a        & 0xFF,  b        & 0xFF) <= tolerance;
}

// Utility: percentage [0..100] of pixels in `current` that match `sample`
// within a per-channel tolerance. Buffers must have identical dimensions;
// returns 0 if they differ or are empty.
inline int BuffersMatchPercent(const PixelBuffer& sample, const PixelBuffer& current,
                               int tolerance) {
    if (sample.Empty() || current.Empty()) return 0;
    if (sample.width != current.width || sample.height != current.height) return 0;
    size_t total = sample.pixels.size();
    if (total == 0 || current.pixels.size() != total) return 0;
    size_t matched = 0;
    for (size_t i = 0; i < total; ++i)
        if (ColorsMatch(sample.pixels[i], current.pixels[i], tolerance)) ++matched;
    return (int)(matched * 100 / total);
}

std::unique_ptr<IPixelChecker> CreatePixelChecker();

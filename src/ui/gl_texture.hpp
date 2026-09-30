#pragma once
#include <cstdint>

// Small OpenGL texture helpers for ImGui::Image previews.
// A texture id of 0 means "no texture". Must be called with the GL context current
// (i.e. from the UI thread, between context creation and destruction).
namespace GlTexture {

// rgba: 4 bytes per pixel in R,G,B,A order
unsigned int CreateRGBA(const uint8_t* rgba, int w, int h);

// rgb: one uint32_t per pixel as 0x00RRGGBB (alpha is forced opaque)
unsigned int CreateFromRGB(const uint32_t* rgb, int w, int h);

// Deletes the texture (if any) and resets the id to 0
void Destroy(unsigned int& tex);

} // namespace GlTexture

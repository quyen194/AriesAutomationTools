#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#endif
#include "gl_texture.hpp"
#include <SDL_opengl.h>
#include <vector>

namespace GlTexture {

unsigned int CreateRGBA(const uint8_t* rgba, int w, int h) {
    if (!rgba || w <= 0 || h <= 0) return 0;
    GLuint tex = 0;
    glGenTextures(1, &tex);
    if (!tex) return 0;
    glBindTexture(GL_TEXTURE_2D, tex);
    // Nearest filtering: previews are small pixel samples scaled up
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

unsigned int CreateFromRGB(const uint32_t* rgb, int w, int h) {
    if (!rgb || w <= 0 || h <= 0) return 0;
    std::vector<uint8_t> rgba((size_t)w * h * 4);
    for (size_t i = 0; i < (size_t)w * h; ++i) {
        rgba[i*4 + 0] = (uint8_t)((rgb[i] >> 16) & 0xFF);
        rgba[i*4 + 1] = (uint8_t)((rgb[i] >>  8) & 0xFF);
        rgba[i*4 + 2] = (uint8_t)( rgb[i]        & 0xFF);
        rgba[i*4 + 3] = 0xFF;
    }
    return CreateRGBA(rgba.data(), w, h);
}

void Destroy(unsigned int& tex) {
    if (tex) {
        GLuint t = tex;
        glDeleteTextures(1, &t);
        tex = 0;
    }
}

} // namespace GlTexture

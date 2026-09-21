module;

#include "gldx/gmf.hpp"

#include <stb_image_write.h>

#include <cstring>

module gldx;

namespace gldx {

bool CaptureScreenshot(const std::string& path) {
    // Read whatever the last render pass left in the viewport: every pass sets
    // GL_VIEWPORT to the full default framebuffer, so this is the whole window at
    // its real (content-scaled) size, not the requested logical size.
    GLint vp[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, vp);
    const int width = vp[2];
    const int height = vp[3];
    if (width <= 0 || height <= 0) return false;

    // Tight row packing (default 4-byte alignment would pad odd widths), and read
    // the back buffer that was just drawn, before the swap makes it the front.
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);

    const std::size_t count = static_cast<std::size_t>(width) * height * 3;
    std::vector<std::uint8_t> rows(count);
    glGetError();  // clear any latched error so the check below is meaningful
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, rows.data());
    if (glGetError() != GL_NO_ERROR) return false;

    // GL readback is bottom-up; stb expects top-down rows. Flip in place-safe copy.
    std::vector<std::uint8_t> flipped(count);
    const std::size_t stride = static_cast<std::size_t>(width) * 3;
    for (int y = 0; y < height; ++y) {
        std::memcpy(&flipped[static_cast<std::size_t>(y) * stride],
                    &rows[static_cast<std::size_t>(height - 1 - y) * stride], stride);
    }

    return stbi_write_png(path.c_str(), width, height, 3, flipped.data(),
                          static_cast<int>(stride)) != 0;
}

} // namespace gldx

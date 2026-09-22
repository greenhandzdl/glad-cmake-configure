module;

#include "gldx/gmf.hpp"

#include <stb_image_write.h>

// Global-module declaration of the hook gldxwin links against; must be seen in
// the GMF here so the definition attaches to the global module, matching the
// declaration gldxwin includes in its own GMF (see ScreenshotHook.h).
#include "gldx/util/ScreenshotHook.h"

#include <cstring>

// EncodeScreenshot is defined IN the global module fragment (same attachment
// trick as the glad/glm entities gmf.hpp pulls in) because gldxwin links
// against the same GMF-declared hook from its own GMF; a purview definition
// would be read as a module-bound redeclaration and rejected. CaptureScreenshot
// below stays a normal module-gldx entity - window-bound capture now goes
// through gldx::win::Window::CaptureScreenshot in demos.
namespace gldx {
bool EncodeScreenshot(const std::string& path, int width, int height,
                      const unsigned char* rgbTopDown) {
    if (width <= 0 || height <= 0 || !rgbTopDown) return false;
    const int stride = width * 3;
    return stbi_write_png(path.c_str(), width, height, 3, rgbTopDown, stride) != 0;
}
} // namespace gldx

#include "gldx/util/Screenshot.h"

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

    return EncodeScreenshot(path, width, height, flipped.data());
}

} // namespace gldx

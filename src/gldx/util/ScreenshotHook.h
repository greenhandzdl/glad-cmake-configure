#ifndef GLDX_UTIL_SCREENSHOT_HOOK_H
#define GLDX_UTIL_SCREENSHOT_HOOK_H

/**
 * @file ScreenshotHook.h
 * @brief The one engine hook gldxwin is allowed to call, declared in the
 *        GLOBAL MODULE FRAGMENT of both libraries.
 *
 * Window::CaptureScreenshot (gldxwin) reads the pixels back itself but hands the
 * PNG encoding to gldx::EncodeScreenshot (Screenshot.cpp, stb_image_write), so
 * stb stays a private dependency of the engine. For that call to link, the
 * declaration must attach to the *global module* on BOTH sides: gldxwin's
 * gmf.hpp includes this header and gldx's Screenshot.cpp includes it in its own
 * global module fragment. Declaring it inside either module purview would
 * mangle the symbol with a module bound (@gldxwin / @gldx) and the linker would
 * not match the two halves.
 */

#include <string>

namespace gldx {

// Encode a top-down, tightly packed RGB row buffer (width*height*3 bytes) into
// `path` as PNG. Defined in gldx/util/Screenshot.cpp; returns false on a bad
// buffer or an unwritable file.
bool EncodeScreenshot(const std::string& path, int width, int height,
                      const unsigned char* rgbTopDown);

} // namespace gldx

#endif // GLDX_UTIL_SCREENSHOT_HOOK_H

#ifndef GLDX_UTIL_SCREENSHOT_H
#define GLDX_UTIL_SCREENSHOT_H

/**
 * @file Screenshot.h
 * @brief Read the current default framebuffer back to a PNG file.
 *
 * A small verification helper, not part of any render pass: it lets a demo (or a
 * headless regression sweep) prove that a scene actually produced pixels, rather
 * than only that the process exited cleanly. A blank window and a fully-rendered
 * one both return 0 from main(), so an exit-code check alone cannot tell them
 * apart; capturing the framebuffer can.
 *
 * Call it at the end of a frame, after the scene has been drawn into the default
 * framebuffer but before the buffers are swapped, while the window's GL context is
 * current. The read size is taken from the current GL_VIEWPORT, which every render
 * pass sets to the full framebuffer, so the image matches what is on screen at the
 * window's real (Retina-scaled) resolution.
 */

#include <string>

namespace gldx {

// Reads the back buffer of the default framebuffer and writes an RGB PNG to
// `path`. Returns false if the viewport is empty, the readback raised a GL error,
// or the file could not be written.
bool CaptureScreenshot(const std::string& path);

} // namespace gldx

#endif // GLDX_UTIL_SCREENSHOT_H

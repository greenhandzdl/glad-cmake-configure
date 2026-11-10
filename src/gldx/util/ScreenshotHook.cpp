// The gldx::EncodeScreenshot hook - a plain (non-module) textual TU.
//
// Why this is not part of Screenshot.cpp: gldxwin calls this function, and for
// the call to link the symbol must attach to the GLOBAL MODULE on both sides
// (see ScreenshotHook.h). A global module fragment may contain preprocessor
// directives only ([module.global.frag]; MSVC says so as C5202), so the
// definition cannot sit in Screenshot.cpp's GMF any more, and moving it into
// that file's module purview would give it a @gldx module bound that gldxwin's
// global-module declaration cannot see - the linker would not match the halves.
//
// A textual TU resolves it exactly the way third_party/stb_image_impl.cpp does
// for STB: it links into the gldx static library like any other object, defines
// a global-module symbol, and never leaks into the exported module surface.
// Do not convert this file to a module; do not move the definition back into
// Screenshot.cpp's GMF.

#include "gldx/util/ScreenshotHook.h"

#include <stb_image_write.h>

namespace gldx {

bool EncodeScreenshot(const std::string& path, int width, int height,
                      const unsigned char* rgbTopDown) {
    if (width <= 0 || height <= 0 || !rgbTopDown) return false;
    const int stride = width * 3;
    return stbi_write_png(path.c_str(), width, height, 3, rgbTopDown, stride) != 0;
}

} // namespace gldx

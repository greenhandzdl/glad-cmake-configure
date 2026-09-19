// stb implementation translation unit.
//
// This is the SINGLE translation unit that defines the STB implementation
// macros. Any other file may simply #include the stb headers (the include
// path is provided by the `stb` INTERFACE target) without defining the macros
// itself. Defining an implementation macro in more than one .cpp would cause
// duplicate-symbol errors at link time.
//
// It is deliberately a plain (non-module) textual TU: it lives under
// src/gfx/third_party/ to keep it inside the engine tree, but it is NOT part of
// the `gfx` module interface - STB never leaks into the exported surface.

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

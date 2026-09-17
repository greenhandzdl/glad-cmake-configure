// stb implementation file.
//
// This is the SINGLE translation unit that defines the STB implementation
// macros. Any other file may simply #include the stb headers (the include
// path is provided by the `stb` INTERFACE target) without defining the
// macros itself. Defining an implementation macro in more than one .cpp
// file would cause duplicate-symbol errors at link time.

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

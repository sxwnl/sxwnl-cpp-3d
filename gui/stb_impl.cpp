// Single translation unit that defines the STB image loader implementation.
// Including this here (instead of in renderer.cpp) keeps compile times fast.
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

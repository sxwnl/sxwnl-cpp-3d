// Browser-native async texture + binary fetch for the Emscripten build.
// Desktop / Android keep Renderer::loadTexFile (stb_image) and do not use this.
#pragma once

#include <string>

#ifdef __EMSCRIPTEN__

struct Texture {
    unsigned int id = 0;
    int width = 0;
    int height = 0;
    bool ready = false;
};

// Returns a placeholder immediately; the real image is decoded by the
// browser and uploaded to the same GL texture on a later turn.
Texture* TexRequest(const std::string& url);

// Fetch a binary file into MEMFS at memfsPath, then invoke cb(ok).
// Used for OBJ meshes and world_b.bin so EO can cache them per-file.
void FetchToMemfs(const std::string& url, const std::string& memfsPath,
                  void (*cb)(bool ok, void* user), void* user);

#endif

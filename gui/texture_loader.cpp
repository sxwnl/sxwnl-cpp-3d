#ifdef __EMSCRIPTEN__

#include "texture_loader.h"

#include "gles/gl_compat.h"
#include <emscripten.h>

#include <cstdio>
#include <unordered_map>
#include <vector>

static std::unordered_map<unsigned int, Texture> g_textures;

struct FileWait {
    void (*cb)(bool, void*) = nullptr;
    void* user = nullptr;
};
static std::vector<FileWait> g_files;

extern "C" EMSCRIPTEN_KEEPALIVE
void sxwnl_on_texture_loaded(int texId, int w, int h) {
    auto it = g_textures.find((unsigned int)texId);
    if (it == g_textures.end()) return;
    it->second.width = w;
    it->second.height = h;
    it->second.ready = true;
    std::fprintf(stderr, "[tex] ready id=%d %dx%d\n", texId, w, h);
}

extern "C" EMSCRIPTEN_KEEPALIVE
void sxwnl_on_file_loaded(int reqId, int ok) {
    if (reqId < 0 || reqId >= (int)g_files.size()) return;
    FileWait w = g_files[(size_t)reqId];
    g_files[(size_t)reqId] = FileWait{};
    if (w.cb) w.cb(ok != 0, w.user);
}

// fetch() + createImageBitmap: the browser decodes JPEG/PNG on its own
// thread and texImage2D uploads from the bitmap, so wasm never holds a
// decoded RGBA copy. GL.textures[id] is the same object glGenTextures
// returned, so the C++ sampler can bind the id before the image arrives.
EM_JS(void, js_fetch_texture, (const char* url, int texId, int flipY), {
    const page = (function() {
        const p = window.location.pathname;
        return p.endsWith('/') ? p : p.slice(0, p.lastIndexOf('/') + 1);
    })();
    // url must be relative (resources/v1/...), never "/resources/...":
    // the project Pages site lives under /sxwnl-cpp-3d/.
    const u = page + UTF8ToString(url);
    fetch(u)
      .then(r => { if (!r.ok) throw new Error(u + ' ' + r.status); return r.blob(); })
      .then(b => createImageBitmap(b, {imageOrientation: flipY ? 'flipY' : 'none'}))
      .then(bmp => {
          const gl = GL.currentContext.GLctx;
          const prev = gl.getParameter(gl.TEXTURE_BINDING_2D);
          gl.bindTexture(gl.TEXTURE_2D, GL.textures[texId]);
          gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, bmp);
          gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR_MIPMAP_LINEAR);
          gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
          gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.REPEAT);
          gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
          gl.generateMipmap(gl.TEXTURE_2D);
          gl.bindTexture(gl.TEXTURE_2D, prev);
          const w = bmp.width, h = bmp.height;
          bmp.close();
          Module.ccall('sxwnl_on_texture_loaded', null,
                       ['number', 'number', 'number'], [texId, w, h]);
      })
      .catch(e => console.error('[tex]', e));
});

EM_JS(void, js_fetch_to_memfs, (const char* url, const char* path, int reqId), {
    const page = (function() {
        const p = window.location.pathname;
        return p.endsWith('/') ? p : p.slice(0, p.lastIndexOf('/') + 1);
    })();
    const u = page + UTF8ToString(url);
    const mem = UTF8ToString(path);
    fetch(u)
      .then(r => { if (!r.ok) throw new Error(u + ' ' + r.status); return r.arrayBuffer(); })
      .then(buf => {
          const slash = mem.lastIndexOf('/');
          if (slash > 0) FS.mkdirTree(mem.slice(0, slash));
          FS.writeFile(mem, new Uint8Array(buf));
          Module.ccall('sxwnl_on_file_loaded', null,
                       ['number', 'number'], [reqId, 1]);
      })
      .catch(e => {
          console.error('[fs]', e);
          Module.ccall('sxwnl_on_file_loaded', null,
                       ['number', 'number'], [reqId, 0]);
      });
});

Texture* TexRequest(const std::string& url) {
    static std::unordered_map<std::string, unsigned int> cache;
    auto c = cache.find(url);
    if (c != cache.end()) return &g_textures[c->second];

    unsigned int tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    const unsigned char px[4] = {32, 32, 40, 255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    cache[url] = tex;
    g_textures[tex] = Texture{tex, 1, 1, false};
    js_fetch_texture(url.c_str(), (int)tex, 1);
    return &g_textures[tex];
}

void FetchToMemfs(const std::string& url, const std::string& memfsPath,
                  void (*cb)(bool, void*), void* user) {
    int id = (int)g_files.size();
    g_files.push_back(FileWait{cb, user});
    js_fetch_to_memfs(url.c_str(), memfsPath.c_str(), id);
}

#endif

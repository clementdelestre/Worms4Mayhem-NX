#include "models.h"
#include "raymath.h"
#include "rlgl.h"
#include <cstring>
#include <map>
#include <string>

#ifdef __SWITCH__
#define MODEL_DIR "sdmc:/switch/worms4nx/assets/models"
#else
#define MODEL_DIR "./assets/models"
#endif

namespace {
struct Entry { Model m; ModelAnimation *anims = nullptr; int count = 0; const ModelAnimation *posed = nullptr; int frame = -1; };
std::map<std::string, Entry> models;
Shader shader{};

// Textured + one directional light; alpha-tested for the teeth/eye overlays. GLSL 100, macro-wrapped for 330.
const char *VS = R"(
attribute vec3 vertexPosition;
attribute vec2 vertexTexCoord;
attribute vec3 vertexNormal;
uniform mat4 mvp;
uniform mat4 matNormal;
varying vec2 uv;
varying vec3 n;
void main() { uv = vertexTexCoord; n = (matNormal * vec4(vertexNormal, 0.0)).xyz; gl_Position = mvp * vec4(vertexPosition, 1.0); }
)";
const char *FS = R"(
uniform sampler2D texture0;
uniform vec4 colDiffuse;
varying vec2 uv;
varying vec3 n;
void main() {
    vec4 c = texture2D(texture0, uv) * colDiffuse;
    if (c.a < 0.5) discard;
    float l = 0.6 + 0.4 * max(dot(normalize(n), normalize(vec3(0.4, 1.0, 0.3))), 0.0);
    gl_FragColor = vec4(c.rgb * l, 1.0);
}
)";
}

void Models::load() {
    if (!DirectoryExists(MODEL_DIR)) return;
    bool es = rlGetVersion() == RL_OPENGL_ES_20 || rlGetVersion() == RL_OPENGL_ES_30;
    std::string vs = es ? "#version 100\n" : "#version 330\n#define attribute in\n#define varying out\n";
    std::string fs = es ? "#version 100\nprecision mediump float;\n" : "#version 330\n#define varying in\n#define texture2D texture\n#define gl_FragColor fragColor\nout vec4 fragColor;\n";
    shader = LoadShaderFromMemory((vs + VS).c_str(), (fs + FS).c_str());
    FilePathList files = LoadDirectoryFilesEx(MODEL_DIR, ".glb", false);
    for (unsigned i = 0; i < files.count; i++) {
        Entry e;
        e.m = LoadModel(files.paths[i]);
        if (!e.m.meshCount) continue;
        for (int k = 0; k < e.m.materialCount; k++) {
            if (shader.id != rlGetShaderIdDefault()) e.m.materials[k].shader = shader;
            Texture2D &t = e.m.materials[k].maps[MATERIAL_MAP_ALBEDO].texture;
            if (t.id != rlGetTextureIdDefault()) { GenTextureMipmaps(&t); SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR); }
        }
        if (e.m.skeleton.boneCount) e.anims = LoadModelAnimations(files.paths[i], &e.count);
        models[GetFileNameWithoutExt(files.paths[i])] = e;
    }
    UnloadDirectoryFiles(files);
    TraceLog(LOG_INFO, "MODELS: %d loaded from %s", (int)models.size(), MODEL_DIR);
}

void Models::unload() {
    for (auto &[name, e] : models) {
        UnloadModelAnimations(e.anims, e.count);
        UnloadModel(e.m);
    }
    models.clear();
    if (shader.id) UnloadShader(shader);
}

static const ModelAnimation *find(const Entry &e, const char *clip) {
    for (int i = 0; clip && i < e.count; i++) if (!strcmp(e.anims[i].name, clip)) return &e.anims[i];
    return nullptr;
}

float Models::clipLength(const char *name, const char *clip) {
    auto it = models.find(name);
    const ModelAnimation *a = it == models.end() ? nullptr : find(it->second, clip);
    return a ? (a->keyframeCount - 1) / 60.0f : 0;  // raylib samples glTF clips at 60 fps
}

bool Models::draw(const char *name, Vector3 pos, float yaw, float pitch, Color tint, const char *clip, float t, bool loop) {
    auto it = models.find(name);
    if (it == models.end()) return false;
    Entry &e = it->second;
    // skinned meshes are shared: pose them right before each draw (CPU skinning), unless already in that pose
    const ModelAnimation *a = find(e, clip);
    if (!a && e.count) a = &e.anims[0];
    if (a) {
        int last = a->keyframeCount - 1, f = (int)(t * 60);
        f = loop && last > 0 ? f % last : (int)Clamp(f, 0, last);
        if (a != e.posed || f != e.frame) UpdateModelAnimation(e.m, *a, f);
        e.posed = a, e.frame = f;
    }
    e.m.transform = MatrixMultiply(MatrixRotateX(-pitch), MatrixRotateY(yaw));
    DrawModel(e.m, pos, 1, tint);
    return true;
}

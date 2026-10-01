#include "models.h"
#include "lit.h"
#include "raymath.h"
#include "rlgl.h"
#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#ifdef __SWITCH__
#define MODEL_DIR "sdmc:/switch/worms4nx/assets/models"
#else
#define MODEL_DIR "./assets/models"
#endif

namespace {
Matrix trs(const Transform &p) {
    return MatrixMultiply(MatrixMultiply(MatrixScale(p.scale.x, p.scale.y, p.scale.z), QuaternionToMatrix(p.rotation)),
                          MatrixTranslate(p.translation.x, p.translation.y, p.translation.z));
}
struct Entry { Model m; ModelAnimation *anims = nullptr; int count = 0; const ModelAnimation *posed = nullptr; int frame = -1; std::vector<Matrix> invBind; };
std::map<std::string, Entry> models;
std::vector<std::string> hatNames;
Shader shader{};
}

void Models::load() {
    if (!DirectoryExists(MODEL_DIR)) return;
    shader = Lit::modelShader(true);  // textured, alpha-tested (teeth/eye overlays), W4M worm light
    FilePathList files = LoadDirectoryFilesEx(MODEL_DIR, ".glb", true);  // recurses into hats/
    for (unsigned i = 0; i < files.count; i++) {
        if (strstr(files.paths[i], "/frontend/")) continue;  // FrontBg loads (and frees) its own scene
        Entry e;
        e.m = LoadModel(files.paths[i]);
        if (!e.m.meshCount) continue;
        for (int k = 0; k < e.m.materialCount; k++) {
            if (shader.id != rlGetShaderIdDefault()) e.m.materials[k].shader = shader;
            Texture2D &t = e.m.materials[k].maps[MATERIAL_MAP_ALBEDO].texture;
            if (t.id != rlGetTextureIdDefault()) { GenTextureMipmaps(&t); SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR); }
        }
        if (e.m.skeleton.boneCount) e.anims = LoadModelAnimations(files.paths[i], &e.count);
        for (int b = 0; b < e.m.skeleton.boneCount; b++) e.invBind.push_back(MatrixInvert(trs(e.m.skeleton.bindPose[b])));
        std::string name = GetFileNameWithoutExt(files.paths[i]);
        if (strstr(files.paths[i], "/hats/")) hatNames.push_back(name);
        models[name] = e;
    }
    std::sort(hatNames.begin(), hatNames.end());  // same file set on every client -> same order
    UnloadDirectoryFiles(files);
    TraceLog(LOG_INFO, "MODELS: %d loaded from %s (%d hats)", (int)models.size(), MODEL_DIR, (int)hatNames.size());
}

void Models::unload() {
    for (auto &[name, e] : models) {
        UnloadModelAnimations(e.anims, e.count);
        UnloadModel(e.m);
    }
    models.clear();
    hatNames.clear();
    if (shader.id) UnloadShader(shader);
}

int Models::hatCount() { return (int)hatNames.size(); }
const char *Models::hatName(int i) { return i >= 0 && i < (int)hatNames.size() ? hatNames[i].c_str() : ""; }

static const ModelAnimation *find(const Entry &e, const char *clip) {
    for (int i = 0; clip && i < e.count; i++) if (!strcmp(e.anims[i].name, clip)) return &e.anims[i];
    return nullptr;
}

bool Models::has(const char *name) { return models.count(name) > 0; }

float Models::clipLength(const char *name, const char *clip) {
    auto it = models.find(name);
    const ModelAnimation *a = it == models.end() ? nullptr : find(it->second, clip);
    return a ? (a->keyframeCount - 1) / 60.0f : 0;  // raylib samples glTF clips at 60 fps
}

static const ModelAnimation *clipFrame(const Entry &e, const char *clip, float t, bool loop, int *f) {
    const ModelAnimation *a = find(e, clip);
    if (!a && e.count) a = &e.anims[0];
    if (!a) return nullptr;
    int last = a->keyframeCount - 1;
    *f = (int)(t * 60);
    *f = loop && last > 0 ? *f % last : (int)Clamp(*f, 0, last);
    return a;
}

bool Models::joint(const char *name, const char *joint, const char *clip, float t, bool loop, Matrix *out) {
    auto it = models.find(name);
    if (it == models.end()) return false;
    const Entry &e = it->second;
    int f, b = 0;
    const ModelAnimation *a = clipFrame(e, clip, t, loop, &f);
    int n = (int)e.m.skeleton.boneCount;
    while (b < n && strcmp(e.m.skeleton.bones[b].name, joint)) b++;
    if (!a || b == n || b >= (int)a->boneCount) return false;
    *out = trs(a->keyframePoses[f][b]);
    return true;
}

// UpdateModelAnimation() equivalent at an integer frame; raylib inverts a bone matrix per vertex for the normals
static void skin(Entry &e, const ModelAnimation &a, int f) {
    Model &m = e.m;
    int n = std::min(m.skeleton.boneCount, a.boneCount);
    static std::vector<Matrix> nm;
    nm.resize(m.skeleton.boneCount);
    for (int b = 0; b < n; b++) {
        m.boneMatrices[b] = MatrixMultiply(e.invBind[b], trs(a.keyframePoses[f][b]));
        nm[b] = MatrixTranspose(MatrixInvert(m.boneMatrices[b]));
    }
    for (int i = 0; i < m.meshCount; i++) {
        Mesh &me = m.meshes[i];
        if (!me.boneWeights || !me.boneIndices || !me.animVertices || !me.animNormals) continue;
        for (int v = 0; v < me.vertexCount; v++) {
            Vector3 p = {me.vertices[3 * v], me.vertices[3 * v + 1], me.vertices[3 * v + 2]}, op = {}, on = {};
            Vector3 nr = me.normals ? Vector3{me.normals[3 * v], me.normals[3 * v + 1], me.normals[3 * v + 2]} : Vector3{};
            for (int j = 0; j < 4; j++) {
                float w = me.boneWeights[4 * v + j];
                if (w == 0) continue;
                int b = me.boneIndices[4 * v + j];
                op = Vector3Add(op, Vector3Scale(Vector3Transform(p, m.boneMatrices[b]), w));
                on = Vector3Add(on, Vector3Scale(Vector3Transform(nr, nm[b]), w));
            }
            memcpy(&me.animVertices[3 * v], &op, sizeof op);
            memcpy(&me.animNormals[3 * v], &on, sizeof on);
        }
        rlUpdateVertexBuffer(me.vboId[SHADER_LOC_VERTEX_POSITION], me.animVertices, me.vertexCount * 3 * sizeof(float), 0);
        if (me.normals) rlUpdateVertexBuffer(me.vboId[SHADER_LOC_VERTEX_NORMAL], me.animNormals, me.vertexCount * 3 * sizeof(float), 0);
    }
}

bool Models::visible(Vector3 c, float r) {
    Matrix m = MatrixMultiply(rlGetMatrixModelview(), rlGetMatrixProjection());
    Vector4 w = {m.m3, m.m7, m.m11, m.m15}, rows[3] = {{m.m0, m.m4, m.m8, m.m12}, {m.m1, m.m5, m.m9, m.m13}, {m.m2, m.m6, m.m10, m.m14}};
    for (Vector4 q : rows)
        for (float s : {1.0f, -1.0f}) {  // clip planes row3 + row, row3 - row
            Vector3 n = {w.x + s * q.x, w.y + s * q.y, w.z + s * q.z};
            if (Vector3DotProduct(n, c) + w.w + s * q.w < -r * Vector3Length(n)) return false;
        }
    return true;
}

bool Models::draw(const char *name, Matrix m, Color tint) {
    auto it = models.find(name);
    if (it == models.end()) return false;
    it->second.m.transform = m;
    DrawModel(it->second.m, {0, 0, 0}, 1, tint);
    return true;
}

bool Models::draw(const char *name, Vector3 pos, float yaw, float pitch, Color tint, const char *clip, float t, bool loop) {
    auto it = models.find(name);
    if (it == models.end()) return false;
    Entry &e = it->second;
    // skinned meshes are shared: pose them right before each draw (CPU skinning), unless already in that pose
    int f;
    if (const ModelAnimation *a = clipFrame(e, clip, t, loop, &f)) {
        if ((a != e.posed || f != e.frame) && e.m.boneMatrices && a->keyframeCount > 0) skin(e, *a, f);
        e.posed = a, e.frame = f;
    }
    e.m.transform = MatrixMultiply(MatrixRotateX(-pitch), MatrixRotateY(yaw));
    DrawModel(e.m, pos, 1, tint);
    return true;
}

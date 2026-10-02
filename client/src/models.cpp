#include "models.h"
#include "lit.h"
#include "raymath.h"
#include "rlgl.h"
#include "external/cgltf.h"
#include <algorithm>
#include <atomic>
#include <deque>
#include <mutex>
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
struct Entry {
    Model m; ModelAnimation *anims = nullptr; int count = 0; const ModelAnimation *posed = nullptr, *aimed = nullptr; int frame = -1, aimFrame = -1;
    std::vector<Matrix> invBind;
    std::vector<int> arm;  // per bone: its shoulder bone, -1 off the arms (the glb skeleton is flat: matched by name)
    std::vector<uint8_t> face;  // per bone: 1 lips, eyelid or eyebrow (W4M emote bones), 2 turns with the head
    int head = -1, hat = -1;
    std::vector<uint64_t> owns;  // per clip: the face bones it moves itself, which an emote layer leaves to it
    Models::Layers lay{}; bool layered = false;  // the Layers of the last skin()
};
std::map<std::string, Entry> models;

// Per clip, the face bones whose offset from the head differs from Base's: W4M channels a gesture animates win over the emote's
void owners(Entry &e) {
    const ModelAnimation *base = nullptr;
    for (int i = 0; i < e.count; i++) if (!strcmp(e.anims[i].name, "Base")) base = &e.anims[i];
    e.owns.assign(e.count, 0);
    int h = e.head, n = std::min((int)e.face.size(), 64);
    if (!base || h < 0) return;
    auto rel = [&](const ModelAnimation &a, int f, int b) { return MatrixMultiply(trs(a.keyframePoses[f][b]), MatrixInvert(trs(a.keyframePoses[f][h]))); };
    for (int i = 0; i < e.count; i++) {
        const ModelAnimation &a = e.anims[i];
        for (int b = 0; b < n && b < (int)a.boneCount && h < (int)a.boneCount; b++) {
            if (!(e.face[b] & 1)) continue;
            Matrix r0 = rel(*base, 0, b);
            for (int f = 0; f < a.keyframeCount && !(e.owns[i] >> b & 1); f += 4) {
                Matrix r = rel(a, f, b);
                const float *p = &r.m0, *q = &r0.m0;
                for (int k = 0; k < 16; k++) if (fabsf(p[k] - q[k]) > 2e-3f) { e.owns[i] |= 1ull << b; break; }
            }
        }
    }
}
std::vector<std::string> hatNames;
Shader shader{};

// Boot: the worker reads each .glb, decodes its textures and samples its clips; the main thread only uploads.
struct Job {
    std::string path;
    unsigned char *data = nullptr;  // the file, trimmed (blank()) so LoadModel() skips the texture decodes and the clips
    int size = 0;
    std::vector<Image> albedo;  // per raylib material (0 = default), mipmapped
    ModelAnimation *anims = nullptr;
    int count = 0;
    Model m{};
    int next = -1;  // albedo to upload next, -1 before LoadModel()
};
std::mutex mu;
std::deque<Job> jobs;
std::atomic<bool> prepared{false};
std::map<std::string, Model> spare;  // decoded on the worker but not drawn here (frontend/): take()
thread_local Job *serve = nullptr;

// LoadFileData() hook: gives LoadModel() the job's bytes (raylib frees them), plain file read otherwise
unsigned char *readFile(const char *path, int *size) {
    *size = 0;
    if (serve && serve->path == path) {
        unsigned char *d = serve->data;
        serve->data = nullptr;
        return *size = serve->size, d;
    }
    FILE *f = fopen(path, "rb");
    if (!f) return TraceLog(LOG_WARNING, "FILEIO: [%s] Failed to open file", path), nullptr;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *d = n > 0 ? (unsigned char *)malloc(n) : nullptr;
    if (d && fread(d, 1, n, f) == (size_t)n) *size = (int)n;
    else free(d), d = nullptr;
    fclose(f);
    return d;
}
const bool hooked = (SetLoadFileDataCallback(readFile), true);

// JSON chunk of the glb (after its 12-byte header and the chunk's own 8)
std::pair<char *, char *> json(Job &j) {
    uint32_t len = 0;
    memcpy(&len, j.data + 12, 4);
    return {(char *)j.data + 20, (char *)j.data + 20 + std::min<size_t>(len, j.size - 20)};
}

// Spaces out the elements of the top-level array `key` from index keep on: same length, so every offset holds
void blank(Job &j, const char *key, size_t keep) {
    auto [js, end] = json(j);
    std::string k = std::string("\"") + key + "\":[";
    char *p = std::search(js, end, k.begin(), k.end());
    if (p == end) return;
    p += k.size();
    char *from = keep ? nullptr : p;
    for (int depth = 0, n = 0; p < end; p++) {
        if (*p == '"') {
            while (++p < end && *p != '"') p += *p == '\\';
        } else if (*p == '{' || *p == '[') {
            depth++;
        } else if (*p == '}' || *p == ']') {
            if (depth-- == 0) break;
        } else if (*p == ',' && !depth && ++n == (int)keep) {
            from = p;
        }
    }
    if (from && p < end) memset(from, ' ', p - from);
}

// Drops the JSON's blanks (outside strings) and closes the gap: buffer view offsets count from the BIN chunk
void compact(Job &j) {
    auto [js, end] = json(j);
    char *w = js;
    bool str = false;
    for (char *p = js; p < end; p++) {
        if (*p == ' ' && !str) continue;
        if (str && *p == '\\' && p + 1 < end) *w++ = *p++;
        else if (*p == '"') str = !str;
        *w++ = *p;
    }
    while ((w - js) % 4) *w++ = ' ';  // chunks stay 4-byte aligned
    uint32_t len = w - js;
    memcpy(j.data + 12, &len, 4);
    memmove(w, end, (char *)j.data + j.size - end);
    j.size -= end - w;
    memcpy(j.data + 8, &j.size, 4);
}

Job prepare(const char *path) {
    Job j{path};
    j.data = readFile(path, &j.size);
    cgltf_options o{};
    cgltf_data *g = nullptr;
    if (!j.data || j.size < 20 || cgltf_parse(&o, j.data, j.size, &g) != cgltf_result_success || g->file_type != cgltf_file_type_glb ||
        cgltf_load_buffers(&o, g, path) != cgltf_result_success) {
        if (g) cgltf_free(g);
        return j;
    }
    j.albedo.resize(g->materials_count + 1);
    for (size_t i = 0; i < g->materials_count; i++) {
        cgltf_texture *t = g->materials[i].pbr_metallic_roughness.base_color_texture.texture;
        cgltf_buffer_view *v = t && t->image ? t->image->buffer_view : nullptr;
        if (!v || !v->buffer->data) continue;
        j.albedo[i + 1] = LoadImageFromMemory(".png", (unsigned char *)v->buffer->data + v->offset, (int)v->size);
        ImageMipmaps(&j.albedo[i + 1]);  // here rather than GenTextureMipmaps(): no GPU blits on the main thread
    }
    size_t acc = 0, views = 0;  // accessors / buffer views LoadModel() reads: meshes and skins (the clips' come after)
    auto use = [&](const cgltf_accessor *a) { if (a) acc = std::max(acc, (size_t)(a - g->accessors) + 1); };
    for (size_t i = 0; i < g->meshes_count; i++)
        for (size_t k = 0; k < g->meshes[i].primitives_count; k++) {
            const cgltf_primitive &pr = g->meshes[i].primitives[k];
            use(pr.indices);
            for (size_t a = 0; a < pr.attributes_count; a++) use(pr.attributes[a].data);
            for (size_t t = 0; t < pr.targets_count; t++)
                for (size_t a = 0; a < pr.targets[t].attributes_count; a++) use(pr.targets[t].attributes[a].data);
        }
    for (size_t i = 0; i < g->skins_count; i++) use(g->skins[i].inverse_bind_matrices);
    auto view = [&](const cgltf_buffer_view *v) { if (v) views = std::max(views, (size_t)(v - g->buffer_views) + 1); };
    for (size_t i = 0; i < acc; i++)
        view(g->accessors[i].buffer_view), view(g->accessors[i].sparse.indices_buffer_view), view(g->accessors[i].sparse.values_buffer_view);
    for (size_t i = 0; i < g->images_count; i++) view(g->images[i].buffer_view);  // still parsed
    bool skinned = g->skins_count > 0;
    cgltf_free(g);
    auto [js, end] = json(j);
    const char *tag = "\"baseColorTexture\"";
    for (char *p = js; (p = std::search(p, end, tag, tag + 18)) != end; p++) p[16] = 'X';  // a key cgltf ignores: no decode
    if (skinned) j.anims = LoadModelAnimations(path, &j.count);  // reads the file again: it needs the clips blank() drops
    blank(j, "animations", 0), blank(j, "accessors", acc), blank(j, "bufferViews", views), compact(j);
    return j;
}

// One step of a job's GPU side (LoadModel(), then a texture); true once j.m is complete
bool uploadStep(Job &j) {
    if (j.next < 0) {
        serve = &j;
        j.m = LoadModel(j.path.c_str());
        serve = nullptr;
        free(j.data), j.data = nullptr, j.next = 0;  // null unless LoadModel() failed before reading it
        return false;
    }
    for (; j.next < (int)j.albedo.size(); j.next++) {
        Image &img = j.albedo[j.next];
        if (!img.data) continue;
        if (j.next < j.m.materialCount) {
            Texture2D &t = j.m.materials[j.next].maps[MATERIAL_MAP_ALBEDO].texture;
            t = LoadTextureFromImage(img);
            SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
        }
        UnloadImage(img), img = {};
        return j.next++, false;
    }
    return true;
}

void add(Job &j) {
    Entry e;
    e.m = j.m;
    if (!e.m.meshCount) return UnloadModelAnimations(j.anims, j.count);
    if (strstr(j.path.c_str(), "/frontend/")) return (void)(spare[j.path] = e.m);  // FrontBg's scene, freed by FrontBg
    for (int k = 0; k < e.m.materialCount; k++)
        if (shader.id != rlGetShaderIdDefault()) e.m.materials[k].shader = shader;
    e.anims = j.anims, e.count = j.count;
    for (int b = 0; b < e.m.skeleton.boneCount; b++) e.invBind.push_back(MatrixInvert(trs(e.m.skeleton.bindPose[b])));
    for (int b = 0, s[2] = {-1, -1}; b < (int)e.m.skeleton.boneCount; b++) {
        const char *n = e.m.skeleton.bones[b].name;
        bool left = strstr(n, "_left"), hand = !strcmp(n, "WeaponLocator");
        for (const char *p : {"wrist_", "pinky", "index", "fore", "thumb_"}) hand |= !strncmp(n, p, strlen(p));
        if (!strncmp(n, "shoulder_", 9)) s[left] = b;
        e.arm.push_back(!strncmp(n, "shoulder_", 9) || hand ? s[left] : -1);  // bones follow their shoulder in the file
        bool face = false;
        for (const char *p : {"upperlip", "bottomlip", "mouthmiddle", "eyetop", "eyebrow"}) face |= !strncmp(n, p, strlen(p));
        e.face.push_back(face ? 3 : !strcmp(n, "head_bone") || !strcmp(n, "HatLocator") ? 2 : 0);  // XBone names: <group>_bone
        if (!strcmp(n, "head_bone")) e.head = b;
        if (!strcmp(n, "HatLocator")) e.hat = b;
    }
    owners(e);
    std::string name = GetFileNameWithoutExt(j.path.c_str());
    if (strstr(j.path.c_str(), "/hats/")) hatNames.push_back(name);
    models[name] = e;
}
}  // namespace

void Models::prepare() {
    if (DirectoryExists(MODEL_DIR)) {
        FilePathList files = LoadDirectoryFilesEx(MODEL_DIR, ".glb", true);  // recurses into hats/ and frontend/
        for (unsigned i = 0; i < files.count; i++) {
            Job j = ::prepare(files.paths[i]);
            std::lock_guard<std::mutex> l(mu);
            jobs.push_back(std::move(j));
        }
        UnloadDirectoryFiles(files);
    }
    prepared = true;
}

bool Models::upload(double until) {
    if (!shader.id) shader = Lit::modelShader(true);  // textured, alpha-tested (teeth/eye overlays), W4M worm light
    static Job cur;
    static bool busy = false;
    for (bool done = prepared; GetTime() < until;) {
        if (!busy) {
            std::lock_guard<std::mutex> l(mu);
            if (jobs.empty()) {
                if (!done) return true;
                std::sort(hatNames.begin(), hatNames.end());  // same file set on every client -> same order
                TraceLog(LOG_INFO, "MODELS: %d loaded from %s (%d hats)", (int)models.size(), MODEL_DIR, (int)hatNames.size());
                return false;
            }
            cur = std::move(jobs.front()), jobs.pop_front(), busy = true;
        }
        if (uploadStep(cur)) add(cur), busy = false;
    }
    return true;
}

Model Models::take(const char *path) {
    auto it = spare.find(path);
    if (it == spare.end()) {
        Job j = ::prepare(path);
        while (!uploadStep(j)) {}
        return j.m;
    }
    Model m = it->second;
    spare.erase(it);
    return m;
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
float Models::bottom(const char *name) {
    auto it = models.find(name);
    return it == models.end() ? 0 : -GetModelBoundingBox(it->second.m).min.y;
}

float Models::clipLength(const char *name, const char *clip) {
    auto it = models.find(name);
    const ModelAnimation *a = it == models.end() ? nullptr : find(it->second, clip);
    return a ? (a->keyframeCount - 1) / 60.0f : 0;  // raylib samples glTF clips at 60 fps
}

static const ModelAnimation *clipFrame(const Entry &e, const char *clip, float t, bool loop, int *f, bool fallback = true) {
    const ModelAnimation *a = find(e, clip);
    if (!a && e.count && fallback) a = &e.anims[0];
    if (!a) return nullptr;
    int last = a->keyframeCount - 1;
    *f = (int)(t * 60);
    *f = loop && last > 0 ? *f % last : (int)Clamp(*f, 0, last);
    return a;
}

// Model-space matrix of bone b; aim (frame af): an arm bone keeps its offset from its shoulder, which takes aim's pose
static Matrix bone(const Entry &e, const ModelAnimation &a, int f, int b, const ModelAnimation *aim, int af) {
    int s = aim && b < (int)e.arm.size() ? e.arm[b] : -1;
    if (s < 0 || s >= (int)aim->boneCount) return trs(a.keyframePoses[f][b]);
    return MatrixMultiply(MatrixMultiply(trs(a.keyframePoses[f][b]), MatrixInvert(trs(a.keyframePoses[f][s]))), trs(aim->keyframePoses[af][s]));
}

// About pivot p: rotate by yaw about y, after pitch about x (frame axes x, y)
static Matrix turn(Vector3 p, Vector3 x, Vector3 y, float yaw, float pitch) {
    Matrix r = MatrixMultiply(MatrixRotate(x, -pitch), MatrixRotate(y, yaw));  // + pitch raises the face (checked on animshot)
    return MatrixMultiply(MatrixMultiply(MatrixTranslate(-p.x, -p.y, -p.z), r), MatrixTranslate(p.x, p.y, p.z));
}

// Every bone's model-space matrix, with the W4M pose layers over the clip
static void pose(const Entry &e, const ModelAnimation &a, int f, const ModelAnimation *aim, int af, const Models::Layers *ly, std::vector<Matrix> &out) {
    int n = std::min(e.m.skeleton.boneCount, a.boneCount);
    out.resize(n);
    for (int b = 0; b < n; b++) out[b] = bone(e, a, f, b, aim, af);
    if (!ly || e.head < 0 || e.head >= n || e.hat < 0 || e.hat >= n) return;
    int ff;
    const ModelAnimation *em = ly->face ? clipFrame(e, ly->face, ly->faceT, true, &ff, false) : nullptr;
    if (em && em != &a && (int)em->boneCount >= n) {
        Matrix toHead = MatrixMultiply(MatrixInvert(trs(em->keyframePoses[ff][e.head])), out[e.head]);
        uint64_t own = e.owns[&a - e.anims];
        for (int b = 0; b < n && b < 64; b++)
            if ((e.face[b] & 1) && !(own >> b & 1)) out[b] = MatrixMultiply(trs(em->keyframePoses[ff][b]), toHead);
    }
    // the head's frame is HatLocator's, whose origin sits (0, 14, -1) units off the head joint (w4m-models --list)
    Matrix w = out[e.hat];
    Vector3 x = Vector3Normalize({w.m0, w.m1, w.m2}), y = Vector3Normalize({w.m4, w.m5, w.m6});
    if (ly->lookYaw || ly->lookPitch) {
        Matrix r = turn(Vector3Transform({0, -14, 1}, w), x, y, ly->lookYaw, ly->lookPitch);
        for (int b = 0; b < n; b++) if (e.face[b] & 2) out[b] = MatrixMultiply(out[b], r);
    }
}

bool Models::joint(const char *name, const char *joint, const char *clip, float t, bool loop, Matrix *out, const char *aim, float aimT, const Layers *ly) {
    auto it = models.find(name);
    if (it == models.end()) return false;
    const Entry &e = it->second;
    int f, af = 0, b = 0;
    const ModelAnimation *a = clipFrame(e, clip, t, loop, &f), *am = aim ? clipFrame(e, aim, aimT, false, &af, false) : nullptr;
    int n = (int)e.m.skeleton.boneCount;
    while (b < n && strcmp(e.m.skeleton.bones[b].name, joint)) b++;
    if (!a || b == n || b >= (int)a->boneCount) return false;
    if (!ly) return *out = bone(e, *a, f, b, am, af), true;
    static std::vector<Matrix> p;
    pose(e, *a, f, am, af, ly, p);
    *out = p[b];
    return true;
}

// UpdateModelAnimation() equivalent at an integer frame; raylib inverts a bone matrix per vertex for the normals
static void skin(Entry &e, const ModelAnimation &a, int f, const ModelAnimation *aim, int af, const Models::Layers *ly) {
    Model &m = e.m;
    static std::vector<Matrix> nm, p;
    nm.resize(m.skeleton.boneCount);
    pose(e, a, f, aim, af, ly, p);
    for (int b = 0; b < (int)p.size(); b++) {
        m.boneMatrices[b] = MatrixMultiply(e.invBind[b], p[b]);
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

static Shader over{};
void Models::shade(Shader s) { over = s; }

static void drawModel(Model &m, Vector3 pos, Color tint) {
    Shader keep = m.materials[0].shader;
    for (int k = 0; over.id && k < m.materialCount; k++) m.materials[k].shader = over;
    DrawModel(m, pos, 1, tint);
    for (int k = 0; over.id && k < m.materialCount; k++) m.materials[k].shader = keep;
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

bool Models::draw(const char *name, Matrix m, Color tint, const char *clip, float t) {
    auto it = models.find(name);
    if (it == models.end()) return false;
    Entry &e = it->second;
    int f;
    if (const ModelAnimation *a = clip ? clipFrame(e, clip, t, true, &f, false) : nullptr) {
        if ((a != e.posed || f != e.frame || e.aimed || e.layered) && e.m.boneMatrices && a->keyframeCount > 0) skin(e, *a, f, nullptr, -1, nullptr);
        e.posed = a, e.frame = f, e.aimed = nullptr, e.aimFrame = -1, e.layered = false;
    }
    e.m.transform = m;
    drawModel(e.m, {0, 0, 0}, tint);
    return true;
}

static bool same(const Models::Layers &a, const Models::Layers &b) {
    return a.face == b.face && (int)(a.faceT * 60) == (int)(b.faceT * 60) && a.lookYaw == b.lookYaw && a.lookPitch == b.lookPitch;
}

bool Models::draw(const char *name, Vector3 pos, float yaw, float pitch, Color tint, const char *clip, float t, bool loop, const char *aim, float aimT, const Layers *ly) {
    auto it = models.find(name);
    if (it == models.end()) return false;
    Entry &e = it->second;
    // skinned meshes are shared: pose them right before each draw (CPU skinning), unless already in that pose
    int f, af = -1;
    const ModelAnimation *am = aim ? clipFrame(e, aim, aimT, false, &af, false) : nullptr;
    if (const ModelAnimation *a = clipFrame(e, clip, t, loop, &f)) {
        bool lay = ly != nullptr, moved = lay != e.layered || (lay && !same(*ly, e.lay));
        if ((a != e.posed || f != e.frame || am != e.aimed || af != e.aimFrame || moved) && e.m.boneMatrices && a->keyframeCount > 0) skin(e, *a, f, am, af, ly);
        e.posed = a, e.frame = f, e.aimed = am, e.aimFrame = af, e.layered = lay;
        if (lay) e.lay = *ly;
    }
    e.m.transform = MatrixMultiply(MatrixRotateX(-pitch), MatrixRotateY(yaw));
    drawModel(e.m, pos, tint);
    return true;
}

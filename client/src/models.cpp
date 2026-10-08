#include "models.h"
#include "lit.h"
#include "loading.h"
#include "raymath.h"
#include "rlgl.h"
#include "external/cgltf.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fcntl.h>
#include <unistd.h>
#include <thread>
#include <deque>
#include <mutex>
#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#ifdef __SWITCH__
#define MODEL_DIR "sdmc:/switch/worms4nx/assets/models"
#else
#define MODEL_DIR "./assets/models"
#endif

namespace {
// W4M XChildSelector (w4m-models root extras "sel"): the mesh showing child 0 and each child's material; per clip keying it,
// its SelectedChild value at 60 fps; "A" tracks: a mesh's XConstColorSet alpha at 60 fps
struct Sel { int mesh; std::vector<int> mat; };
struct SelTrack { int sel; std::vector<float> v; };
using Tracks = std::map<std::string, std::vector<SelTrack>>;  // per W4M clip
void parseSel(const char *p, std::vector<Sel> &sels, Tracks &tracks, Tracks &alphas) {
    while (*p && *p != '"') {  // "S <primitive> <material>...;" then "K|A <clip> <selector|primitive> <length> <value or value*count>...;"
        const char *end = p + strcspn(p, ";\"");
        std::istringstream in(std::string(p, end));
        std::string tag, w;
        if (in >> tag; tag == "S") {
            Sel s{};
            in >> s.mesh;
            for (int m; in >> m;) s.mat.push_back(m + 1);  // raylib material 0 is its default
            sels.push_back(s);
        } else if (tag == "K" || tag == "A") {
            std::string clip;
            SelTrack t{};
            float len;
            in >> clip >> t.sel >> len;
            while (in >> w) t.v.insert(t.v.end(), w.find('*') == std::string::npos ? 1 : atoi(w.c_str() + w.find('*') + 1), strtof(w.c_str(), nullptr));
            (tag == "K" ? tracks : alphas)[clip].push_back(std::move(t));
        }
        p = *end == ';' ? end + 1 : end;
    }
}
Matrix trs(const Transform &p) {
    return MatrixMultiply(MatrixMultiply(MatrixScale(p.scale.x, p.scale.y, p.scale.z), QuaternionToMatrix(p.rotation)),
                          MatrixTranslate(p.translation.x, p.translation.y, p.translation.z));
}
struct Entry {
    Model m; ModelAnimation *anims = nullptr; int count = 0; const ModelAnimation *posed = nullptr, *aimed = nullptr; int frame = -1, aimFrame = -1;
    std::vector<Matrix> invBind;
    std::vector<int> arm;  // per bone: its shoulder bone, -1 off the arms (the glb skeleton is flat: matched by name)
    std::vector<int> parent;  // per bone: its parent in W4.Worm's main|head|... graph, -1 for main or another model
    std::vector<uint8_t> face;  // per bone: 1 lips, eyelid or eyebrow (W4M emote bones), 2 turns with the head
    bool hasFx = false;
    Vector3 fx{};
    int head = -1, hat = -1, sh[2] = {-1, -1}, mainB = -1, blend = -1;  // sh: right, left shoulder
    const ModelAnimation *base = nullptr;
    std::vector<int> pupil;  // meshes of the pupils (the eyes' layer W4M's Eyes_LR/UD offset), left eye first
    std::vector<std::vector<float>> uv0; float eyeUV[3] = {};  // their rest texcoords; the offsets now in their VBOs
    std::vector<uint64_t> owns;  // per clip: the face bones it moves itself, which an emote layer leaves to it
    Models::Layers lay{}; bool layered = false;  // the Layers of the last skin()
    std::vector<int> glow;  // materials drawn as additive light (W4M shader surfaces)
    std::vector<Sel> sels;
    Tracks tracks, alphas;
    std::vector<float> alpha;  // per mesh, from the last draw's clip
    // skinned poses in their own buffers: a worm drawn in the shadow pass then the view is skinned once a frame
    // pose: the skinned meshes' positions and normals, all meshes in one buffer each (a pose is two uploads)
    struct Slot { const ModelAnimation *a, *am; int f, af; bool lay; Models::Layers ly; float eyeUV[3]; unsigned long used; std::vector<Mesh> meshes; unsigned pose[2]; };
    std::vector<Slot> slots;
};
std::map<std::string, Entry> models;

static void unloadSlot(Entry::Slot &s) {  // GPU side only: the CPU arrays are the model's
    for (Mesh m : s.meshes) {
        m.vertices = m.texcoords = m.normals = m.tangents = m.texcoords2 = m.animVertices = m.animNormals = m.boneWeights = nullptr;
        m.colors = nullptr, m.indices = nullptr, m.boneIndices = nullptr;
        UnloadMesh(m);
    }
    s.meshes.clear();
    rlUnloadVertexBuffer(s.pose[0]), rlUnloadVertexBuffer(s.pose[1]);
}

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
    bool hasFx = false;
    Vector3 fx{};  // translation of the glb's FxLocator node (WEAPTWK FxLocator: the ArielFx emitters' origin), model space
    std::vector<Sel> sels;
    Tracks tracks, alphas;
};
std::mutex mu;
std::deque<Job> jobs;
// load cost per phase, ns summed over threads: every LoadFileData() read, then the models' glb parse, png decode, mipmaps, clips, upload
enum { T_READ, T_PARSE, T_PNG, T_MIP, T_CLIPS, T_UPLOAD, T_N };
std::atomic<long long> spent[T_N]{}, readBytes{0};
struct Span { int k; std::chrono::steady_clock::time_point t = std::chrono::steady_clock::now();
              ~Span() { spent[k] += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t).count(); } };
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
    static std::mutex io;  // one read at a time (the SD is slower with concurrent readers), in big blocks; decoding stays parallel
    std::lock_guard<std::mutex> l(io);
    Span sp{T_READ};
    int fd = open(path, O_RDONLY);
    if (fd < 0) return TraceLog(LOG_WARNING, "FILEIO: [%s] Failed to open file", path), nullptr;
    off_t n = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    unsigned char *d = n > 0 ? (unsigned char *)malloc(n) : nullptr;
    off_t got = 0;
    for (ssize_t r; d && got < n && (r = read(fd, d + got, std::min<off_t>(n - got, 4 << 20))) > 0;) got += r;  // newlib's fread reads in st_blksize pieces
    if (d && got == n) *size = (int)n, readBytes += n;
    else free(d), d = nullptr;
    close(fd);
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
    {
        Span sp{T_PARSE};
        if (!j.data || j.size < 20 || cgltf_parse(&o, j.data, j.size, &g) != cgltf_result_success || g->file_type != cgltf_file_type_glb ||
            cgltf_load_buffers(&o, g, path) != cgltf_result_success) {
            if (g) cgltf_free(g);
            return j;
        }
    }
    j.albedo.resize(g->materials_count + 1);
    for (size_t i = 0; i < g->materials_count; i++) {
        cgltf_texture *t = g->materials[i].pbr_metallic_roughness.base_color_texture.texture;
        cgltf_buffer_view *v = t && t->image ? t->image->buffer_view : nullptr;
        if (!v || !v->buffer->data) continue;
        { Span sp{T_PNG}; j.albedo[i + 1] = LoadImageFromMemory(".png", (unsigned char *)v->buffer->data + v->offset, (int)v->size); }
        Span sp{T_MIP};
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
    for (size_t i = 0; i < g->nodes_count; i++)
        if (g->nodes[i].name && !strcmp(g->nodes[i].name, "FxLocator") && g->nodes[i].has_translation)
            j.hasFx = true, j.fx = {g->nodes[i].translation[0], g->nodes[i].translation[1], g->nodes[i].translation[2]};
    if (const char *x = g->extras.data ? strstr(g->extras.data, "\"sel\":\"") : nullptr) parseSel(x + 7, j.sels, j.tracks, j.alphas);
    cgltf_free(g);
    auto [js, end] = json(j);
    const char *tag = "\"baseColorTexture\"";
    for (char *p = js; (p = std::search(p, end, tag, tag + 18)) != end; p++) p[16] = 'X';  // a key cgltf ignores: no decode
    if (skinned) {  // a copy of the bytes, not a second SD read: it needs the clips blank() drops
        Span sp{T_CLIPS};
        unsigned char *keep = j.data;
        j.data = (unsigned char *)memcpy(malloc(j.size), keep, j.size);
        serve = &j, j.anims = LoadModelAnimations(path, &j.count), serve = nullptr;
        j.data = keep;
    }
    blank(j, "animations", 0), blank(j, "accessors", acc), blank(j, "bufferViews", views), compact(j);
    return j;
}

// One step of a job's GPU side (LoadModel(), then a texture); true once j.m is complete
bool uploadStep(Job &j) {
    Span sp{T_UPLOAD};
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

// W4.Worm graph (w4m-models --list, W4M_GROUPS): main > tail > tail2 > tail3, head > face bones and HatLocator, shoulder_X >
// wrist_X > <finger>knuckle_X > <finger>_X (thumb_X and WeaponLocator on the wrist); XBone names are <group>_bone
static int parentOf(const Entry &e, const char *n) {
    const char *side = strstr(n, "_left") ? "left" : "right";
    std::string p;
    static const char *const FACE[] = {"upperlip", "bottomlip", "mouthmiddle", "eyetop", "eyebrow", "HatLocator"};
    for (const char *f : FACE) if (!strncmp(n, f, strlen(f))) p = "head_bone";
    if (!strcmp(n, "tail2_bone")) p = "tail_bone";
    else if (!strcmp(n, "tail3_bone")) p = "tail2_bone";
    else if (!strcmp(n, "WeaponLocator")) p = "wrist_right_bone";
    else if (!strncmp(n, "wrist_", 6)) p = std::string("shoulder_") + side + "_bone";
    else if (strstr(n, "knuckle_") || !strncmp(n, "thumb_", 6)) p = std::string("wrist_") + side + "_bone";
    else if (const char *u = strchr(n, '_'); u && (!strncmp(n, "pinky_", 6) || !strncmp(n, "index_", 6) || !strncmp(n, "fore_", 5)))
        p = std::string(n, u) + "knuckle_" + side + "_bone";
    else if (p.empty() && strcmp(n, "main_bone")) p = "main_bone";
    for (int b = 0; !p.empty() && b < (int)e.m.skeleton.boneCount; b++) if (p == e.m.skeleton.bones[b].name) return b;
    return -1;
}

void add(Job &j) {
    Entry e;
    e.m = j.m;
    if (!e.m.meshCount) return UnloadModelAnimations(j.anims, j.count);
    if (strstr(j.path.c_str(), "/frontend/")) return (void)(spare[j.path] = e.m);  // FrontBg's scene, freed by FrontBg
    for (int k = 0; k < e.m.materialCount; k++)
        if (shader.id != rlGetShaderIdDefault()) e.m.materials[k].shader = shader;
    e.anims = j.anims, e.count = j.count, e.hasFx = j.hasFx, e.fx = j.fx, e.sels = std::move(j.sels), e.tracks = std::move(j.tracks), e.alphas = std::move(j.alphas);
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
        if (!strncmp(n, "shoulder_", 9)) e.sh[left] = b;
        if (!strcmp(n, "main_bone")) e.mainB = b;
        if (!strcmp(n, "Blend")) e.blend = b;
    }
    for (int b = 0; b < (int)e.m.skeleton.boneCount; b++) e.parent.push_back(parentOf(e, e.m.skeleton.bones[b].name));
    owners(e);
    for (int i = 0; i < e.count; i++) if (!strcmp(e.anims[i].name, "Base")) e.base = &e.anims[i];
    std::string name = GetFileNameWithoutExt(j.path.c_str());
    if (name == "ufo") e.glow = {2};  // BeamConeShape's noise image (raylib material 0 is the default)
    for (int i = 0; name == "worm" && i < e.m.meshCount; i++) {  // glb material 1: boggy*eye, whose shadercolor2 the Eyes clips key
        const Mesh &me = e.m.meshes[i];
        if (e.m.meshMaterial[i] != 2 || !me.texcoords) continue;
        bool left = me.vertices[0] > 0;  // the worm's left is +x
        e.uv0.emplace(left ? e.uv0.begin() : e.uv0.end(), me.texcoords, me.texcoords + 2 * me.vertexCount);
        e.pupil.insert(left ? e.pupil.begin() : e.pupil.end(), i);
    }
    models[name] = e;
}
}  // namespace

// Workers on cores 1 and 2: the menu scene, then (second start()) worm.glb (longest job) and the rest
static std::vector<std::string> files;
static std::atomic<unsigned> nextFile{0};
static std::atomic<int> working{0};
static std::atomic<bool> stopWork{false};
static std::thread workers[2];
static int menuLeft = 0;  // menu scene jobs not yet added
static double startT = 0;
static bool menuFile(const std::string &p) { return p.find("/frontend/") != std::string::npos || p.find("/seagull.glb") != std::string::npos; }

void Models::start() {
    bool first = files.empty();
    if (first) {
        for (std::string &p : Loading::list(MODEL_DIR, ".glb", true)) {  // recurses into hats/ and frontend/
            if (p.find("/sky/") != std::string::npos) continue;  // level skies: one per match, Models::decode()
            if (p.find("/hats/") != std::string::npos) hatNames.push_back(GetFileNameWithoutExt(p.c_str()));
            files.push_back(p);
        }
        if (files.empty()) return;
        std::sort(hatNames.begin(), hatNames.end());  // same file set on every client -> same order
        auto rank = [](const std::string &p) { return menuFile(p) ? 0 : p.find("/worm.glb") != std::string::npos ? 1 : 2; };
        std::stable_sort(files.begin(), files.end(), [&](const std::string &a, const std::string &b) { return rank(a) < rank(b); });
        menuLeft = (int)std::count_if(files.begin(), files.end(), menuFile);
        startT = GetTime();
    }
    for (std::thread &w : workers) if (w.joinable()) w.join();
    unsigned end = first ? (unsigned)menuLeft : (unsigned)files.size();
    working = 2;
    for (int c = 0; c < 2; c++)
        workers[c] = std::thread([end](int core) {
            Loading::pinCore(core);
            for (unsigned i; !stopWork && (i = nextFile++) < end;) {
                Job j = ::prepare(files[i].c_str());
                std::lock_guard<std::mutex> l(mu);
                jobs.push_back(std::move(j));
            }
            nextFile = std::min(nextFile.load(), end);
            working--;
        }, c + 1);
}

bool Models::menuReady() { return menuLeft == 0; }

static void compileBeam();
bool Models::upload(double until) {
    if (!shader.id) shader = Lit::modelShader(true), compileBeam();  // textured, alpha-tested (teeth/eye overlays), W4M worm light
    static Job cur;
    static bool busy = false, finished = false;
    if (finished || files.empty()) return false;
    while (GetTime() < until) {
        if (!busy) {
            std::lock_guard<std::mutex> l(mu);
            if (jobs.empty()) break;
            cur = std::move(jobs.front()), jobs.pop_front(), busy = true;
        }
        if (uploadStep(cur)) menuLeft -= menuFile(cur.path), add(cur), busy = false;
    }
    if (busy || working || nextFile < files.size()) return true;
    if (std::lock_guard<std::mutex> l(mu); !jobs.empty()) return true;
    for (std::thread &w : workers) w.join();
    finished = true;
    TraceLog(LOG_INFO, "MODELS: %d loaded from %s (%d hats) in %.0f ms", (int)models.size(), MODEL_DIR, (int)hatNames.size(), (GetTime() - startT) * 1000);
    return false;
}

size_t Models::bytes() {
    size_t n = 0;
    for (const auto &[name, e] : models) {
        for (int i = 0; i < e.m.meshCount; i++) {
            const Mesh &me = e.m.meshes[i];
            size_t v = me.vertexCount;
            n += v * ((me.vertices ? 12 : 0) + (me.normals ? 12 : 0) + (me.texcoords ? 8 : 0) + (me.texcoords2 ? 8 : 0) + (me.tangents ? 16 : 0) + (me.colors ? 4 : 0) +
                      (me.boneIndices ? 4 : 0) + (me.boneWeights ? 16 : 0) + (me.animVertices ? 12 : 0) + (me.animNormals ? 12 : 0)) +
                 (me.indices ? (size_t)me.triangleCount * 6 : 0);
        }
        for (int i = 0; i < e.count; i++) n += (size_t)e.anims[i].keyframeCount * e.anims[i].boneCount * sizeof(Transform);
    }
    return n;
}

const char *Models::bootStats() {
    static char b[320];
    auto ms = [](int k) { return spent[k] / 1e6; };
    snprintf(b, sizeof b, "files %.1f MB read in %.0f ms (%.1f MB/s, one reader); models: glb parse %.0f, png %.0f, mipmaps %.0f, clips %.0f (workers, summed), upload %.0f (main)",
             readBytes / 1048576.0, ms(T_READ), readBytes / 1048576.0 / fmax(ms(T_READ) / 1e3, 1e-3), ms(T_PARSE), ms(T_PNG), ms(T_MIP), ms(T_CLIPS), ms(T_UPLOAD));
    return b;
}

static std::map<std::string, Job> decoded;  // Models::decode() output, uploaded by take()

void Models::decode(const char *path) {
    Job j = ::prepare(path);
    std::lock_guard<std::mutex> l(mu);
    decoded[path] = std::move(j);
}

Model Models::take(const char *path) {
    auto it = spare.find(path);
    if (it == spare.end()) {
        Job j;
        {
            std::lock_guard<std::mutex> l(mu);
            auto d = decoded.find(path);
            if (d != decoded.end()) j = std::move(d->second), decoded.erase(d);
        }
        if (j.path.empty()) j = ::prepare(path);
        while (!uploadStep(j)) {}
        return j.m;
    }
    Model m = it->second;
    spare.erase(it);
    return m;
}

static Shader scroll{};  // drawModel's beam pass

void Models::unload() {
    stopWork = true;
    for (std::thread &w : workers) if (w.joinable()) w.join();
    for (auto &[name, e] : models) {
        for (Entry::Slot &s : e.slots) unloadSlot(s);
        UnloadModelAnimations(e.anims, e.count);
        UnloadModel(e.m);
    }
    models.clear();
    hatNames.clear();
    if (scroll.id) UnloadShader(scroll), scroll = {};
    if (shader.id) UnloadShader(shader);
}

int Models::hatCount() { return (int)hatNames.size(); }
const char *Models::hatName(int i) { return i >= 0 && i < (int)hatNames.size() ? hatNames[i].c_str() : ""; }

static const ModelAnimation *find(const Entry &e, const char *clip) {  // "A" also finds w4m-models' "A+B" (A over B)
    size_t n = clip ? strlen(clip) : 0;
    for (int i = 0; clip && i < e.count; i++) if (!strncmp(e.anims[i].name, clip, n) && (!e.anims[i].name[n] || e.anims[i].name[n] == '+')) return &e.anims[i];
    return nullptr;
}

bool Models::has(const char *name) { return models.count(name) > 0; }
bool Models::fxLocator(const char *name, Vector3 *out) {
    auto it = models.find(name);
    return it != models.end() && it->second.hasFx && (*out = it->second.fx, true);
}
float Models::bottom(const char *name) {
    auto it = models.find(name);
    return it == models.end() ? 0 : -GetModelBoundingBox(it->second.m).min.y;
}

float Models::clipLength(const char *name, const char *clip) {
    auto it = models.find(name);
    if (it == models.end() || !clip) return 0;
    if (const ModelAnimation *a = find(it->second, clip)) return (a->keyframeCount - 1) / 60.0f;  // raylib samples glTF clips at 60 fps
    auto k = it->second.tracks.find(clip);  // a clip keying only XChildSelectors
    return k == it->second.tracks.end() ? 0 : (k->second[0].v.size() - 1) / 60.0f;
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

// x[b] relative to its parent; false under a parent scaled to 0 (Base keys the wrists at 0, so their children)
static bool rel(const Entry &e, const Transform *x, int b, Transform *out) {
    int p = e.parent[b];
    if (p < 0) return *out = x[b], true;
    const Transform &q = x[p];
    if (fabsf(q.scale.x) < 1e-4f || fabsf(q.scale.y) < 1e-4f || fabsf(q.scale.z) < 1e-4f) return false;
    Quaternion qi = QuaternionInvert(q.rotation);
    Vector3 t = Vector3RotateByQuaternion(Vector3Subtract(x[b].translation, q.translation), qi);
    *out = {Vector3Divide(t, q.scale), QuaternionMultiply(qi, x[b].rotation), Vector3Divide(x[b].scale, q.scale)};
    return true;
}

// XAnim sums the clips' channels (0x7ac1a0): each layer clip adds its offset from Base to body joint by joint, translations
// summed, rotations composed (ours, for W4M's Euler sum: the glb holds baked transforms); joints L leaves at Base keep body's offset
static void addLayers(const Entry &e, const Transform *body, const Transform *const *L, const float *w, int nl, std::vector<Matrix> &out) {
    int n = (int)out.size();
    const Transform *B = e.base ? e.base->keyframePoses[0] : nullptr;
    static std::vector<int> moved;  // the nearest re-posed joint at or above b
    static std::vector<Matrix> inv;  // body matrix of a re-posed joint, inverted at its first child
    static std::vector<uint8_t> hasInv;
    moved.assign(n, -1), inv.resize(n), hasInv.assign(n, 0);
    for (int b = 0; b < n; b++) {
        int p = b < (int)e.parent.size() ? e.parent[b] : -1;
        Transform rb, rl, r0;
        bool own = false;
        Vector3 dt = {};
        Quaternion dq = QuaternionIdentity();
        for (int k = 0; k < nl; k++)  // XAnim sums every clip's channels (0x7ac1a0): the layers' offsets from Base add up
            if (B && b < (int)e.base->boneCount && rel(e, L[k], b, &rl) && rel(e, B, b, &r0) &&
                (Vector3Distance(rl.translation, r0.translation) > 1e-4f || fabsf(rl.rotation.x * r0.rotation.x + rl.rotation.y * r0.rotation.y + rl.rotation.z * r0.rotation.z + rl.rotation.w * r0.rotation.w) < 1 - 1e-6f))
                own = true, dt = Vector3Add(dt, Vector3Scale(Vector3Subtract(rl.translation, r0.translation), w[k])),
                dq = QuaternionMultiply(QuaternionSlerp(QuaternionIdentity(), QuaternionMultiply(rl.rotation, QuaternionInvert(r0.rotation)), w[k]), dq);
        if (own && rel(e, body, b, &rb)) {
            Transform r = {Vector3Add(rb.translation, dt), QuaternionNormalize(QuaternionMultiply(dq, rb.rotation)), rb.scale};
            out[b] = p >= 0 ? MatrixMultiply(trs(r), out[p]) : trs(r), moved[b] = b;
        } else if (int a = p >= 0 ? moved[p] : -1; a >= 0) {
            if (!hasInv[a]) inv[a] = MatrixInvert(trs(body[a])), hasInv[a] = 1;
            out[b] = MatrixMultiply(MatrixMultiply(trs(body[b]), inv[a]), out[a]), moved[b] = a;
        } else out[b] = trs(body[b]);
    }
}

// The body clip at frame f under the acting gesture layers: XAnim sums w x value per channel (0x7ac1a0) and Base carries
// weight 1, so a gesture at weight w is (1 - w) body + w gesture, scale averaged likewise (attribute flag 8, 0x7acc6f)
static const Transform *layered(const Entry &e, const ModelAnimation &a, int f, const Models::Layers *ly) {
    const ModelAnimation *c[2] = {};
    int cf[2] = {}, n = (int)a.boneCount;
    float w[2] = {};
    for (int k = 0; ly && k < 2; k++)
        if (ly->act[k] && ly->actW[k] > 0 && (c[k] = clipFrame(e, ly->act[k], ly->actT[k], false, &cf[k], false)) && (int)c[k]->boneCount >= n) w[k] = ly->actW[k];
    if (!w[0] && !w[1]) return a.keyframePoses[f];
    static std::vector<Transform> out;
    out.resize(n);
    float wb = fmaxf(0, 1 - w[0] - w[1]);
    for (int b = 0; b < n; b++) {
        Transform t = a.keyframePoses[f][b];
        Vector3 p = Vector3Scale(t.translation, wb), sc = Vector3Scale(t.scale, wb);
        Quaternion q = QuaternionScale(t.rotation, wb);
        for (int k = 0; k < 2; k++) {
            if (!w[k]) continue;
            const Transform &u = c[k]->keyframePoses[cf[k]][b];
            Quaternion r = u.rotation;
            if (r.x * t.rotation.x + r.y * t.rotation.y + r.z * t.rotation.z + r.w * t.rotation.w < 0) r = QuaternionScale(r, -1);
            p = Vector3Add(p, Vector3Scale(u.translation, w[k])), sc = Vector3Add(sc, Vector3Scale(u.scale, w[k]));
            q = QuaternionAdd(q, QuaternionScale(r, w[k]));
        }
        out[b] = {p, QuaternionNormalize(q), sc};
    }
    return out.data();
}

// About pivot p: rotate by yaw about y, after pitch about x (frame axes x, y)
static Matrix turn(Vector3 p, Vector3 x, Vector3 y, float yaw, float pitch) {
    Matrix r = MatrixMultiply(MatrixRotate(x, -pitch), MatrixRotate(y, yaw));  // + pitch raises the face (checked on animshot)
    return MatrixMultiply(MatrixMultiply(MatrixTranslate(-p.x, -p.y, -p.z), r), MatrixTranslate(p.x, p.y, p.z));
}

// Clip a's Blend node at frame f minus Base's: Translate.x, .y (units) and Rotate.y (degrees, unsigned)
static bool blendOf(const Entry &e, const Transform *p, int n, float out[3]) {
    if (e.blend < 0 || e.mainB < 0 || !e.base || e.blend >= n || e.blend >= (int)e.base->boneCount) return false;
    auto rel = [&](const Transform *c) { return MatrixMultiply(trs(c[e.blend]), MatrixInvert(trs(c[e.mainB]))); };
    Matrix r = rel(p), r0 = rel(e.base->keyframePoses[0]), d = MatrixMultiply(r, MatrixInvert(r0));
    out[0] = r.m12 - r0.m12, out[1] = r.m13 - r0.m13;
    out[2] = atan2f(sqrtf(fabsf(d.m8 * d.m2)), d.m10) * RAD2DEG;  // the bind's scale skews d: |sin| from both off-diagonals
    return true;
}

// 0x59b870: an arm follows the GestureAt target with weight c, the head with d; its Blend mode x is 0 (target), 1 (neither), -1 or 2 (head)
static void armWeights(float x, float *c, float *d) {
    *c = x >= 1 || x < -1 ? 0 : 1 - fabsf(x);
    *d = x >= 1 ? x - 1 : x >= 0 ? 0 : x >= -1 ? -x : x + 2;
}

// A viseme clip at weight w on the face bones: its offset from Base added in the parent's frame (XAnim sums the channels, 0x7ac1a0)
static void lipLayer(const Entry &e, const Transform *L, float w, std::vector<Matrix> &out) {
    for (int b = 0; e.base && b < (int)out.size() && b < (int)e.face.size(); b++) {
        int p = e.parent[b];
        Transform rl, r0;
        if (!(e.face[b] & 1) || p < 0 || !rel(e, L, b, &rl) || !rel(e, e.base->keyframePoses[0], b, &r0)) continue;
        Quaternion d = QuaternionMultiply(rl.rotation, QuaternionInvert(r0.rotation));
        Vector3 dt = Vector3Subtract(rl.translation, r0.translation);
        if (Vector3Length(dt) < 1e-5f && fabsf(d.w) > 1 - 1e-6f) continue;
        Vector3 t, sc;
        Quaternion q;
        MatrixDecompose(MatrixMultiply(out[b], MatrixInvert(out[p])), &t, &q, &sc);
        Transform r = {Vector3Add(t, Vector3Scale(dt, w)), QuaternionNormalize(QuaternionMultiply(QuaternionSlerp(QuaternionIdentity(), d, w), q)), sc};
        out[b] = MatrixMultiply(trs(r), out[p]);
    }
}

// Every bone's model-space matrix, with the W4M pose layers over the clip
static void pose(const Entry &e, const ModelAnimation &a, int f, const ModelAnimation *aim, int af, const Models::Layers *ly, std::vector<Matrix> &out) {
    int n = std::min(e.m.skeleton.boneCount, a.boneCount);
    out.resize(n);
    const Transform *src = layered(e, a, f, ly);
    const Transform *L[5];
    float lw[5];
    int nl = 0;
    if (aim && (int)aim->boneCount >= n) L[nl] = aim->keyframePoses[af], lw[nl++] = ly ? ly->aimW : 1;
    for (int k = 0, kf; ly && k < 4; k++)
        if (const ModelAnimation *c = ly->add[k] ? clipFrame(e, ly->add[k], ly->addT[k], false, &kf, false) : nullptr; c && (int)c->boneCount >= n)
            L[nl] = c->keyframePoses[kf], lw[nl++] = 1;
    if (nl) addLayers(e, src, L, lw, nl, out);
    else for (int b = 0; b < n; b++) out[b] = trs(src[b]);
    if (!ly || e.head < 0 || e.head >= n || e.hat < 0 || e.hat >= n) return;
    int ff;
    const ModelAnimation *em = ly->face ? clipFrame(e, ly->face, ly->faceT, true, &ff, false) : nullptr;
    if (em && em != &a && (int)em->boneCount >= n) {
        Matrix toHead = MatrixMultiply(MatrixInvert(trs(em->keyframePoses[ff][e.head])), out[e.head]);
        uint64_t own = e.owns[&a - e.anims];
        for (int b = 0; b < n && b < 64; b++)
            if ((e.face[b] & 1) && !(own >> b & 1)) out[b] = MatrixMultiply(trs(em->keyframePoses[ff][b]), toHead);
    }
    for (int k = 0, lf; k < 2; k++)
        if (const ModelAnimation *L = ly->lip[k] && ly->lipW[k] > 0 ? clipFrame(e, ly->lip[k], ly->lipW[k], false, &lf, false) : nullptr; L && (int)L->boneCount >= n)
            lipLayer(e, L->keyframePoses[lf], ly->lipW[k], out);
    // the head's frame is HatLocator's, whose origin sits (0, 14, -1) units off the head joint (w4m-models --list)
    Matrix w = out[e.hat];
    Vector3 x = Vector3Normalize({w.m0, w.m1, w.m2}), y = Vector3Normalize({w.m4, w.m5, w.m6});
    float hy = Clamp(ly->lookYaw, -1.047f, 1.047f), hp = Clamp(ly->lookPitch, -1.22f, 0.785f);  // HeadRotY/X keys' ends
    if (hy || hp) {
        Matrix r = turn(Vector3Transform({0, -14, 1}, w), x, y, hy, hp);
        for (int b = 0; b < n; b++) if (e.face[b] & 2) out[b] = MatrixMultiply(out[b], r);
    }
    // Left/RightArmRotY/X turn the shoulder about main's y then x axes, +-90 degrees at the clips' ends (fitted on the clips)
    float m[3];
    if (e.mainB < 0 || e.mainB >= n || !blendOf(e, src, n, m)) return;
    Matrix mb = out[e.mainB];
    Vector3 mx = Vector3Normalize({mb.m0, mb.m1, mb.m2}), my = Vector3Normalize({mb.m4, mb.m5, mb.m6});
    for (int s = 0; s < 2; s++) {
        float c, d;
        armWeights(m[s ? 0 : 1], &c, &d);  // Translate.x is the left arm's
        float ry = Clamp(c * ly->gestYaw + d * ly->lookYaw, -PI / 2, PI / 2), rx = Clamp(c * ly->gestPitch + d * ly->lookPitch, -PI / 2, PI / 2);
        if (e.sh[s] < 0 || e.sh[s] >= n || (!ry && !rx)) continue;
        Matrix r = turn(Vector3Transform({0, 0.9679f, 0.1689f}, out[e.sh[s]]), mx, my, ry, rx);  // the shoulder joint in the bind space
        for (int b = 0; b < n; b++) if (b < (int)e.arm.size() && e.arm[b] == e.sh[s]) out[b] = MatrixMultiply(out[b], r);
    }
}

// The exe's key curve for unweighted channels (every Eyes / PoseBlend one): Hermite on the tangents' slopes (0x7abb1c, 0x7aa7df),
// a zero out-tangent holds the key, constant outside the keys
float Models::curve(const float (*k)[6], int n, float t) {
    int i = 0;
    while (i < n - 1 && t >= k[i + 1][4]) i++;
    const float *a = k[i], *b = k[std::min(i + 1, n - 1)];
    if (t < k[0][4] || i == n - 1 || a[4] == b[4] || (!a[2] && !a[3])) return t < k[0][4] ? k[0][5] : a[5];
    auto slope = [](float x, float y) { return x ? y / x : 5.72958e6f; };
    float dx = b[4] - a[4], dy = b[5] - a[5], m0 = slope(a[2], a[3]), m1 = slope(b[0], b[1]), u = t - a[4];
    float c3 = (m0 * dx + m1 * dx - 2 * dy) / (dx * dx * dx), c2 = (3 * dy - 2 * m0 * dx - m1 * dx) / (dx * dx);
    return ((c3 * u + c2) * u + m0) * u + a[5];
}

// Eyes_LR / Eyes_UD shadercolor2 keys (in x, y, out x, y, time, value) at clip time 0..2 (1 = ahead): the pupils' offsets
static void eyeOffsets(const Models::Layers *ly, float uv[3]) {  // left u, right u, v
    static const float LR_L[3][6] = {{0, 0, 0.9995117f, -0.019989014f, 0, 0.019989014f}, {0.9995117f, -0.019989014f, 0.89404297f, -0.44702148f, 1, 0},
                                     {0.89404297f, -0.44702148f, 0, 0, 2, -0.5f}};
    static const float LR_R[3][6] = {{0, 0, 0.89404297f, 0.44702148f, 0, -0.5f}, {0.89404297f, 0.44702148f, 0.9995117f, 0.019989014f, 1, 0},
                                     {0.9995117f, 0.019989014f, 0, 0, 2, 0.019989014f}};
    static const float UD_U[5][6] = {{0.9838867f, 0.17712402f, 0.9838867f, 0.17712402f, 0, -0.08996582f}, {1, 0, 1, 0, 0.5f, 0}, {1, 0, 1, 0, 1, 0},
                                     {1, 0, 1, 0, 1.7080078f, 0}, {0.98535156f, -0.16894531f, 0.98535156f, -0.16894531f, 2, -0.049987793f}};
    static const float UD_V[3][6] = {{0.92822266f, 0.3713379f, 0.92822266f, 0.3713379f, 0, -0.39990234f},
                                     {0.93310547f, 0.35913086f, 0.93310547f, 0.35913086f, 1, 0}, {0.9375f, 0.34692383f, 0.9375f, 0.34692383f, 2, 0.36987305f}};
    float lr = ly ? 1 + ly->eyeYaw / (PI / 2) : 1, ud = ly ? 1 - ly->eyePitch / (PI / 2) : 1;  // 0x59b752; the keys hold past 0 and 2
    float u = Models::curve(UD_U, 5, ud);  // both clips key the u channel: XAnim adds them (attribute flags 0x04)
    uv[0] = Models::curve(LR_L, 3, lr) + u, uv[1] = Models::curve(LR_R, 3, lr) + u, uv[2] = Models::curve(UD_V, 3, ud);
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
    static std::vector<Matrix> p;
    pose(e, *a, f, am, af, ly, p);
    *out = p[b];
    return true;
}

static bool skinned(const Mesh &me) { return me.boneWeights && me.boneIndices && me.animVertices && me.animNormals; }

// UpdateModelAnimation() equivalent at an integer frame; raylib inverts a bone matrix per vertex for the normals
// dst: the meshes whose buffers take the result (the model's own by default); shared: a slot's pose buffers instead
static void skin(Entry &e, const ModelAnimation &a, int f, const ModelAnimation *aim, int af, const Models::Layers *ly, Mesh *dst = nullptr, const unsigned *shared = nullptr) {
    Model &m = e.m;
    if (!dst) dst = m.meshes;
    static std::vector<Matrix> nm, p;
    nm.resize(m.skeleton.boneCount);
    pose(e, a, f, aim, af, ly, p);
    for (int b = 0; b < (int)p.size(); b++) {
        m.boneMatrices[b] = MatrixMultiply(e.invBind[b], p[b]);
        nm[b] = MatrixTranspose(MatrixInvert(m.boneMatrices[b]));
    }
    static std::vector<float> allP, allN;
    allP.clear(), allN.clear();
    for (int i = 0; i < m.meshCount; i++) {
        Mesh &me = m.meshes[i];
        if (!skinned(me)) continue;
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
        if (shared) {
            allP.insert(allP.end(), me.animVertices, me.animVertices + 3 * me.vertexCount), allN.insert(allN.end(), me.animNormals, me.animNormals + 3 * me.vertexCount);
            continue;
        }
        rlUpdateVertexBuffer(dst[i].vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_POSITION], me.animVertices, me.vertexCount * 3 * sizeof(float), 0);
        if (me.normals) rlUpdateVertexBuffer(dst[i].vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_NORMAL], me.animNormals, me.vertexCount * 3 * sizeof(float), 0);
    }
    if (shared && !allP.empty())
        rlUpdateVertexBuffer(shared[0], allP.data(), (int)(allP.size() * sizeof(float)), 0), rlUpdateVertexBuffer(shared[1], allN.data(), (int)(allN.size() * sizeof(float)), 0);
}

static Shader over{};
// W4M BeamConeShape_WarpgateShader_1 (key 0x1000401): its v offset runs 0 to -1 every 1.166 s in every Abduct* clip (axis not verified)
static const char *SCROLL_VS = R"(
attribute vec3 vertexPosition;
attribute vec2 vertexTexCoord;
uniform mat4 mvp;
uniform vec2 shift;
varying vec2 uv;
void main() { uv = vertexTexCoord + shift; gl_Position = mvp * vec4(vertexPosition, 1.0); }
)";
static const char *SCROLL_FS = R"(
uniform sampler2D texture0;
uniform vec4 colDiffuse;
varying vec2 uv;
void main() { gl_FragColor = texture2D(texture0, uv) * colDiffuse; }
)";
static void compileBeam() { scroll = Lit::shader(SCROLL_VS, SCROLL_FS, false); }  // at boot: a compile on the first abduction drops frames
void Models::shade(Shader s) { over = s; }
static const char *picked = nullptr;
static float pickedT = 0;
void Models::pick(const char *clip, float t) { picked = clip, pickedT = t; }

// The Blend node's Translate.z and Scale.x in pose p (main's frame)
static bool blendZS(const Entry &e, const Transform *p, int n, float *z, float *sx) {
    if (e.blend < 0 || e.mainB < 0 || e.blend >= n || e.mainB >= n) return false;
    Matrix r = MatrixMultiply(trs(p[e.blend]), MatrixInvert(trs(p[e.mainB])));
    return *z = r.m14, *sx = sqrtf(r.m0 * r.m0 + r.m1 * r.m1 + r.m2 * r.m2), true;
}

// WormPoseManager 0x59db24, Teeth (always at weight 1): open > 0.1 (speaking) at Blend.Scale.x, which only the visemes key (flag 8:
// weighted mean; none: the stored value); else 1 when Blend.Translate.z, summed over the playing clips (flag 1), is over 0.4, else 0
static float teethTime(const Entry &e, const char *clip, float t, bool loop, const Models::Layers *ly) {
    float z0, s0, z, s, dz = 0, sw = 0, ss = 0;
    int f, n = e.base ? (int)e.base->boneCount : 0;
    if (!n || !blendZS(e, e.base->keyframePoses[0], n, &z0, &s0)) return 0;
    if (ly->open > 0.1f) {
        for (int k = 0; k < 2; k++)
            if (const ModelAnimation *L = ly->lip[k] && ly->lipW[k] > 0 ? clipFrame(e, ly->lip[k], ly->lipW[k], false, &f, false) : nullptr;
                L && blendZS(e, L->keyframePoses[f], (int)L->boneCount, &z, &s))
                ss += ly->lipW[k] * s, sw += ly->lipW[k];
        return sw > 0 ? ss / sw : s0;
    }
    if (const ModelAnimation *a = clipFrame(e, clip, t, loop, &f); a && blendZS(e, layered(e, *a, f, ly), (int)a->boneCount, &z, &s)) dz += z - z0;
    if (const ModelAnimation *em = ly->face ? clipFrame(e, ly->face, ly->faceT, true, &f, false) : nullptr; em && blendZS(e, em->keyframePoses[f], (int)em->boneCount, &z, &s))
        dz += z - z0;
    return z0 + dz > 0.4f ? 1 : 0;
}

static float trackAt(const SelTrack &k, float t, bool loop) {
    int f = (int)(t * 60), last = (int)k.v.size() - 1;
    return last < 0 ? 0 : k.v[loop && last > 0 ? f % last : std::clamp(f, 0, last)];
}

// W4M XConstColorSet alpha of each mesh keyed by the playing clip (0x3000200), which the Col CG programs multiply in; 1 unkeyed
static void fade(Entry &e, const char *clip, float t, bool loop) {
    e.alpha.assign(e.m.meshCount, 1);
    for (const char *p = clip; p && *p && !e.alphas.empty();) {
        size_t n = strcspn(p, "+");
        if (auto it = e.alphas.find(std::string(p, n)); it != e.alphas.end())
            for (const SelTrack &k : it->second)
                if (k.sel < e.m.meshCount) e.alpha[k.sel] = std::clamp(trackAt(k, t, loop), 0.0f, 1.0f);
        p += n + (p[n] == '+');
    }
}

// SelectedChild: the largest value the playing clips key (attribute flag 0x10, 0x7ac1a0), truncated (0x6c7243); none keeps the file's 0
static void select(Entry &e, const char *clip, float t, bool loop, const char *aim, float aimT, const Models::Layers *ly) {
    if (e.sels.empty()) return;
    float v[32] = {};  // the game has at most 11 per mesh
    auto add = [&](const char *c, float ct, bool lp) {
        for (const char *p = c; p && *p;) {
            size_t n = strcspn(p, "+");
            if (auto it = e.tracks.find(std::string(p, n)); it != e.tracks.end())
                for (const SelTrack &k : it->second)
                    if (k.sel >= 0 && k.sel < 32 && !k.v.empty()) v[k.sel] = fmaxf(v[k.sel], trackAt(k, ct, lp));
            p += n + (p[n] == '+');
        }
    };
    add(clip, t, loop), add(aim, aimT, false), add(picked, pickedT, true);
    if (ly) {  // the worm's other XAnim layers: Base at weight 1, the acting gestures, the emote
        add("Base", t, true), add(ly->face, ly->faceT, true), add("Teeth", teethTime(e, clip, t, loop, ly), false);
        for (int k = 0; k < 2; k++) if (ly->actW[k] > 0) add(ly->act[k], ly->actT[k], false);
    }
    for (size_t i = 0; i < e.sels.size() && i < 32; i++) {
        const Sel &s = e.sels[i];
        if (s.mesh < e.m.meshCount && !s.mat.empty()) e.m.meshMaterial[s.mesh] = s.mat[std::clamp((int)v[i], 0, (int)s.mat.size() - 1)];
    }
}

static void drawModel(Entry &e, Vector3 pos, Color tint) {
    Model &m = e.m;
    Shader keep = m.materials[0].shader;
    for (int k = 0; over.id && k < m.materialCount; k++) m.materials[k].shader = over;
    Matrix xf = MatrixMultiply(m.transform, MatrixTranslate(pos.x, pos.y, pos.z));
    for (int i = 0; i < m.meshCount; i++) {  // DrawModel's loop with each mesh's alpha; the glow materials after
        int mi = m.meshMaterial[i];
        if (std::find(e.glow.begin(), e.glow.end(), mi) != e.glow.end()) continue;
        Material mat = m.materials[mi];
        Color c = mat.maps[MATERIAL_MAP_DIFFUSE].color;
        float a = i < (int)e.alpha.size() ? e.alpha[i] : 1;
        mat.maps[MATERIAL_MAP_DIFFUSE].color = {(unsigned char)(c.r * tint.r / 255), (unsigned char)(c.g * tint.g / 255), (unsigned char)(c.b * tint.b / 255), (unsigned char)(c.a * tint.a / 255 * a)};
        if (mat.shader.locs && mat.shader.locs[SHADER_LOC_MATRIX_BONETRANSFORMS] != -1 && m.boneMatrices) {
            rlEnableShader(mat.shader.id);
            rlSetUniformMatrices(mat.shader.locs[SHADER_LOC_MATRIX_BONETRANSFORMS], m.boneMatrices, m.skeleton.boneCount);
        }
        DrawMesh(m.meshes[i], mat, xf);
        mat.maps[MATERIAL_MAP_DIFFUSE].color = c;  // maps is shared with the model's material
    }
    if (!e.glow.empty()) {  // additive and unlit (BeamCone's WarpgateShader: the grey noise as cyan light)
        rlDrawRenderBatchActive();
        BeginBlendMode(BLEND_ADDITIVE);
        rlDisableDepthMask(), rlDisableBackfaceCulling();
        for (int i = 0; i < m.meshCount; i++) {
            int mi = m.meshMaterial[i];
            if (std::find(e.glow.begin(), e.glow.end(), mi) == e.glow.end()) continue;
            Material mat = m.materials[mi];
            Vector2 shift = {0, -fmodf((float)GetTime(), 1.166f) / 1.166f};
            SetShaderValue(scroll, GetShaderLocation(scroll, "shift"), &shift, SHADER_UNIFORM_VEC2);
            mat.shader = scroll;
            mat.maps[MATERIAL_MAP_DIFFUSE].color = {(unsigned char)(70 * tint.r / 255), (unsigned char)(200 * tint.g / 255), (unsigned char)(255 * tint.b / 255), (unsigned char)(50 * tint.a / 255)};
            DrawMesh(m.meshes[i], mat, xf);
        }
        rlDrawRenderBatchActive();
        EndBlendMode();
        rlEnableDepthMask(), rlEnableBackfaceCulling();
    }
    for (int k = 0; over.id && k < m.materialCount; k++) m.materials[k].shader = keep;
}

bool Models::visible(Vector3 c, float r) { return visible(c, r, MatrixMultiply(rlGetMatrixModelview(), rlGetMatrixProjection())); }

bool Models::visible(Vector3 c, float r, const Matrix &m) {
    Vector4 w = {m.m3, m.m7, m.m11, m.m15}, rows[3] = {{m.m0, m.m4, m.m8, m.m12}, {m.m1, m.m5, m.m9, m.m13}, {m.m2, m.m6, m.m10, m.m14}};
    for (Vector4 q : rows)
        for (float s : {1.0f, -1.0f}) {  // clip planes row3 + row, row3 - row
            Vector3 n = {w.x + s * q.x, w.y + s * q.y, w.z + s * q.z};
            if (Vector3DotProduct(n, c) + w.w + s * q.w < -r * Vector3Length(n)) return false;
        }
    return true;
}

bool Models::draw(const char *name, Matrix m, Color tint, const char *clip, float t, bool loop) {
    auto it = models.find(name);
    if (it == models.end()) return false;
    Entry &e = it->second;
    int f;
    if (const ModelAnimation *a = clip ? clipFrame(e, clip, t, loop, &f, false) : nullptr) {
        if ((a != e.posed || f != e.frame || e.aimed || e.layered) && e.m.boneMatrices && a->keyframeCount > 0) skin(e, *a, f, nullptr, -1, nullptr);
        e.posed = a, e.frame = f, e.aimed = nullptr, e.aimFrame = -1, e.layered = false;
    }
    e.m.transform = m;
    select(e, clip, t, loop, nullptr, 0, nullptr), fade(e, clip, t, loop);
    drawModel(e, {0, 0, 0}, tint);
    return true;
}

static bool same(const Models::Layers &a, const Models::Layers &b) {
    return a.face == b.face && (int)(a.faceT * 60) == (int)(b.faceT * 60) && a.lookYaw == b.lookYaw && a.lookPitch == b.lookPitch &&
           a.gestYaw == b.gestYaw && a.gestPitch == b.gestPitch && a.act[0] == b.act[0] && a.act[1] == b.act[1] && a.actW[0] == b.actW[0] &&
           a.actW[1] == b.actW[1] && a.aimW == b.aimW && (int)(a.actT[0] * 60) == (int)(b.actT[0] * 60) && (int)(a.actT[1] * 60) == (int)(b.actT[1] * 60) &&
           a.lip[0] == b.lip[0] && a.lip[1] == b.lip[1] && a.lipW[0] == b.lipW[0] && a.lipW[1] == b.lipW[1] &&
           std::equal(a.add, a.add + 4, b.add) && std::equal(a.addT, a.addT + 4, b.addT);
}

bool Models::blend(const char *name, const char *clip, float t, bool loop, const Layers *ly, Vector3 *out) {
    auto it = models.find(name);
    int f;
    const ModelAnimation *a = it == models.end() ? nullptr : clipFrame(it->second, clip, t, loop, &f);
    float m[3];
    if (!a || !blendOf(it->second, layered(it->second, *a, f, ly), (int)a->boneCount, m)) return false;
    return *out = {m[0], m[1], m[2]}, true;
}

// The pupils' texcoords offset (W4M shadercolor2 as a texture translation), uploaded when they change
static void eyes(Entry &e, const Models::Layers *ly, Mesh *dst, float *had) {
    float uv[3];
    eyeOffsets(ly, uv);
    if (e.pupil.size() != 2 || !memcmp(uv, had, sizeof uv)) return;
    memcpy(had, uv, sizeof uv);
    for (int k = 0; k < 2; k++) {
        Mesh &me = e.m.meshes[e.pupil[k]];
        for (int v = 0; v < me.vertexCount; v++) me.texcoords[2 * v] = e.uv0[k][2 * v] + uv[k], me.texcoords[2 * v + 1] = e.uv0[k][2 * v + 1] + uv[2];
        rlUpdateVertexBuffer(dst[e.pupil[k]].vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_TEXCOORD], me.texcoords, me.vertexCount * 2 * sizeof(float), 0);
    }
}

// The slot holding this pose, skinned into it on a miss (the least recently drawn slot is reused past 16)
static Entry::Slot &slotFor(Entry &e, const ModelAnimation *a, int f, const ModelAnimation *am, int af, const Models::Layers *ly) {
    static unsigned long seq = 0;
    bool lay = ly != nullptr;
    for (Entry::Slot &s : e.slots)
        if (s.a == a && s.f == f && s.am == am && s.af == af && s.lay == lay && (!lay || same(*ly, s.ly))) return s.used = ++seq, s;
    if (e.slots.size() < 16) {
        e.slots.push_back({});
        Entry::Slot &n = e.slots.back();
        int total = 0;
        for (int i = 0; i < e.m.meshCount; i++) total += skinned(e.m.meshes[i]) ? e.m.meshes[i].vertexCount : 0;
        n.pose[0] = rlLoadVertexBuffer(nullptr, total * 3 * sizeof(float), true), n.pose[1] = rlLoadVertexBuffer(nullptr, total * 3 * sizeof(float), true);
        for (int i = 0, at = 0; i < e.m.meshCount; i++) {
            Mesh c = e.m.meshes[i];
            c.vaoId = 0, c.vboId = nullptr;
            UploadMesh(&c, true);
            if (skinned(c)) {  // its VAO reads the shared buffers at its offset
                rlEnableVertexArray(c.vaoId);
                for (int k = 0; k < (c.normals ? 2 : 1); k++) {
                    int loc = k ? RL_DEFAULT_SHADER_ATTRIB_LOCATION_NORMAL : RL_DEFAULT_SHADER_ATTRIB_LOCATION_POSITION;
                    rlEnableVertexBuffer(n.pose[k]);
                    rlSetVertexAttribute(loc, 3, RL_FLOAT, false, 0, at * 3 * (int)sizeof(float));
                    rlUnloadVertexBuffer(c.vboId[loc]), c.vboId[loc] = 0;
                }
                rlDisableVertexArray();
                at += c.vertexCount;
            }
            n.meshes.push_back(c);
        }
    }
    Entry::Slot &s = *std::min_element(e.slots.begin(), e.slots.end(), [](const Entry::Slot &x, const Entry::Slot &y) { return x.used < y.used; });
    skin(e, *a, f, am, af, ly, s.meshes.data(), s.pose);
    s.a = a, s.f = f, s.am = am, s.af = af, s.lay = lay, s.ly = lay ? *ly : Models::Layers{}, s.used = ++seq;
    s.eyeUV[0] = s.eyeUV[1] = s.eyeUV[2] = NAN;  // uploaded below
    return s;
}

bool Models::draw(const char *name, Vector3 pos, float yaw, float pitch, Color tint, const char *clip, float t, bool loop, const char *aim, float aimT, const Layers *ly) {
    auto it = models.find(name);
    if (it == models.end()) return false;
    Entry &e = it->second;
    // CPU skinning into the pose's slot (skinned once while it stays in use)
    int f, af = -1;
    const ModelAnimation *am = aim ? clipFrame(e, aim, aimT, false, &af, false) : nullptr;
    e.m.transform = MatrixMultiply(MatrixRotateX(-pitch), MatrixRotateY(yaw));
    select(e, clip, t, loop, aim, aimT, ly), fade(e, clip, t, loop);
    if (const ModelAnimation *a = clipFrame(e, clip, t, loop, &f); a && e.m.boneMatrices && a->keyframeCount > 0) {
        Entry::Slot &s = slotFor(e, a, f, am, af, ly);
        eyes(e, ly, s.meshes.data(), s.eyeUV);
        Mesh *own = e.m.meshes;
        e.m.meshes = s.meshes.data();
        drawModel(e, pos, tint);
        e.m.meshes = own;
        return true;
    }
    eyes(e, ly, e.m.meshes, e.eyeUV);
    drawModel(e, pos, tint);
    return true;
}

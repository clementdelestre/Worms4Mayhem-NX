#pragma once
#include "raylib.h"
#include "sharp.h"
#include <map>
#include <string>
#include <vector>

struct ChunkGeo;

// Destructible voxel landscape: density field (>0 = solid, ~metres to surface), meshed per chunk (dual contouring where the land is exact).
struct Terrain {
    static constexpr int NX = 320, NY = 256, NZ = 320, CS = 32;
    static constexpr float VOX = 0.25f, WATER = 3.0f;
    static constexpr float Q = 254;  // density stored as int8 = metres * Q, clamped to +-0.5 m

    std::vector<signed char> d;
    SharpLand sharp;  // imported maps: the exact land where the surface runs (.cells); d keeps its signs elsewhere
    struct Part { int mat; Mesh mesh; bool fringe = false; };  // one mesh per (chunk, material), plus its grass fringe cards
    std::vector<std::vector<Part>> parts;
    std::vector<bool> dirty;
    std::vector<std::pair<int, std::vector<Part>>> pending;  // rebuilt chunks held back until the dirty set is done
    // Instant replay (Snapshot): the meshes drawn at the snapshot, kept as rebuilt chunks replace them, then the live
    // ones set aside while the replay draws the kept ones (liveDirty: live chunks not meshed yet)
    std::vector<std::pair<int, std::vector<Part>>> kept, liveKept;
    std::vector<int> liveDirty;
    bool keep = false;
    void keepMeshes();     // Snapshot::take: keeps from now on if every chunk is meshed
    bool rewindMeshes();   // Snapshot::restore to the take: the kept meshes drawn again; false if none were kept
    bool forwardMeshes();  // Snapshot::forward: the live meshes back; false if rewindMeshes() did not run
    std::vector<std::pair<int, signed char>> *undo = nullptr;  // when set, carve() logs (voxel, old density) here
    // girder voxels (W4M kUtilityGirder): ordinary land meshed with theme material 61; empty until a weld()
    std::vector<bool> steel;  // undo logs a voxel turning steel as (-1 - voxel, 0)
    unsigned edits = 0;  // carve() / weld() calls that may have changed a voxel: stamps the AI's per-think caches
    Material mat{};  // loaded on first remesh, so the sim runs without a GL context

    // Filled by load(): fixed spawn points (team-major order), optional race finish, theme palette.
    bool hasFinish = false;
    Vector3 finish{};
    struct Marker { std::string name, type; Vector3 pos; };  // W4M script markers (missions): worm, target, crate, mine...
    std::vector<Marker> markers;
    std::vector<Vector4> blocks;  // W4M AI NodeGrid boxes (x0, z0, x1, z1 in m): heightmap and land pieces, merged (AddLandBlock)
    void addBlock(Vector4 b);     // W4M AddLandBlock 0x4b22e0: drops a box under 250 units², else merges it into the first it touches
    void mergeBlocks();           // W4M PopulatePathingNodes' second merge pass 0x4b2800
    Color sky = {120, 170, 230, 255}, top = {86, 150, 60, 255}, side = {130, 95, 60, 255}, beach = {194, 178, 128, 255};
    std::string theme;  // lowercase theme name (music/<theme>.ogg), empty for the procedural fallback
    std::string time = "day";  // map's "time": day/evening/night (Fx::theme picks the matching sky/water set)
    // Imported maps ("voxels"): per-voxel material (0 = none) indexing palTop/palSide and texture files.
    std::vector<unsigned char> mats;
    std::vector<Color> palTop, palSide;
    std::vector<std::string> texFiles;  // per material: top, side, roof, fringe (paths, "" = none)
    std::vector<Vector2> texRepeat;     // per material: metres per texture repeat (top, side)
    std::vector<Material> texMats, fringeMats;  // per material, built with the textures on first remesh
    float scale = 1;  // import scale: metres per W4M land voxel
    struct Thin { int vox; unsigned char mat; Vector3 c[8]; };  // a sub-voxel W4M cell, drawn while voxel `vox` is solid
    std::vector<std::vector<Thin>> thin;  // per chunk
    std::vector<int> thinOnly;  // sorted voxels solid only for thin cells: not meshed
    std::vector<Texture2D> textures;
    std::map<std::string, Image> decoded;  // decodeTextures() output, uploaded by the first remesh
    int scaleLoc = -1;
    Color grad[2][32] = {};  // W4M LightGradient and side gradient, 32 entries each (loaded on first remesh)
    bool hasGrad = false;
    std::vector<unsigned char> colTop;  // per (x, z) column: 1 + highest solid voxel at remesh time (shadow ray early-out); back() = max
    BoundingBox bounds{};  // solid voxels at the first remesh: the shadow map's land box
    static inline unsigned meshVer = 0;  // bumped whenever rebuilt chunks swap in or the land unloads
    // Map decor (W4M detail objects, no collision): models/decor/<name>.glb, removed by carve().
    struct Object { int model; Vector3 pos; Matrix m; };
    std::vector<Object> objects;
    std::vector<std::string> objModels;
    struct Emitter { std::string fx; Vector3 pos; bool alive = true; };  // W4M EMITTER_ details: PARTTWK effect at pos (render only)
    std::vector<Emitter> emitters;
    Vector3 origin{40, WATER, 40};  // W4M world origin in map metres (map "origin"), for the sky
    float rainProb = -1;  // >= 0: the level script's Particle.Rain.Prob (map "rain_prob")

    bool load(const std::string &map, unsigned seed);  // empty or missing map => generate(seed)
    static std::string mapTheme(const std::string &map);  // the map file's theme without loading it ("" if none)
    void generate(unsigned seed);
    float at(int x, int y, int z) const;
    float sample(Vector3 p) const;  // density: exact in a listed cell, else trilinear
    float field(Vector3 p) const;   // trilinear density only (the mesh)
    static inline thread_local unsigned long samples = 0;  // field() reads (exact-land work in the same unit): the AI's deterministic measure of its own work
    bool solid(Vector3 p) const { return sample(p) > 0; }
    Vector3 normal(Vector3 p, float e = VOX) const;  // the exact face in a listed cell, else -gradient over +-e (e < VOX: VOX where flat)
    bool carve(Vector3 c, float radius);  // true when some voxel changed (W4M Land.Changed)
    void weld(Vector3 c, Vector3 half);  // a solid girder box (W4M Land.SpawnPiece), half extents
    void addCell(const Vector3 *c);  // a convex land cell, exact as an imported map's (corners bit 1 +x, 2 +y, 4 +z)
    bool isSteel(size_t i) const { return !steel.empty() && steel[i]; }
    bool raycast(Ray r, float maxDist, Vector3 *hit) const;
    // the first land along unit dir within len (*t from a, *n its normal); exact where listed, else sampled VOX/4 and bisected to
    // the last point out of land (6e-5 m)
    bool cast(Vector3 a, Vector3 dir, float len, float *t, Vector3 *n) const;
    void decodeTextures();  // CPU only (worker thread): moves the PNG decode out of remesh
    int remesh(double budget = 1e30);  // seconds; past it the rest waits for the next call. Returns the chunks rebuilt
    // Match frames: uploads what the meshing thread built, then hands it the dirty chunks with `budget` s to start new ones
    // (it runs while the frame renders). Returns the chunks uploaded. remeshWait() must precede any voxel change.
    int remeshAsync(double budget);
    void remeshWait();
    void chunkGeometry(int ci, ChunkGeo &g) const;  // CPU only, any thread
    void draw() const;
    void setView(Vector3 cam) const;  // camera for the land and model shaders (W4M Landscape.cg: no fog)
    // clock: s, for the decor clips; draw false: only load the decor models; pick 1 / 2: only still / clip-played decor; at: their spheres
    void drawObjects(float clock, bool draw = true, int pick = 0, std::vector<Vector4> *at = nullptr) const;
    void drawFringe() const;  // W4M LandFringe bin: after the water
    void drawShadow() const;  // the land's depth for Lit::shadowPass
    void unload();

private:
    void reset(signed char fill);
    void island(float bh, float height, float rough, float rad, unsigned s);
    void buildChunk(int ci);
    int collect();       // the meshing thread's chunks into pending
    void swapPending();  // rebuilt chunks replace the drawn ones together
    Color vertexColour(Vector3 p, Vector3 n) const;
    void loadGradients();
    bool loadVoxels(const std::string &path);
    void loadThin(const std::string &path);
    void loadTextures();
};

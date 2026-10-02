#pragma once
#include "raylib.h"
#include <map>
#include <string>
#include <vector>

// Destructible voxel landscape: density field (>0 = solid, ~metres to surface), surface-nets meshed per chunk.
struct Terrain {
    static constexpr int NX = 320, NY = 256, NZ = 320, CS = 32;
    static constexpr float VOX = 0.25f, WATER = 3.0f;
    static constexpr float Q = 254;  // density stored as int8 = metres * Q, clamped to +-0.5 m

    std::vector<signed char> d;
    struct Part { int mat; Mesh mesh; };  // one mesh per (chunk, material)
    std::vector<std::vector<Part>> parts;
    std::vector<bool> dirty;
    std::vector<std::pair<int, std::vector<Part>>> pending;  // rebuilt chunks held back until the dirty set is done
    std::vector<std::pair<int, signed char>> *undo = nullptr;  // when set, carve() logs (voxel, old density) here
    // girder voxels (W4M kUtilityGirder): ordinary land meshed with theme material 61; empty until a weld()
    std::vector<bool> steel;  // undo logs a voxel turning steel as (-1 - voxel, 0)
    Material mat{};  // loaded on first remesh, so the sim runs without a GL context

    // Filled by load(): fixed spawn points (team-major order), optional race finish, theme palette.
    std::vector<Vector3> spawns;
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
    std::vector<std::string> texFiles;  // per material: top, side (paths, "" = none)
    std::vector<Vector2> texRepeat;     // per material: metres per texture repeat (top, side)
    std::vector<Material> texMats;      // per material, built with the textures on first remesh
    std::vector<Texture2D> textures;
    std::map<std::string, Image> decoded;  // decodeTextures() output, uploaded by the first remesh
    int scaleLoc = -1;
    std::vector<unsigned char> colTop;  // per (x, z) column: 1 + highest solid voxel at remesh time (shadow ray early-out); back() = max
    // Map decor (W4M detail objects, no collision): models/decor/<name>.glb, removed by carve().
    struct Object { int model; Vector3 pos; Matrix m; };
    std::vector<Object> objects;
    std::vector<std::string> objModels;

    bool load(const std::string &map, unsigned seed);  // empty or missing map => generate(seed)
    void generate(unsigned seed);
    float at(int x, int y, int z) const;
    float sample(Vector3 p) const;  // trilinear density
    static inline thread_local unsigned long samples = 0;  // sample() calls: the AI's deterministic measure of its own work
    bool solid(Vector3 p) const { return sample(p) > 0; }
    Vector3 normal(Vector3 p) const;
    void carve(Vector3 c, float radius);
    void weld(Vector3 c, Vector3 half);  // a solid girder box (W4M Land.SpawnPiece), half extents
    bool isSteel(size_t i) const { return !steel.empty() && steel[i]; }
    bool raycast(Ray r, float maxDist, Vector3 *hit) const;
    void decodeTextures();  // CPU only (worker thread): moves the PNG decode out of remesh
    void remesh(double budget = 1e30);  // seconds; past it the rest waits for the next call
    void draw() const;
    void setFog(Vector3 cam, Color c, float start, float end) const;  // textured maps only
    void drawObjects(Vector3 cam) const;
    void unload();

private:
    void reset(signed char fill);
    void island(float bh, float height, float rough, float rad, unsigned s);
    void buildChunk(int ci);
    void bake(Vector3 p, Vector3 n, Vector3 l, float *ao, float *vis) const;
    bool loadVoxels(const std::string &path);
    void loadTextures();
};

#include "lit.h"
#include "raymath.h"
#include "rlgl.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#ifdef __SWITCH__
#include <switch.h>
extern "C" void NxSetRenderSize(int width, int height);  // tools/patches/raylib-nx-docked-1080p.patch
extern "C" void glBlitFramebuffer(int, int, int, int, int, int, int, int, unsigned, unsigned);  // GLES3: rlgl's ES2 build leaves it out
#else
extern "C" void glDrawBuffer(unsigned), glReadBuffer(unsigned);
#endif
extern "C" void glGenTextures(int, unsigned *), glBindTexture(unsigned, unsigned), glTexParameteri(unsigned, unsigned, int),
    glTexImage2D(unsigned, int, int, int, int, int, unsigned, unsigned, const void *);

namespace Lit {
Light sun;
int aniso = 16;  // measured free over 4x (docs/tests.md "Render budget")
struct Entry { Shader s; bool worm; int loc[6]; };  // worm: fixed worm light; loc: sunDir, ambient, diffuse, specular, camPos, shadowMatrix
static std::vector<Entry> shaders;

// Worm.Light.Ambient / Worm.Light.Diffuse from Data/Tweak/TWEAK.XOM.
static const Vector3 WORM_AMB = {0.5f, 0.5f, 0.6f}, WORM_DIF = {0.7f, 0.7f, 0.6f};

const char *MVS = R"(
attribute vec3 vertexPosition;
attribute vec2 vertexTexCoord;
attribute vec3 vertexNormal;
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
varying vec2 uv;
varying vec3 n;
varying vec3 wp;
void main() {
    uv = vertexTexCoord; n = (matNormal * vec4(vertexNormal, 0.0)).xyz; wp = (matModel * vec4(vertexPosition, 1.0)).xyz;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";
static const char *MFS = R"(
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec3 sunDir;
uniform vec3 ambient;
uniform vec3 diffuse;
uniform vec3 camPos;
varying vec2 uv;
varying vec3 n;
varying vec3 wp;
void main() {
    vec4 c = texture2D(texture0, uv) * colDiffuse;
    if (c.a < 0.5) discard;
    vec3 nn = normalize(gl_FrontFacing ? n : -n), v = normalize(camPos - wp);
    vec3 l = (diffuse * max(dot(nn, sunDir), 0.0) + ambient) * c.rgb;
    float r = 1.0 - max(dot(v, nn), 0.0);
    gl_FragColor = vec4(l + (0.15 + 0.2 * diffuse) * r * r, 1.0);
}
)";

Shader shader(const char *vs, const char *fs, bool lit, bool es3) {
    bool es = rlGetVersion() == RL_OPENGL_ES_20 || rlGetVersion() == RL_OPENGL_ES_30;
    static const std::string F3 = "#define varying in\n#define texture2D texture\n#define gl_FragColor fragColor\nout vec4 fragColor;\n"
                                  "#define SHADOW_SAMPLER lowp sampler2DShadow\n";
    std::string v = es && !es3 ? "#version 100\n" : std::string(es ? "#version 300 es\n" : "#version 330\n") + "#define attribute in\n#define varying out\n";
    std::string f = es && !es3 ? "#version 100\n#ifdef GL_FRAGMENT_PRECISION_HIGH\nprecision highp float;\n#else\nprecision mediump float;\n#endif\n"
                               : (es ? "#version 300 es\nprecision highp float;\n" : "#version 330\n") + F3;
    Shader s = LoadShaderFromMemory((v + vs).c_str(), (f + fs).c_str());
    if (lit && s.id != rlGetShaderIdDefault()) {
        static const char *U[6] = {"sunDir", "ambient", "diffuse", "specular", "camPos", "shadowMatrix"};
        shaders.push_back({s, false, {}});
        for (int i = 0; i < 6; i++) shaders.back().loc[i] = GetShaderLocation(s, U[i]);
    }
    return s;
}

Shader modelShader(bool worm) {
    Shader s = shader(MVS, MFS);
    if (worm && !shaders.empty() && shaders.back().s.id == s.id) shaders.back().worm = true;
    return s;
}

bool shadows = true;
static const int SHADOW_SIZE = 1024;  // [0x95a100]+0x88: 1024 unless /SHADOWMAP (0x4d5cde)
static unsigned fbo[2], depth[2];  // 0: static casters, 1: plus this frame's casters (sampled by the land)
static Matrix shadowMtx = MatrixTranslate(-2, 0, 0);  // before the first pass: every point off the map, so lit

Texture2D shadowMap() {
    for (int k = 0; k < 2 && !depth[1]; k++) {
        glGenTextures(1, &depth[k]);
        glBindTexture(0x0DE1, depth[k]);  // GL_TEXTURE_2D; unsized GL_DEPTH_COMPONENT: valid on GL 3.3, GLES2 + OES_depth_texture and GLES3
        glTexImage2D(0x0DE1, 0, 0x1902, SHADOW_SIZE, SHADOW_SIZE, 0, 0x1902, 0x1405, nullptr);
        glTexParameteri(0x0DE1, 0x2801, 0x2601), glTexParameteri(0x0DE1, 0x2800, 0x2601);  // linear (0x6f62ac)
        glTexParameteri(0x0DE1, 0x884C, 0x884E);  // GL_COMPARE_R_TO_TEXTURE, default LEQUAL (0x6f644a)
        glBindTexture(0x0DE1, 0);
        fbo[k] = rlLoadFramebuffer();
        rlFramebufferAttach(fbo[k], depth[k], RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);
#ifndef __SWITCH__
        rlEnableFramebuffer(fbo[k]), glDrawBuffer(0), glReadBuffer(0), rlDisableFramebuffer();  // depth only: GL 3.3 completeness
#endif
        if (!rlFramebufferComplete(fbo[k])) shadows = false;
    }
    return {depth[1], SHADOW_SIZE, SHADOW_SIZE, 1, 0};
}

void shadowPass(BoundingBox box, unsigned ver, BoundingBox stale, const std::function<void()> &statics, const std::function<void(std::vector<Vector4> &)> &casters) {
    static unsigned had = ~0u;
    static BoundingBox hadBox{};
    static Matrix view, proj;
    static float ortho[6];  // proj's left, right, bottom, top, near, far
    static std::vector<Vector4> drawn;
    static int dirty[4];  // texel rect of last frame's casters in map 1: only it is restored from map 0 (a full blit costs ~0.35 ms)
    if (shadows) shadowMap();  // may find the depth FBO incomplete
    if (!shadows) return;
    bool fresh = ver != had || memcmp(&box, &hadBox, sizeof box);
    if (fresh) {  // LightingService 0x47cfd8: aimed along -LightVector, ortho over the land box corners in light space x 1.2 (0x47de52)
        view = MatrixLookAt({}, Vector3Negate(Vector3Normalize(LOW_LIGHT)), {0, 1, 0});
        Vector3 lo = {1e9f, 1e9f, 1e9f}, hi = Vector3Negate(lo);
        for (int k = 0; k < 8; k++) {
            Vector3 c = Vector3Transform({k & 1 ? box.max.x : box.min.x, k & 2 ? box.max.y : box.min.y, k & 4 ? box.max.z : box.min.z}, view);
            lo = Vector3Min(lo, c), hi = Vector3Max(hi, c);
        }
        Vector3 c = Vector3Scale(Vector3Add(lo, hi), 0.5f), e = Vector3Scale(Vector3Subtract(hi, lo), 0.6f);  // half of 1.2 x the extent
        const float o[6] = {c.x - e.x, c.x + e.x, c.y - e.y, c.y + e.y, -(c.z + e.z), -(c.z - e.z)};  // near 0, far 1.2 x depth (0x47e351)
        memcpy(ortho, o, sizeof o);
        proj = MatrixOrtho(o[0], o[1], o[2], o[3], o[4], o[5]);
        shadowMtx = MatrixMultiply(MatrixMultiply(view, proj), MatrixMultiply(MatrixScale(0.5f, 0.5f, 0.5f), MatrixTranslate(0.5f, 0.5f, 0.5f)));
    }
    // r: the texel rect drawn (x0, y0, x1, y1), through a projection of that part of the map alone, so culling skips the rest
    auto pass = [&](int k, const std::function<void()> &draw, const int *r = nullptr) {
        BeginTextureMode({fbo[k], {0, SHADOW_SIZE, SHADOW_SIZE, 1, 0}, {depth[k], SHADOW_SIZE, SHADOW_SIZE, 1, 0}});
        rlColorMask(false, false, false, false), rlEnableDepthTest(), rlEnableDepthMask();
        Matrix p = proj;
        if (r) {
            auto at = [](float a, float b, int t) { return a + (b - a) * t / SHADOW_SIZE; };
            p = MatrixOrtho(at(ortho[0], ortho[1], r[0]), at(ortho[0], ortho[1], r[2]), at(ortho[2], ortho[3], r[1]), at(ortho[2], ortho[3], r[3]), ortho[4], ortho[5]);
            rlViewport(r[0], r[1], r[2] - r[0], r[3] - r[1]), rlEnableScissorTest(), rlScissor(r[0], r[1], r[2] - r[0], r[3] - r[1]);
        }
        if (k == 0) rlClearScreenBuffers();
        rlSetMatrixProjection(p), rlSetMatrixModelview(view);
        draw();
        rlDrawRenderBatchActive();
        if (r) rlDisableScissorTest();
        rlColorMask(true, true, true, true), rlDisableDepthTest();
        EndTextureMode();
    };
    if (fresh) had = ver, hadBox = box, pass(0, statics), dirty[0] = dirty[1] = 0, dirty[2] = dirty[3] = SHADOW_SIZE;
    else if (ver != had && stale.min.x <= stale.max.x) {  // land rebuilt: only the texels its chunks cover
        had = ver;
        int r[4] = {SHADOW_SIZE, SHADOW_SIZE, 0, 0};
        for (int k = 0; k < 8; k++) {
            Vector3 c = Vector3Transform({k & 1 ? stale.max.x : stale.min.x, k & 2 ? stale.max.y : stale.min.y, k & 4 ? stale.max.z : stale.min.z}, shadowMtx);
            r[0] = std::min(r[0], (int)floorf(c.x * SHADOW_SIZE) - 1), r[1] = std::min(r[1], (int)floorf(c.y * SHADOW_SIZE) - 1);
            r[2] = std::max(r[2], (int)ceilf(c.x * SHADOW_SIZE) + 1), r[3] = std::max(r[3], (int)ceilf(c.y * SHADOW_SIZE) + 1);
        }
        r[0] = std::max(r[0], 0), r[1] = std::max(r[1], 0), r[2] = std::min(r[2], SHADOW_SIZE), r[3] = std::min(r[3], SHADOW_SIZE);
        if (r[2] > r[0] && r[3] > r[1]) {
            pass(0, statics, r);
            dirty[0] = std::min(dirty[0], r[0]), dirty[1] = std::min(dirty[1], r[1]), dirty[2] = std::max(dirty[2], r[2]), dirty[3] = std::max(dirty[3], r[3]);
        }
    }
    if (int x = dirty[0], y = dirty[1], w = dirty[2] - x, h = dirty[3] - y; w > 0 && h > 0) {
        rlBindFramebuffer(RL_READ_FRAMEBUFFER, fbo[0]), rlBindFramebuffer(RL_DRAW_FRAMEBUFFER, fbo[1]);
#ifdef __SWITCH__
        glBlitFramebuffer(x, y, x + w, y + h, x, y, x + w, y + h, 0x100, 0x2600);  // depth, nearest
#else
        rlBlitFramebuffer(x, y, x + w, y + h, x, y, x + w, y + h, 0x100);  // passes its corners as glBlitFramebuffer's
#endif
        rlDisableFramebuffer();
    }
    drawn.clear();
    pass(1, [&] { casters(drawn); });
    dirty[0] = dirty[1] = SHADOW_SIZE, dirty[2] = dirty[3] = 0;
    for (Vector4 s : drawn) {  // ortho: a sphere covers r x the projection scale around its centre
        Vector3 c = Vector3Transform({s.x, s.y, s.z}, shadowMtx);
        float rx = s.w * fabsf(proj.m0) * 0.5f * SHADOW_SIZE + 2, ry = s.w * fabsf(proj.m5) * 0.5f * SHADOW_SIZE + 2;
        dirty[0] = std::min(dirty[0], std::max(0, (int)(c.x * SHADOW_SIZE - rx))), dirty[1] = std::min(dirty[1], std::max(0, (int)(c.y * SHADOW_SIZE - ry)));
        dirty[2] = std::max(dirty[2], std::min(SHADOW_SIZE, (int)(c.x * SHADOW_SIZE + rx) + 1)), dirty[3] = std::max(dirty[3], std::min(SHADOW_SIZE, (int)(c.y * SHADOW_SIZE + ry) + 1));
    }
}

void frame(Vector3 cam) {
    Vector3 l = Vector3Normalize(sun.dir);
    for (const Entry &x : shaders) {
        SetShaderValue(x.s, x.loc[0], &l, SHADER_UNIFORM_VEC3);
        SetShaderValue(x.s, x.loc[1], x.worm ? &WORM_AMB : &sun.ambient, SHADER_UNIFORM_VEC3);
        SetShaderValue(x.s, x.loc[2], x.worm ? &WORM_DIF : &sun.diffuse, SHADER_UNIFORM_VEC3);
        SetShaderValue(x.s, x.loc[3], &sun.specular, SHADER_UNIFORM_VEC3);
        SetShaderValue(x.s, x.loc[4], &cam, SHADER_UNIFORM_VEC3);
        if (x.loc[5] >= 0) SetShaderValueMatrix(x.s, x.loc[5], shadowMtx);
    }
}

// Docked: a 1920x1080 framebuffer, handheld 1280x720 (docs/tests.md "Render budget"); docked frames averaging over 36 ms
// for 3 s fall back to 720p until the next undock
void profile(float frameTime) {
#ifdef __SWITCH__
    static int was = -1;
    static bool slow = false;
    static float sum = 0;
    static int n = 0;
    bool docked = appletGetOperationMode() == AppletOperationMode_Console;
    if (docked != was) was = docked, slow = FileExists("sdmc:/switch/worms4nx/docked720"), sum = 0, n = 0;  // that file keeps docked play at 720p
    if (docked && frameTime < 0.1f && (sum += frameTime, ++n, sum) >= 3) slow = slow || sum / n > 0.036f, sum = 0, n = 0;  // load hitches skipped
    NxSetRenderSize(docked && !slow ? 1920 : 1280, docked && !slow ? 1080 : 720);
#else
    (void)frameTime;
#endif
}
}

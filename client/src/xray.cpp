#include "xray.h"
#include "lit.h"
#include "models.h"
#include "rlgl.h"
#include <cmath>

extern "C" void glDepthFunc(unsigned), glPolygonOffset(float, float), glEnable(unsigned), glDisable(unsigned), glBlendFunc(unsigned, unsigned),
    glBlendColor(float, float, float, float);

namespace {
const unsigned GL_GREATER_ = 0x204, GL_LEQUAL_ = 0x203, GL_POLYGON_OFFSET_FILL_ = 0x8037, GL_SRC_ALPHA_ = 0x302, GL_ONE_MINUS_SRC_ALPHA_ = 0x303,
               GL_CONSTANT_ALPHA_ = 0x8003, GL_ONE_MINUS_CONSTANT_ALPHA_ = 0x8004;
const float OPAQUE_AT = 2.5f, CLEAR_AT = 1.25f;
const int SLOT = 6;  // MATERIAL_MAP_HEIGHT: never bound by the glb materials

// RT texel: nearest worm depth (r + g/255) and team (b); a = covered.
const char *MASK = R"(
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform float team;
varying vec2 uv;
void main() {
    if (texture2D(texture0, uv).a * colDiffuse.a < 0.5) discard;
    float z = gl_FragCoord.z * 255.0;
    gl_FragColor = vec4(floor(z) / 255.0, fract(z), team, 1.0);
}
)";
// Only the nearest worm layer, so overlapping worm parts blend once, as W4M's full-screen pass does.
const char *HIDE = R"(
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform sampler2D mask;
uniform vec2 px;
varying vec2 uv;
void main() {
    if (texture2D(texture0, uv).a * colDiffuse.a < 0.5) discard;
    vec4 m = texture2D(mask, gl_FragCoord.xy * px);
    if (abs(m.r + m.g / 255.0 - gl_FragCoord.z) > 2e-5) discard;
    gl_FragColor = vec4(0.25, 0.25, 0.25, 0.5);
}
)";
const char *LINE_VS = R"(
attribute vec3 vertexPosition;
attribute vec2 vertexTexCoord;
uniform mat4 mvp;
varying vec2 uv;
void main() { uv = vertexTexCoord; gl_Position = mvp * vec4(vertexPosition, 1.0); }
)";
const char *LINE = R"(
uniform sampler2D texture0;
uniform vec2 px;
uniform vec3 t0, t1, t2, t3;
varying vec2 uv;
void main() {
    vec4 c = texture2D(texture0, uv);
    if (c.a < 0.5) discard;
    float e = min(min(texture2D(texture0, uv - px).a, texture2D(texture0, uv + px).a),
                  min(texture2D(texture0, uv + vec2(px.x, -px.y)).a, texture2D(texture0, uv + vec2(-px.x, px.y)).a));
    if (e > 0.5) discard;
    gl_FragColor = vec4(c.b < 0.125 ? t0 : c.b < 0.375 ? t1 : c.b < 0.625 ? t2 : t3, 0.247);
}
)";

RenderTexture2D rt{};
Shader maskS{}, hideS{}, lineS{};
bool used = false;

void offset(bool on) {
    if (on) glEnable(GL_POLYGON_OFFSET_FILL_), glPolygonOffset(-1, -2);  // hides ties with the worm's own depth
    else glDisable(GL_POLYGON_OFFSET_FILL_);
}
}  // namespace

void Xray::begin() {
    if (!maskS.id) maskS = Lit::shader(Lit::MVS, MASK), hideS = Lit::shader(Lit::MVS, HIDE), lineS = Lit::shader(LINE_VS, LINE);
    int w = GetRenderWidth(), h = GetRenderHeight();
    if (rt.texture.width != w || rt.texture.height != h) {
        if (rt.id) UnloadRenderTexture(rt);
        rt = LoadRenderTexture(w, h);  // nearest filtering: texels are read back exactly
    }
    used = false;
    rlDrawRenderBatchActive();
    rlEnableFramebuffer(rt.id);
    rlClearColor(0, 0, 0, 0);
    rlClearScreenBuffers();
    rlDisableFramebuffer();
}

float Xray::opacity(Vector3 cam, Vector3 worm) {
    float d = sqrtf((cam.x - worm.x) * (cam.x - worm.x) + (cam.y - worm.y) * (cam.y - worm.y) + (cam.z - worm.z) * (cam.z - worm.z));
    float t = (d - CLEAR_AT) / (OPAQUE_AT - CLEAR_AT);
    return t < 0 ? 0 : t > 1 ? 1 : t;
}

void Xray::depth() {
    rlDrawRenderBatchActive();
    rlColorMask(false, false, false, false);
}

void Xray::fade(float op) {
    rlColorMask(true, true, true, true);
    glBlendColor(0, 0, 0, op);
    glBlendFunc(GL_CONSTANT_ALPHA_, GL_ONE_MINUS_CONSTANT_ALPHA_);
}

bool Xray::mask(int team) {
    if (!rt.id || maskS.id == rlGetShaderIdDefault()) return false;
    float t = team * 0.25f;
    SetShaderValue(maskS, GetShaderLocation(maskS, "team"), &t, SHADER_UNIFORM_FLOAT);
    rlDrawRenderBatchActive();
    rlEnableFramebuffer(rt.id);
    offset(true);
    Models::shade(maskS);
    return used = true;
}

void Xray::hidden() {
    rlDisableFramebuffer();
    Vector2 px = {1.0f / rt.texture.width, 1.0f / rt.texture.height};
    int slot = SLOT;
    SetShaderValue(hideS, GetShaderLocation(hideS, "px"), &px, SHADER_UNIFORM_VEC2);
    SetShaderValue(hideS, GetShaderLocation(hideS, "mask"), &slot, SHADER_UNIFORM_INT);
    rlActiveTextureSlot(SLOT), rlEnableTexture(rt.texture.id), rlActiveTextureSlot(0);
    rlDisableDepthMask();
    glDepthFunc(GL_GREATER_);
    Models::shade(hideS);
}

void Xray::done() {
    Models::shade({});
    rlColorMask(true, true, true, true);
    glBlendFunc(GL_SRC_ALPHA_, GL_ONE_MINUS_SRC_ALPHA_);
    glDepthFunc(GL_LEQUAL_);
    rlEnableDepthMask();
    offset(false);
    rlActiveTextureSlot(SLOT), rlDisableTexture(), rlActiveTextureSlot(0);
}

void Xray::outline(const Color *teams) {
    if (!used) return;
    Vector2 px = {1.0f / rt.texture.width, 1.0f / rt.texture.height};
    SetShaderValue(lineS, GetShaderLocation(lineS, "px"), &px, SHADER_UNIFORM_VEC2);
    for (int i = 0; i < 4; i++) {
        Vector3 c = {teams[i].r / 255.0f, teams[i].g / 255.0f, teams[i].b / 255.0f};
        SetShaderValue(lineS, GetShaderLocation(lineS, TextFormat("t%d", i)), &c, SHADER_UNIFORM_VEC3);
    }
    BeginShaderMode(lineS);
    DrawTextureRec(rt.texture, {0, 0, (float)rt.texture.width, -(float)rt.texture.height}, {0, 0}, WHITE);
    EndShaderMode();
}

#include "lit.h"
#include "raymath.h"
#include "rlgl.h"
#include <string>
#include <vector>
#ifdef __SWITCH__
#include <switch.h>
extern "C" void NxSetRenderSize(int width, int height);  // tools/patches/raylib-nx-docked-1080p.patch
#endif

namespace Lit {
Light sun;
int aniso = 16;  // measured free over 4x (docs/tests.md "Render budget")
struct Entry { Shader s; bool worm; int loc[5]; };  // worm: fixed worm light; loc: sunDir, ambient, diffuse, specular, camPos
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

Shader shader(const char *vs, const char *fs, bool lit) {
    bool es = rlGetVersion() == RL_OPENGL_ES_20 || rlGetVersion() == RL_OPENGL_ES_30;
    std::string v = es ? "#version 100\n" : "#version 330\n#define attribute in\n#define varying out\n";
    std::string f = es ? "#version 100\n#ifdef GL_FRAGMENT_PRECISION_HIGH\nprecision highp float;\n#else\nprecision mediump float;\n#endif\n"
                       : "#version 330\n#define varying in\n#define texture2D texture\n#define gl_FragColor fragColor\nout vec4 fragColor;\n";
    Shader s = LoadShaderFromMemory((v + vs).c_str(), (f + fs).c_str());
    if (lit && s.id != rlGetShaderIdDefault()) {
        static const char *U[5] = {"sunDir", "ambient", "diffuse", "specular", "camPos"};
        shaders.push_back({s, false, {}});
        for (int i = 0; i < 5; i++) shaders.back().loc[i] = GetShaderLocation(s, U[i]);
    }
    return s;
}

Shader modelShader(bool worm) {
    Shader s = shader(MVS, MFS);
    if (worm && !shaders.empty() && shaders.back().s.id == s.id) shaders.back().worm = true;
    return s;
}

void frame(Vector3 cam) {
    Vector3 l = Vector3Normalize(sun.dir);
    for (const Entry &x : shaders) {
        SetShaderValue(x.s, x.loc[0], &l, SHADER_UNIFORM_VEC3);
        SetShaderValue(x.s, x.loc[1], x.worm ? &WORM_AMB : &sun.ambient, SHADER_UNIFORM_VEC3);
        SetShaderValue(x.s, x.loc[2], x.worm ? &WORM_DIF : &sun.diffuse, SHADER_UNIFORM_VEC3);
        SetShaderValue(x.s, x.loc[3], &sun.specular, SHADER_UNIFORM_VEC3);
        SetShaderValue(x.s, x.loc[4], &cam, SHADER_UNIFORM_VEC3);
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

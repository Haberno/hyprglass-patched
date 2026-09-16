// Native offscreen regression for opt-in contour antialiasing.
// Requires an available OpenGL ES 3 context; renders no desktop window.
#include "../src/Shaders.hpp"
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QSurfaceFormat>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QSurfaceFormat fmt;
    fmt.setRenderableType(QSurfaceFormat::OpenGLES);
    fmt.setVersion(3, 0);
    QOpenGLContext ctx;
    ctx.setFormat(fmt);
    if (!ctx.create())
        return 2;
    QOffscreenSurface surface;
    surface.setFormat(ctx.format());
    surface.create();
    if (!ctx.makeCurrent(&surface))
        return 2;
    auto *g = ctx.extraFunctions();
    std::cout << "Renderer: " << g->glGetString(GL_RENDERER) << '\n';
    const char *vertex = "#version 300 es\nout vec2 v_texcoord;void main(){vec2 "
                         "p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);v_texcoord=p;"
                         "gl_Position=vec4(p*2.0-1.0,0,1);}";
    auto shader = [&](GLenum type, const char *text) {
        GLuint s = g->glCreateShader(type);
        g->glShaderSource(s, 1, &text, nullptr);
        g->glCompileShader(s);
        GLint good;
        g->glGetShaderiv(s, GL_COMPILE_STATUS, &good);
        if (!good) {
            char log[4096];
            g->glGetShaderInfoLog(s, 4096, nullptr, log);
            std::cerr << log;
            exit(2);
        }
        return s;
    };
    GLuint p = g->glCreateProgram();
    g->glAttachShader(p, shader(GL_VERTEX_SHADER, vertex));
    g->glAttachShader(p, shader(GL_FRAGMENT_SHADER, SHADERS.at("liquidglass.frag")));
    g->glLinkProgram(p);
    g->glUseProgram(p);
    auto f = [&](const char *n, float v) { g->glUniform1f(g->glGetUniformLocation(p, n), v); };
    auto i = [&](const char *n, int v) { g->glUniform1i(g->glGetUniformLocation(p, n), v); };
    auto v2 = [&](const char *n, float x, float y) { g->glUniform2f(g->glGetUniformLocation(p, n), x, y); };
    constexpr int size = 128;
    std::vector<unsigned char> mask(size * size * 4), black(size * size * 4), out(size * size * 4);
    auto smooth = [](double a, double b, double x) {
        double t = std::clamp((x - a) / (b - a), 0., 1.);
        return t * t * (3 - 2 * t);
    };
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            double d = std::hypot(x + .5 - 64, y + .5 - 64) - 27, cover = 1 - smooth(-.7, .8, d);
            double sd = std::hypot(x + .5 - 64, y + .5 - 70) - 27;
            double shadow = std::exp(-std::max(sd, 0.) / 9) * .16;
            int off = 4 * (y * size + x);
            for (int c = 0; c < 3; ++c)
                mask[off + c] = std::lround(255 * .62 * cover);
            mask[off + 3] = std::lround(255 * (.62 * cover + (1 - cover) * shadow));
        }
    GLuint textures[3];
    g->glGenTextures(3, textures);
    for (int t = 0; t < 3; ++t) {
        g->glActiveTexture(GL_TEXTURE0 + t);
        g->glBindTexture(GL_TEXTURE_2D, textures[t]);
        g->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        g->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        g->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        g->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        g->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                        t == 1 ? mask.data() : black.data());
    }
    GLuint fb;
    g->glGenFramebuffers(1, &fb);
    g->glBindFramebuffer(GL_FRAMEBUFFER, fb);
    g->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textures[2], 0);
    if (g->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        return 2;
    GLuint vao;
    g->glGenVertexArrays(1, &vao);
    g->glBindVertexArray(vao);
    g->glViewport(0, 0, size, size);
    g->glClearColor(0, 0, 0, 0);
    g->glClear(GL_COLOR_BUFFER_BIT);
    i("tex", 0);
    i("maskTex", 1);
    i("useMask", 1);
    v2("fullSize", size, size);
    v2("maskUVScale", 1, 1);
    f("maskAlphaThreshold", .20);
    f("contourHeight", 56);
    f("edgeThickness", .19);
    f("roundingPower", 2);
    f("glassOpacity", 1);
    f("brightness", 1);
    f("contrast", 1);
    f("saturation", 1);
    f("tintAlpha", 1);
    g->glUniform3f(g->glGetUniformLocation(p, "tintColor"), 1, 1, 1);
    g->glDrawArrays(GL_TRIANGLES, 0, 3);
    g->glReadPixels(0, 0, size, size, GL_RGBA, GL_UNSIGNED_BYTE, out.data());
    double edgeError = 0;
    int edgeCount = 0;
    int fractional = 0, leaks = 0, solid = 0;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            const double d = std::hypot(x + .5 - 64, y + .5 - 64) - 27;
            const int a = out[4 * (y * size + x) + 3];
            if (std::abs(d) < 1.5) {
                edgeError += std::abs(a / 255.0 - (1 - smooth(-.5, .5, d)));
                ++edgeCount;
            }
            if (std::abs(d) < 1.5 && a > 20 && a < 235)
                ++fractional;
            if (d > 1.5 && a > 0)
                ++leaks;
            if (d < -2 && a == 255)
                ++solid;
        }
    std::cout << "fractional edge pixels=" << fractional << ", outside leaks=" << leaks << ", opaque interior=" << solid
              << '\n';
    std::cout << "mean edge coverage error=" << edgeError / edgeCount << "\n";
    return fractional >= 60 && leaks == 0 && solid > 1900 && edgeError / edgeCount < .08 ? 0 : 1;
}

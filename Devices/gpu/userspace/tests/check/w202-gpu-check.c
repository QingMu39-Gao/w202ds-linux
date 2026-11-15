// SPDX-License-Identifier: MIT
/* Headless hardware check: draw with shaders, then verify every output pixel. */
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define EGL_FUNCTIONS(X) \
 X(eglGetDisplay) X(eglInitialize) X(eglBindAPI) X(eglChooseConfig) \
 X(eglCreatePbufferSurface) X(eglCreateContext) X(eglMakeCurrent) \
 X(eglQueryString) X(eglGetError) X(eglDestroyContext) \
 X(eglDestroySurface) X(eglTerminate)
#define GL_FUNCTIONS(X) \
 X(glGetString) X(glGetError) X(glCreateShader) X(glShaderSource) \
 X(glCompileShader) X(glGetShaderiv) X(glGetShaderInfoLog) X(glCreateProgram) \
 X(glAttachShader) X(glBindAttribLocation) X(glLinkProgram) X(glGetProgramiv) \
 X(glGetProgramInfoLog) X(glUseProgram) X(glGetUniformLocation) X(glUniform4f) \
 X(glGenBuffers) X(glBindBuffer) X(glBufferData) X(glVertexAttribPointer) \
 X(glEnableVertexAttribArray) X(glViewport) X(glDisable) X(glClearColor) \
 X(glClear) X(glDrawArrays) X(glReadPixels) X(glFinish) X(glDeleteBuffers) \
 X(glDeleteProgram) X(glDeleteShader)
#define DECLARE(fn) static __typeof__(fn) *p_##fn;
EGL_FUNCTIONS(DECLARE)
GL_FUNCTIONS(DECLARE)
#undef DECLARE

static void *symbol(void *library, const char *name)
{
    void *address = dlsym(library, name);
    if (!address) {
        fprintf(stderr, "Missing graphics entry: %s\n", name);
        exit(2);
    }
    return address;
}

static GLuint shader(GLenum type, const char *source)
{
    GLuint object = p_glCreateShader(type);
    GLint valid = 0;
    char log[2048];
    p_glShaderSource(object, 1, &source, NULL);
    p_glCompileShader(object);
    p_glGetShaderiv(object, GL_COMPILE_STATUS, &valid);
    if (!valid) {
        p_glGetShaderInfoLog(object, sizeof(log), NULL, log);
        fprintf(stderr, "Shader compilation failed: %s\n", log);
        exit(1);
    }
    return object;
}

static double milliseconds(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000.0 + t.tv_nsec / 1000000.0;
}

int main(int argc, char **argv)
{
    enum { WIDTH = 64, HEIGHT = 64, FRAMES = 100 };
    void *egl, *gles;
    EGLDisplay display;
    EGLConfig config;
    EGLContext context;
    EGLSurface surface;
    EGLint major, minor, count;
    const EGLint attributes[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE
    };
    const EGLint dimensions[] = { EGL_WIDTH, WIDTH, EGL_HEIGHT, HEIGHT, EGL_NONE };
    const EGLint context_attributes[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    const char *renderer;
    const GLfloat vertices[] = { -1.f, -1.f, 3.f, -1.f, -1.f, 3.f };
    unsigned char pixels[WIDTH * HEIGHT * 4];
    GLuint vertex, fragment, program, buffer;
    GLint linked = 0, color;
    double start, elapsed;
    int frame, channel, pixel;

    if (argc != 1) {
        fprintf(stderr, "Usage: %s\n", argv[0]);
        return 2;
    }
    egl = dlopen("libEGL.so.1", RTLD_NOW | RTLD_GLOBAL);
    gles = dlopen("libGLESv2.so.2", RTLD_NOW | RTLD_GLOBAL);
    if (!egl || !gles) {
        fprintf(stderr, "Graphics libraries: %s\n", dlerror());
        return 2;
    }
#define LOAD_EGL(fn) p_##fn = (__typeof__(fn) *)symbol(egl, #fn);
#define LOAD_GL(fn) p_##fn = (__typeof__(fn) *)symbol(gles, #fn);
    EGL_FUNCTIONS(LOAD_EGL)
    GL_FUNCTIONS(LOAD_GL)
#undef LOAD_EGL
#undef LOAD_GL
    display = p_eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY || !p_eglInitialize(display, &major, &minor) ||
        !p_eglBindAPI(EGL_OPENGL_ES_API) ||
        !p_eglChooseConfig(display, attributes, &config, 1, &count) || count != 1) {
        fprintf(stderr, "EGL initialization/config failed: 0x%x\n", p_eglGetError());
        return 1;
    }
    surface = p_eglCreatePbufferSurface(display, config, dimensions);
    context = p_eglCreateContext(display, config, EGL_NO_CONTEXT, context_attributes);
    if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
        !p_eglMakeCurrent(display, surface, surface, context)) {
        fprintf(stderr, "EGL context failed: 0x%x\n", p_eglGetError());
        return 1;
    }
    renderer = (const char *)p_glGetString(GL_RENDERER);
    printf("EGL=%d.%d vendor=%s\nrenderer=%s\nversion=%s\n", major, minor,
           p_eglQueryString(display, EGL_VENDOR), renderer ? renderer : "(none)",
           (const char *)p_glGetString(GL_VERSION));
    if (!renderer || !strstr(renderer, "Panfrost") || !strstr(renderer, "Mali")) {
        fputs("Hardware renderer is not Mali/Panfrost\n", stderr);
        return 1;
    }
    vertex = shader(GL_VERTEX_SHADER,
        "attribute vec2 position; void main(){gl_Position=vec4(position,0.0,1.0);}");
    fragment = shader(GL_FRAGMENT_SHADER,
        "precision mediump float; uniform vec4 color; void main(){gl_FragColor=color;}");
    program = p_glCreateProgram();
    p_glAttachShader(program, vertex);
    p_glAttachShader(program, fragment);
    p_glBindAttribLocation(program, 0, "position");
    p_glLinkProgram(program);
    p_glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[2048];
        p_glGetProgramInfoLog(program, sizeof(log), NULL, log);
        fprintf(stderr, "Program link failed: %s\n", log);
        return 1;
    }
    p_glUseProgram(program);
    color = p_glGetUniformLocation(program, "color");
    if (color < 0) {
        fputs("Missing shader uniform\n", stderr);
        return 1;
    }
    p_glGenBuffers(1, &buffer);
    p_glBindBuffer(GL_ARRAY_BUFFER, buffer);
    p_glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    p_glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    p_glEnableVertexAttribArray(0);
    p_glViewport(0, 0, WIDTH, HEIGHT);
    p_glDisable(GL_DITHER);
    p_glClearColor(0.f, 0.f, 0.f, 0.f);
    start = milliseconds();
    for (frame = 0; frame < FRAMES; frame++) {
        const unsigned char expected[] = {
            (frame * 37 + 19) % 256, (frame * 71 + 83) % 256,
            (frame * 113 + 149) % 256, 255
        };
        p_glUniform4f(color, expected[0] / 255.f, expected[1] / 255.f,
                     expected[2] / 255.f, 1.f);
        p_glClear(GL_COLOR_BUFFER_BIT);
        p_glDrawArrays(GL_TRIANGLES, 0, 3);
        p_glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        if (p_glGetError() != GL_NO_ERROR) {
            fprintf(stderr, "GL error in frame %d\n", frame);
            return 1;
        }
        for (pixel = 0; pixel < WIDTH * HEIGHT; pixel++)
            for (channel = 0; channel < 4; channel++)
                if (abs((int)pixels[pixel * 4 + channel] - expected[channel]) > 1) {
                    fprintf(stderr, "Readback mismatch frame=%d pixel=%d channel=%d got=%u expected=%u\n",
                            frame, pixel, channel, pixels[pixel * 4 + channel], expected[channel]);
                    return 1;
                }
    }
    p_glFinish();
    elapsed = milliseconds() - start;
    printf("frames=%d pixels_verified=%d elapsed_ms=%.3f mean_ms=%.3f\n",
           FRAMES, WIDTH * HEIGHT * FRAMES, elapsed, elapsed / FRAMES);
    puts("Timing includes synchronous readback and CPU verification; not display FPS.");
    p_glDeleteBuffers(1, &buffer);
    p_glDeleteProgram(program);
    p_glDeleteShader(vertex);
    p_glDeleteShader(fragment);
    p_eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    p_eglDestroyContext(display, context);
    p_eglDestroySurface(display, surface);
    p_eglTerminate(display);
    puts("W202_GPU_SHADER_READBACK_PASS");
    return 0;
}

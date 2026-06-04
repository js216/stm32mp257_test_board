// cube.c -- GPU-rendered rotating cube, fullscreen via DRM/KMS (no compositor).
//
// For the STM32MP257 EVB (Vivante GC8000, OpenSTLinux). Drives the LVDS panel
// directly: EGL on a GBM scanout surface, GLES2 draws the cube on the GPU, and
// frames are presented with DRM atomic page-flips synced to vblank. Between
// flips the CPU sleeps in poll(), so it holds 60 fps at near-zero CPU -- the
// per-frame compositor cost of the windowed version is gone.
//
// Requires DRM master, so Weston must be stopped first:
//   systemctl stop  weston-graphical-session.service
//   ./cube                      # Ctrl-C or SIGTERM to quit
//   systemctl start weston-graphical-session.service
//
// Build: see tools/Makefile (cross-compiled for aarch64).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <poll.h>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

// ---- DRM / GBM / EGL state ------------------------------------------------

static int drm_fd = -1;
static drmModeModeInfo mode;
static uint32_t connector_id;
static uint32_t crtc_id;
static drmModeCrtc *orig_crtc;          // saved to restore on clean exit

static struct gbm_device  *gbm_dev;
static struct gbm_surface *gbm_surf;

static EGLDisplay edpy;
static EGLContext ectx;
static EGLSurface esurf;

static volatile sig_atomic_t running = 1;
static void on_signal(int s) { (void)s; running = 0; }

// ---- DRM setup ------------------------------------------------------------

static int drm_init(const char *path)
{
    drm_fd = open(path, O_RDWR | O_CLOEXEC);
    if (drm_fd < 0) { fprintf(stderr, "open %s: %s\n", path, strerror(errno)); return -1; }

    drmModeRes *res = drmModeGetResources(drm_fd);
    if (!res) { fprintf(stderr, "drmModeGetResources failed (is Weston stopped?)\n"); return -1; }

    // Pick the first connected connector and its preferred mode.
    drmModeConnector *conn = NULL;
    for (int i = 0; i < res->count_connectors; i++) {
        drmModeConnector *c = drmModeGetConnector(drm_fd, res->connectors[i]);
        if (c && c->connection == DRM_MODE_CONNECTED && c->count_modes > 0) {
            conn = c;
            break;
        }
        if (c) drmModeFreeConnector(c);
    }
    if (!conn) { fprintf(stderr, "no connected connector\n"); drmModeFreeResources(res); return -1; }
    connector_id = conn->connector_id;

    int best = 0;
    for (int i = 0; i < conn->count_modes; i++)
        if (conn->modes[i].type & DRM_MODE_TYPE_PREFERRED) { best = i; break; }
    mode = conn->modes[best];

    // Find a CRTC: use the connector's current encoder if set, else search.
    crtc_id = 0;
    if (conn->encoder_id) {
        drmModeEncoder *enc = drmModeGetEncoder(drm_fd, conn->encoder_id);
        if (enc) { crtc_id = enc->crtc_id; drmModeFreeEncoder(enc); }
    }
    for (int i = 0; !crtc_id && i < conn->count_encoders; i++) {
        drmModeEncoder *enc = drmModeGetEncoder(drm_fd, conn->encoders[i]);
        if (!enc) continue;
        for (int c = 0; c < res->count_crtcs; c++) {
            if (enc->possible_crtcs & (1 << c)) { crtc_id = res->crtcs[c]; break; }
        }
        drmModeFreeEncoder(enc);
    }
    if (!crtc_id) { fprintf(stderr, "no CRTC for connector\n"); return -1; }

    orig_crtc = drmModeGetCrtc(drm_fd, crtc_id);
    drmModeFreeConnector(conn);
    drmModeFreeResources(res);

    fprintf(stderr, "DRM: connector %u, crtc %u, mode %s %dx%d@%d\n",
            connector_id, crtc_id, mode.name, mode.hdisplay, mode.vdisplay,
            mode.vrefresh);
    return 0;
}

// Wrap a GBM buffer object in a DRM framebuffer, caching the fb id on the bo.
struct fb_wrap { uint32_t fb_id; };

static void bo_destroy_cb(struct gbm_bo *bo, void *data)
{
    (void)bo;
    struct fb_wrap *fb = data;
    if (fb) {
        if (fb->fb_id) drmModeRmFB(drm_fd, fb->fb_id);
        free(fb);
    }
}

static uint32_t fb_for_bo(struct gbm_bo *bo)
{
    struct fb_wrap *fb = gbm_bo_get_user_data(bo);
    if (fb) return fb->fb_id;

    uint32_t w = gbm_bo_get_width(bo);
    uint32_t h = gbm_bo_get_height(bo);
    uint32_t stride = gbm_bo_get_stride(bo);
    uint32_t handle = gbm_bo_get_handle(bo).u32;

    fb = calloc(1, sizeof(*fb));
    if (drmModeAddFB(drm_fd, w, h, 24, 32, stride, handle, &fb->fb_id)) {
        fprintf(stderr, "drmModeAddFB failed: %s\n", strerror(errno));
        free(fb);
        return 0;
    }
    gbm_bo_set_user_data(bo, fb, bo_destroy_cb);
    return fb->fb_id;
}

// ---- EGL setup ------------------------------------------------------------

static void egl_init(void)
{
    gbm_dev = gbm_create_device(drm_fd);
    if (!gbm_dev) { fprintf(stderr, "gbm_create_device failed\n"); exit(1); }

    gbm_surf = gbm_surface_create(gbm_dev, mode.hdisplay, mode.vdisplay,
                                  GBM_FORMAT_XRGB8888,
                                  GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    if (!gbm_surf) { fprintf(stderr, "gbm_surface_create failed\n"); exit(1); }

    edpy = eglGetDisplay((EGLNativeDisplayType)gbm_dev);
    if (edpy == EGL_NO_DISPLAY || !eglInitialize(edpy, NULL, NULL)) {
        fprintf(stderr, "eglInitialize failed\n"); exit(1);
    }
    eglBindAPI(EGL_OPENGL_ES_API);

    const EGLint cfg_attr[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_DEPTH_SIZE, 16,
        EGL_NONE,
    };
    // Pick a config whose native visual matches the GBM format.
    EGLConfig cfgs[32];
    EGLint n = 0;
    if (!eglChooseConfig(edpy, cfg_attr, cfgs, 32, &n) || n < 1) {
        fprintf(stderr, "eglChooseConfig failed\n"); exit(1);
    }
    EGLConfig cfg = cfgs[0];
    for (int i = 0; i < n; i++) {
        EGLint vid = 0;
        eglGetConfigAttrib(edpy, cfgs[i], EGL_NATIVE_VISUAL_ID, &vid);
        if (vid == (EGLint)GBM_FORMAT_XRGB8888) { cfg = cfgs[i]; break; }
    }

    const EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    ectx = eglCreateContext(edpy, cfg, EGL_NO_CONTEXT, ctx_attr);
    if (ectx == EGL_NO_CONTEXT) { fprintf(stderr, "eglCreateContext failed\n"); exit(1); }

    esurf = eglCreateWindowSurface(edpy, cfg,
                                   (EGLNativeWindowType)gbm_surf, NULL);
    if (esurf == EGL_NO_SURFACE) { fprintf(stderr, "eglCreateWindowSurface failed\n"); exit(1); }

    eglMakeCurrent(edpy, esurf, esurf, ectx);
}

// ---- 4x4 column-major matrix helpers --------------------------------------

static void mat_identity(float *m)
{
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}
static void mat_mul(float *r, const float *a, const float *b)
{
    float t[16];
    for (int c = 0; c < 4; c++)
        for (int row = 0; row < 4; row++)
            t[c * 4 + row] =
                a[0 * 4 + row] * b[c * 4 + 0] + a[1 * 4 + row] * b[c * 4 + 1] +
                a[2 * 4 + row] * b[c * 4 + 2] + a[3 * 4 + row] * b[c * 4 + 3];
    memcpy(r, t, sizeof(t));
}
static void mat_perspective(float *m, float fovy_deg, float aspect, float zn, float zf)
{
    float f = 1.0f / tanf(fovy_deg * (float)M_PI / 360.0f);
    mat_identity(m);
    m[0] = f / aspect; m[5] = f;
    m[10] = (zf + zn) / (zn - zf); m[11] = -1.0f;
    m[14] = (2.0f * zf * zn) / (zn - zf); m[15] = 0.0f;
}
static void mat_translate(float *m, float x, float y, float z)
{
    mat_identity(m); m[12] = x; m[13] = y; m[14] = z;
}
static void mat_rotate(float *m, float ax, float ay)
{
    float cx = cosf(ax), sx = sinf(ax), cy = cosf(ay), sy = sinf(ay);
    float rx[16], ry[16];
    mat_identity(rx); rx[5] = cx; rx[6] = sx; rx[9] = -sx; rx[10] = cx;
    mat_identity(ry); ry[0] = cy; ry[2] = -sy; ry[8] = sy; ry[10] = cy;
    mat_mul(m, ry, rx);
}

// ---- Cube geometry + shaders ----------------------------------------------

static const GLfloat cube_verts[] = {
    -1,-1, 1,  1,0,0,   1,-1, 1,  1,0,0,   1, 1, 1,  1,0,0,
    -1,-1, 1,  1,0,0,   1, 1, 1,  1,0,0,  -1, 1, 1,  1,0,0,
    -1,-1,-1,  0,1,0,  -1, 1,-1,  0,1,0,   1, 1,-1,  0,1,0,
    -1,-1,-1,  0,1,0,   1, 1,-1,  0,1,0,   1,-1,-1,  0,1,0,
     1,-1,-1,  0,0,1,   1, 1,-1,  0,0,1,   1, 1, 1,  0,0,1,
     1,-1,-1,  0,0,1,   1, 1, 1,  0,0,1,   1,-1, 1,  0,0,1,
    -1,-1,-1,  1,1,0,  -1,-1, 1,  1,1,0,  -1, 1, 1,  1,1,0,
    -1,-1,-1,  1,1,0,  -1, 1, 1,  1,1,0,  -1, 1,-1,  1,1,0,
    -1, 1,-1,  0,1,1,  -1, 1, 1,  0,1,1,   1, 1, 1,  0,1,1,
    -1, 1,-1,  0,1,1,   1, 1, 1,  0,1,1,   1, 1,-1,  0,1,1,
    -1,-1,-1,  1,0,1,   1,-1,-1,  1,0,1,   1,-1, 1,  1,0,1,
    -1,-1,-1,  1,0,1,   1,-1, 1,  1,0,1,  -1,-1, 1,  1,0,1,
};
static const char *vert_src =
    "uniform mat4 u_mvp;\n"
    "attribute vec3 a_pos;\n"
    "attribute vec3 a_col;\n"
    "varying vec3 v_col;\n"
    "void main(){ v_col=a_col; gl_Position=u_mvp*vec4(a_pos,1.0); }\n";
static const char *frag_src =
    "precision mediump float;\n"
    "varying vec3 v_col;\n"
    "void main(){ gl_FragColor=vec4(v_col,1.0); }\n";

static GLint u_mvp;

static GLuint compile(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char l[512]; glGetShaderInfoLog(s, 512, NULL, l); fprintf(stderr, "shader: %s\n", l); exit(1); }
    return s;
}
static void gl_init(void)
{
    GLuint prog = glCreateProgram();
    glAttachShader(prog, compile(GL_VERTEX_SHADER, vert_src));
    glAttachShader(prog, compile(GL_FRAGMENT_SHADER, frag_src));
    glBindAttribLocation(prog, 0, "a_pos");
    glBindAttribLocation(prog, 1, "a_col");
    glLinkProgram(prog);
    glUseProgram(prog);
    u_mvp = glGetUniformLocation(prog, "u_mvp");

    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cube_verts), cube_verts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat), (void *)0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat), (void *)(3 * sizeof(GLfloat)));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
    glViewport(0, 0, mode.hdisplay, mode.vdisplay);
}

static double now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
static void draw(void)
{
    static double t0 = -1.0;
    if (t0 < 0) t0 = now_sec();
    float t = (float)(now_sec() - t0);

    float proj[16], view[16], model[16], mv[16], mvp[16];
    mat_perspective(proj, 45.0f, (float)mode.hdisplay / mode.vdisplay, 1.0f, 100.0f);
    mat_translate(view, 0.0f, 0.0f, -6.0f);
    mat_rotate(model, t * 0.7f, t * 1.1f);
    mat_mul(mv, view, model);
    mat_mul(mvp, proj, mv);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUniformMatrix4fv(u_mvp, 1, GL_FALSE, mvp);
    glDrawArrays(GL_TRIANGLES, 0, 36);
}

// ---- page flip ------------------------------------------------------------

static void page_flip_handler(int fd, unsigned int seq, unsigned int sec,
                              unsigned int usec, void *data)
{
    (void)fd; (void)seq; (void)sec; (void)usec;
    *(int *)data = 0;   // flip completed
}

int main(int argc, char **argv)
{
    const char *card = (argc > 1) ? argv[1] : "/dev/dri/card0";

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    if (drm_init(card)) return 1;
    drmSetMaster(drm_fd);   // root + free card: become master (ignore failure)

    egl_init();
    gl_init();

    // First frame: render, lock a scanout buffer, and mode-set the CRTC.
    draw();
    eglSwapBuffers(edpy, esurf);
    struct gbm_bo *bo = gbm_surface_lock_front_buffer(gbm_surf);
    uint32_t fb = fb_for_bo(bo);
    if (drmModeSetCrtc(drm_fd, crtc_id, fb, 0, 0, &connector_id, 1, &mode)) {
        fprintf(stderr, "drmModeSetCrtc failed: %s\n", strerror(errno));
        return 1;
    }

    drmEventContext ev = { .version = 2, .page_flip_handler = page_flip_handler };
    struct pollfd pfd = { .fd = drm_fd, .events = POLLIN };

    while (running) {
        draw();
        eglSwapBuffers(edpy, esurf);
        struct gbm_bo *next = gbm_surface_lock_front_buffer(gbm_surf);
        uint32_t next_fb = fb_for_bo(next);

        int waiting = 1;
        if (drmModePageFlip(drm_fd, crtc_id, next_fb,
                            DRM_MODE_PAGE_FLIP_EVENT, &waiting)) {
            fprintf(stderr, "page flip failed: %s\n", strerror(errno));
            gbm_surface_release_buffer(gbm_surf, next);
            break;
        }
        // Sleep until vblank delivers the flip event -- no busy spin.
        while (waiting && running) {
            if (poll(&pfd, 1, 1000) > 0)
                drmHandleEvent(drm_fd, &ev);
        }
        gbm_surface_release_buffer(gbm_surf, bo);
        bo = next;
    }

    // Restore the framebuffer Weston had, so the handover back is clean.
    if (orig_crtc)
        drmModeSetCrtc(drm_fd, orig_crtc->crtc_id, orig_crtc->buffer_id,
                       orig_crtc->x, orig_crtc->y, &connector_id, 1, &orig_crtc->mode);

    eglMakeCurrent(edpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(edpy, esurf);
    eglDestroyContext(edpy, ectx);
    eglTerminate(edpy);
    gbm_surface_release_buffer(gbm_surf, bo);
    gbm_surface_destroy(gbm_surf);
    gbm_device_destroy(gbm_dev);
    drmDropMaster(drm_fd);
    close(drm_fd);
    return 0;
}

// fft.c -- GPU spectrum analyzer, fullscreen via DRM/KMS (no compositor).
//
// For the STM32MP257 EVB (Vivante GC8000, OpenSTLinux). Same direct-display
// path as cube.c: EGL on a GBM scanout surface, frames presented with DRM
// page-flips synced to vblank, so it holds 60 fps at near-zero CPU.
//
// Each frame it generates a fresh window of a synthetic signal (a few stable
// tones + a swept tone + noise), takes a Hann-windowed real FFT with libfftw3f,
// converts to dBFS, and draws a spectrum-analyzer view on the GPU: a dB/freq
// graticule, a colour-ramped filled trace (green->yellow->red), a bright trace
// line, and a slowly-decaying peak-hold line.
//
// The FFT runs on the CPU (libfftw3f, single precision, NEON) -- cheap for a
// 2048-point transform; all drawing is on the GPU.
//
// Requires DRM master, so stop Weston first:
//   systemctl stop  weston-graphical-session.service
//   ./fft                       # Ctrl-C / SIGTERM to quit
//   systemctl start weston-graphical-session.service
//
// Build: see tools/Makefile.

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
#include <stddef.h>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <fftw3.h>

// ---- analyzer parameters --------------------------------------------------

#define NFFT   2048               // FFT size (power of two)
#define NMASK  (NFFT - 1)
#define NBIN   (NFFT / 2)         // displayed bins (1024 -> ~1 bin/px @1024w)
#define FS     48000.0f          // synthetic sample rate (Hz)
#define HOP    800                // new samples per frame (~FS/60, real-time)
#define DB_MIN (-90.0f)
#define DB_MAX (0.0f)

// ---- DRM / GBM / EGL state ------------------------------------------------

static int drm_fd = -1;
static drmModeModeInfo mode;
static uint32_t connector_id, crtc_id;
static drmModeCrtc *orig_crtc;

static struct gbm_device  *gbm_dev;
static struct gbm_surface *gbm_surf;
static EGLDisplay edpy;
static EGLContext ectx;
static EGLSurface esurf;

static volatile sig_atomic_t running = 1;
static void on_signal(int s) { (void)s; running = 0; }

// ---- DRM setup (identical approach to cube.c) -----------------------------

static int drm_init(const char *path)
{
    drm_fd = open(path, O_RDWR | O_CLOEXEC);
    if (drm_fd < 0) { fprintf(stderr, "open %s: %s\n", path, strerror(errno)); return -1; }
    drmModeRes *res = drmModeGetResources(drm_fd);
    if (!res) { fprintf(stderr, "drmModeGetResources failed (is Weston stopped?)\n"); return -1; }

    drmModeConnector *conn = NULL;
    for (int i = 0; i < res->count_connectors; i++) {
        drmModeConnector *c = drmModeGetConnector(drm_fd, res->connectors[i]);
        if (c && c->connection == DRM_MODE_CONNECTED && c->count_modes > 0) { conn = c; break; }
        if (c) drmModeFreeConnector(c);
    }
    if (!conn) { fprintf(stderr, "no connected connector\n"); return -1; }
    connector_id = conn->connector_id;

    int best = 0;
    for (int i = 0; i < conn->count_modes; i++)
        if (conn->modes[i].type & DRM_MODE_TYPE_PREFERRED) { best = i; break; }
    mode = conn->modes[best];

    crtc_id = 0;
    if (conn->encoder_id) {
        drmModeEncoder *e = drmModeGetEncoder(drm_fd, conn->encoder_id);
        if (e) { crtc_id = e->crtc_id; drmModeFreeEncoder(e); }
    }
    for (int i = 0; !crtc_id && i < conn->count_encoders; i++) {
        drmModeEncoder *e = drmModeGetEncoder(drm_fd, conn->encoders[i]);
        if (!e) continue;
        for (int c = 0; c < res->count_crtcs; c++)
            if (e->possible_crtcs & (1 << c)) { crtc_id = res->crtcs[c]; break; }
        drmModeFreeEncoder(e);
    }
    if (!crtc_id) { fprintf(stderr, "no CRTC\n"); return -1; }

    orig_crtc = drmModeGetCrtc(drm_fd, crtc_id);
    drmModeFreeConnector(conn);
    drmModeFreeResources(res);
    fprintf(stderr, "DRM: connector %u, crtc %u, mode %s %dx%d@%d\n",
            connector_id, crtc_id, mode.name, mode.hdisplay, mode.vdisplay, mode.vrefresh);
    return 0;
}

struct fb_wrap { uint32_t fb_id; };
static void bo_destroy_cb(struct gbm_bo *bo, void *data)
{
    (void)bo;
    struct fb_wrap *fb = data;
    if (fb) { if (fb->fb_id) drmModeRmFB(drm_fd, fb->fb_id); free(fb); }
}
static uint32_t fb_for_bo(struct gbm_bo *bo)
{
    struct fb_wrap *fb = gbm_bo_get_user_data(bo);
    if (fb) return fb->fb_id;
    uint32_t w = gbm_bo_get_width(bo), h = gbm_bo_get_height(bo);
    uint32_t stride = gbm_bo_get_stride(bo), handle = gbm_bo_get_handle(bo).u32;
    fb = calloc(1, sizeof(*fb));
    if (drmModeAddFB(drm_fd, w, h, 24, 32, stride, handle, &fb->fb_id)) {
        fprintf(stderr, "drmModeAddFB failed: %s\n", strerror(errno)); free(fb); return 0;
    }
    gbm_bo_set_user_data(bo, fb, bo_destroy_cb);
    return fb->fb_id;
}

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
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE,
    };
    EGLConfig cfgs[32]; EGLint n = 0;
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
    esurf = eglCreateWindowSurface(edpy, cfg, (EGLNativeWindowType)gbm_surf, NULL);
    if (esurf == EGL_NO_SURFACE) { fprintf(stderr, "eglCreateWindowSurface failed\n"); exit(1); }
    eglMakeCurrent(edpy, esurf, esurf, ectx);
}

// ---- GLES2: one program, optional vertical colour ramp --------------------

static const char *vert_src =
    "attribute vec2 a_pos;\n"
    "varying float v_h;\n"
    "void main(){ v_h=(a_pos.y+1.0)*0.5; gl_Position=vec4(a_pos,0.0,1.0); }\n";
static const char *frag_src =
    "precision mediump float;\n"
    "varying float v_h;\n"
    "uniform vec4 u_color;\n"
    "uniform float u_ramp;\n"
    "void main(){\n"
    "  if(u_ramp>0.5){\n"
    "    vec3 lo=vec3(0.0,0.9,0.35), mid=vec3(0.95,0.95,0.1), hi=vec3(1.0,0.2,0.1);\n"
    "    vec3 c = v_h<0.5 ? mix(lo,mid,v_h*2.0) : mix(mid,hi,(v_h-0.5)*2.0);\n"
    "    gl_FragColor=vec4(c, 0.82);\n"
    "  } else gl_FragColor=u_color;\n"
    "}\n";

static GLint u_color, u_ramp;

static float fill_v[2 * NBIN * 2];   // (x,y) bottom/top pairs (triangle strip)
static float line_v[NBIN * 2];       // (x,y) trace
static float peak_v[NBIN * 2];       // (x,y) peak-hold
static float peak_db[NBIN];

static GLuint compile(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL); glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char l[512]; glGetShaderInfoLog(s, 512, NULL, l); fprintf(stderr, "shader: %s\n", l); exit(1); }
    return s;
}

static GLuint vbo_fill, vbo_line, vbo_peak, vbo_grid;
static int grid_count;

static void build_grid(void)
{
    float g[512]; int k = 0;
    // horizontal dB lines every 10 dB
    for (int d = 0; d >= -90; d -= 10) {
        float y = -1.0f + 2.0f * (d - DB_MIN) / (DB_MAX - DB_MIN);
        g[k++] = -1.0f; g[k++] = y; g[k++] = 1.0f; g[k++] = y;
    }
    // vertical freq lines every 2 kHz (0..24 kHz)
    for (float f = 0.0f; f <= FS / 2.0f; f += 2000.0f) {
        float x = -1.0f + 2.0f * (f / (FS / 2.0f));
        g[k++] = x; g[k++] = -1.0f; g[k++] = x; g[k++] = 1.0f;
    }
    grid_count = k / 2;
    glGenBuffers(1, &vbo_grid);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_grid);
    glBufferData(GL_ARRAY_BUFFER, k * sizeof(float), g, GL_STATIC_DRAW);
}

static void gl_init(void)
{
    GLuint prog = glCreateProgram();
    glAttachShader(prog, compile(GL_VERTEX_SHADER, vert_src));
    glAttachShader(prog, compile(GL_FRAGMENT_SHADER, frag_src));
    glBindAttribLocation(prog, 0, "a_pos");
    glLinkProgram(prog); glUseProgram(prog);
    u_color = glGetUniformLocation(prog, "u_color");
    u_ramp  = glGetUniformLocation(prog, "u_ramp");
    glEnableVertexAttribArray(0);

    glGenBuffers(1, &vbo_fill); glGenBuffers(1, &vbo_line); glGenBuffers(1, &vbo_peak);
    // Pre-size the dynamic buffers once; per-frame we only glBufferSubData.
    glBindBuffer(GL_ARRAY_BUFFER, vbo_fill);
    glBufferData(GL_ARRAY_BUFFER, sizeof(fill_v), NULL, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_line);
    glBufferData(GL_ARRAY_BUFFER, sizeof(line_v), NULL, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_peak);
    glBufferData(GL_ARRAY_BUFFER, sizeof(peak_v), NULL, GL_DYNAMIC_DRAW);
    build_grid();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.03f, 0.04f, 0.05f, 1.0f);
    glViewport(0, 0, mode.hdisplay, mode.vdisplay);
}

// ---- signal + FFT ---------------------------------------------------------

static float        han[NFFT];
static float       *fin;
static fftwf_complex *fout;
static fftwf_plan    plan;
static float        wsum;             // window sum, for amplitude normalization

// Recurrence ("coupled-form") oscillator: each sample is the real part of a
// unit phasor (cr,ci) rotated per sample by (wr,wi)=e^{i*2*pi*f/FS}. No sin()
// in the inner loop -- just two mults and an add per sample per oscillator.
typedef struct { float cr, ci, wr, wi, amp; } osc_t;

static const float ftone[3] = { 3000.0f, 7000.0f, 11000.0f };
static const float atone[3] = { 1.00f, 0.55f, 0.35f };
static osc_t tone[3];
static osc_t sweep;                 // freq updated each frame

static float ring[NFFT];            // circular buffer of generated samples
static int   ring_head;             // index of oldest sample in the window

static void osc_set_freq(osc_t *o, float f)
{
    o->wr = cosf(2.0f * (float)M_PI * f / FS);
    o->wi = sinf(2.0f * (float)M_PI * f / FS);
}
static inline float osc_step(osc_t *o)
{
    float s = o->amp * o->cr;
    float nr = o->cr * o->wr - o->ci * o->wi;
    float ni = o->cr * o->wi + o->ci * o->wr;
    o->cr = nr; o->ci = ni;
    return s;
}
static inline void osc_renorm(osc_t *o)
{
    float m = 1.0f / sqrtf(o->cr * o->cr + o->ci * o->ci);
    o->cr *= m; o->ci *= m;
}

static void signal_init(void)
{
    for (int i = 0; i < NFFT; i++) {
        han[i] = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * i / (NFFT - 1));
        wsum += han[i];
    }
    for (int k = 0; k < 3; k++) {
        tone[k].cr = 1.0f; tone[k].ci = 0.0f; tone[k].amp = atone[k];
        osc_set_freq(&tone[k], ftone[k]);
    }
    sweep.cr = 1.0f; sweep.ci = 0.0f; sweep.amp = 0.8f;
    osc_set_freq(&sweep, 5000.0f);

    fin  = fftwf_alloc_real(NFFT);
    fout = fftwf_alloc_complex(NFFT / 2 + 1);
    plan = fftwf_plan_dft_r2c_1d(NFFT, fin, fout, FFTW_ESTIMATE);
    for (int i = 0; i < NBIN; i++) peak_db[i] = DB_MIN;
}

static float frand(void) { return (float)rand() / RAND_MAX * 2.0f - 1.0f; }

// Push HOP new samples into the ring, then copy the latest NFFT windowed.
static void gen_samples(double t)
{
    for (int k = 0; k < 3; k++)
        tone[k].amp = atone[k] * (0.85f + 0.15f * sinf(0.7f * (float)t + k));
    float fsweep = 1000.0f + 9500.0f * (0.5f + 0.5f * sinf(0.25f * (float)t));
    osc_set_freq(&sweep, fsweep);

    for (int n = 0; n < HOP; n++) {
        float s = osc_step(&tone[0]) + osc_step(&tone[1]) + osc_step(&tone[2])
                + osc_step(&sweep) + 0.02f * frand();
        ring[ring_head] = s;
        ring_head = (ring_head + 1) & NMASK;
    }
    for (int k = 0; k < 3; k++) osc_renorm(&tone[k]);
    osc_renorm(&sweep);

    for (int j = 0; j < NFFT; j++)
        fin[j] = ring[(ring_head + j) & NMASK] * han[j];
}

static double now_sec(void)
{
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static void draw(void)
{
    static double t0 = -1.0;
    if (t0 < 0) t0 = now_sec();
    double t = now_sec() - t0;

    gen_samples(t);
    fftwf_execute(plan);

    float scale = 2.0f / wsum;
    for (int i = 0; i < NBIN; i++) {
        float re = fout[i][0], im = fout[i][1];
        float mag = sqrtf(re * re + im * im) * scale;
        float db = 20.0f * log10f(mag + 1e-9f);
        if (db < DB_MIN) db = DB_MIN;
        if (db > DB_MAX) db = DB_MAX;
        float x = -1.0f + 2.0f * (float)i / (NBIN - 1);
        float y = -1.0f + 2.0f * (db - DB_MIN) / (DB_MAX - DB_MIN);

        fill_v[4 * i + 0] = x; fill_v[4 * i + 1] = -1.0f;   // bottom
        fill_v[4 * i + 2] = x; fill_v[4 * i + 3] = y;       // top
        line_v[2 * i + 0] = x; line_v[2 * i + 1] = y;

        peak_db[i] -= 0.35f;                                // peak-hold decay
        if (db > peak_db[i]) peak_db[i] = db;
        float yp = -1.0f + 2.0f * (peak_db[i] - DB_MIN) / (DB_MAX - DB_MIN);
        peak_v[2 * i + 0] = x; peak_v[2 * i + 1] = yp;
    }

    glClear(GL_COLOR_BUFFER_BIT);

    // graticule
    glUniform1f(u_ramp, 0.0f);
    glUniform4f(u_color, 0.18f, 0.22f, 0.26f, 1.0f);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_grid);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glDrawArrays(GL_LINES, 0, grid_count);

    // filled spectrum (colour ramp)
    glUniform1f(u_ramp, 1.0f);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_fill);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(fill_v), fill_v);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 2 * NBIN);

    // trace line
    glUniform1f(u_ramp, 0.0f);
    glUniform4f(u_color, 0.85f, 1.0f, 0.9f, 1.0f);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_line);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(line_v), line_v);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glDrawArrays(GL_LINE_STRIP, 0, NBIN);

    // peak-hold line
    glUniform4f(u_color, 1.0f, 0.85f, 0.2f, 0.9f);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_peak);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(peak_v), peak_v);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glDrawArrays(GL_LINE_STRIP, 0, NBIN);
}

// ---- page flip ------------------------------------------------------------

static void page_flip_handler(int fd, unsigned seq, unsigned sec, unsigned usec, void *data)
{
    (void)fd; (void)seq; (void)sec; (void)usec; *(int *)data = 0;
}

int main(int argc, char **argv)
{
    const char *card = (argc > 1) ? argv[1] : "/dev/dri/card0";
    signal(SIGINT, on_signal); signal(SIGTERM, on_signal);

    if (drm_init(card)) return 1;
    drmSetMaster(drm_fd);
    egl_init();
    gl_init();
    signal_init();

    draw();
    eglSwapBuffers(edpy, esurf);
    struct gbm_bo *bo = gbm_surface_lock_front_buffer(gbm_surf);
    if (drmModeSetCrtc(drm_fd, crtc_id, fb_for_bo(bo), 0, 0, &connector_id, 1, &mode)) {
        fprintf(stderr, "drmModeSetCrtc failed: %s\n", strerror(errno)); return 1;
    }

    drmEventContext ev = { .version = 2, .page_flip_handler = page_flip_handler };
    struct pollfd pfd = { .fd = drm_fd, .events = POLLIN };

    while (running) {
        draw();
        eglSwapBuffers(edpy, esurf);
        struct gbm_bo *next = gbm_surface_lock_front_buffer(gbm_surf);
        int waiting = 1;
        if (drmModePageFlip(drm_fd, crtc_id, fb_for_bo(next), DRM_MODE_PAGE_FLIP_EVENT, &waiting)) {
            fprintf(stderr, "page flip failed: %s\n", strerror(errno));
            gbm_surface_release_buffer(gbm_surf, next); break;
        }
        while (waiting && running)
            if (poll(&pfd, 1, 1000) > 0) drmHandleEvent(drm_fd, &ev);
        gbm_surface_release_buffer(gbm_surf, bo);
        bo = next;
    }

    if (orig_crtc)
        drmModeSetCrtc(drm_fd, orig_crtc->crtc_id, orig_crtc->buffer_id,
                       orig_crtc->x, orig_crtc->y, &connector_id, 1, &orig_crtc->mode);
    eglMakeCurrent(edpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(edpy, esurf); eglDestroyContext(edpy, ectx); eglTerminate(edpy);
    gbm_surface_release_buffer(gbm_surf, bo);
    gbm_surface_destroy(gbm_surf); gbm_device_destroy(gbm_dev);
    fftwf_destroy_plan(plan); fftwf_free(fin); fftwf_free(fout);
    drmDropMaster(drm_fd); close(drm_fd);
    return 0;
}

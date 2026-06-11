// fft_cpu.c -- CPU spectrum analyzer, fullscreen via DRM/KMS, NO GPU / NO OpenGL.
//
// For the STM32MP257F-DK (OpenSTLinux or the lean Buildroot image). Renders the
// spectrum entirely on the Cortex-A35 into a DRM "dumb" framebuffer (plain CPU
// pixel writes), double-buffered and page-flipped to the LVDS panel. Only depends
// on libdrm + libfftw3f -- no EGL/GLES/gbm, no Vivante blobs.
//
// Each frame: generate a synthetic signal (coupled-form oscillators), Hann-window
// + real FFT (libfftw3f), convert to dBFS, then draw a graticule + colour-ramped
// filled spectrum + peak-hold line straight into the XRGB8888 buffer.
//
// Requires DRM master, so stop Weston first if running OpenSTLinux:
//   systemctl --user -M root@ stop weston   (or: systemctl stop weston-graphical-session)
//   ./fft_cpu                # Ctrl-C / SIGTERM to quit
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
#include <stdint.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm/drm_mode.h>
#include <fftw3.h>

// ---- analyzer parameters --------------------------------------------------
#define NFFT   2048
#define NMASK  (NFFT - 1)
#define NBIN   (NFFT / 2)
#define FS     48000.0f
#define HOP    800
#define DB_MIN (-90.0f)
#define DB_MAX (0.0f)

// ---- DRM state ------------------------------------------------------------
static int drm_fd = -1;
static drmModeModeInfo mode;
static uint32_t conn_id, crtc_id;
static drmModeCrtc *orig_crtc;
static int W, H;

struct fb {
	uint32_t fb_id, handle, pitch;
	uint64_t size;
	uint32_t *px;     // mmap'd XRGB8888 pixels
};
static struct fb bufs[2];

static volatile sig_atomic_t running = 1;
static void on_signal(int s) { (void)s; running = 0; }

static int create_fb(struct fb *b)
{
	struct drm_mode_create_dumb creq;
	memset(&creq, 0, sizeof(creq));
	creq.width = W; creq.height = H; creq.bpp = 32;
	if (drmIoctl(drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq)) return -1;
	b->handle = creq.handle; b->pitch = creq.pitch; b->size = creq.size;
	if (drmModeAddFB(drm_fd, W, H, 24, 32, creq.pitch, creq.handle, &b->fb_id)) return -1;
	struct drm_mode_map_dumb mreq;
	memset(&mreq, 0, sizeof(mreq));
	mreq.handle = creq.handle;
	if (drmIoctl(drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq)) return -1;
	b->px = mmap(0, creq.size, PROT_READ | PROT_WRITE, MAP_SHARED, drm_fd, mreq.offset);
	if (b->px == MAP_FAILED) return -1;
	return 0;
}

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
	conn_id = conn->connector_id;
	int best = 0;
	for (int i = 0; i < conn->count_modes; i++)
		if (conn->modes[i].type & DRM_MODE_TYPE_PREFERRED) { best = i; break; }
	mode = conn->modes[best];
	W = mode.hdisplay; H = mode.vdisplay;

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

	if (create_fb(&bufs[0]) || create_fb(&bufs[1])) {
		fprintf(stderr, "dumb buffer alloc failed: %s\n", strerror(errno)); return -1;
	}
	fprintf(stderr, "DRM: conn %u crtc %u %dx%d@%u, pitch %u\n", conn_id, crtc_id, W, H,
	        mode.vrefresh, bufs[0].pitch);
	return 0;
}

// ---- drawing primitives (XRGB8888) ----------------------------------------
static inline uint32_t rgb(int r, int g, int b)
{
	return ((uint32_t)(r & 0xff) << 16) | ((uint32_t)(g & 0xff) << 8) | (uint32_t)(b & 0xff);
}
static void clear(struct fb *b, uint32_t c)
{
	uint32_t stride = b->pitch / 4;
	for (int y = 0; y < H; y++) {
		uint32_t *row = b->px + (size_t)y * stride;
		for (int x = 0; x < W; x++) row[x] = c;
	}
}
static inline void vline(struct fb *b, int x, int y0, int y1, uint32_t c)
{
	if (x < 0 || x >= W) return;
	if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
	if (y0 < 0) y0 = 0;
	if (y1 >= H) y1 = H - 1;
	uint32_t stride = b->pitch / 4;
	for (int y = y0; y <= y1; y++) b->px[(size_t)y * stride + x] = c;
}
static inline void hline(struct fb *b, int x0, int x1, int y, uint32_t c)
{
	if (y < 0 || y >= H) return;
	if (x0 < 0) x0 = 0;
	if (x1 >= W) x1 = W - 1;
	uint32_t *row = b->px + (size_t)y * (b->pitch / 4);
	for (int x = x0; x <= x1; x++) row[x] = c;
}
static inline void putpx(struct fb *b, int x, int y, uint32_t c)
{
	if ((unsigned)x < (unsigned)W && (unsigned)y < (unsigned)H)
		b->px[(size_t)y * (b->pitch / 4) + x] = c;
}

// colour ramp green->yellow->red by normalized height h in [0,1]
static uint32_t ramp(float h)
{
	if (h < 0) h = 0;
	if (h > 1) h = 1;
	int r, g, bl;
	if (h < 0.5f) { float t = h * 2.0f; r = (int)(t * 242); g = 230; bl = (int)(90 * (1 - t)) + 20; }
	else { float t = (h - 0.5f) * 2.0f; r = 242 + (int)(t * 13); g = (int)(230 * (1 - t)) + 25; bl = 25; }
	return rgb(r, g, bl);
}

// ---- signal + FFT (coupled-form oscillators) ------------------------------
static float han[NFFT];
static float *fin;
static fftwf_complex *fout;
static fftwf_plan plan;
static float wsum;
static float peak_db[NBIN];

typedef struct { float cr, ci, wr, wi, amp; } osc_t;
static const float ftone[3] = { 3000.0f, 7000.0f, 11000.0f };
static const float atone[3] = { 1.00f, 0.55f, 0.35f };
static osc_t tone[3], sweep;
static float ring[NFFT];
static int ring_head;

static void osc_freq(osc_t *o, float f)
{
	o->wr = cosf(2.0f * (float)M_PI * f / FS);
	o->wi = sinf(2.0f * (float)M_PI * f / FS);
}
static inline float osc_step(osc_t *o)
{
	float s = o->amp * o->cr;
	float nr = o->cr * o->wr - o->ci * o->wi, ni = o->cr * o->wi + o->ci * o->wr;
	o->cr = nr; o->ci = ni;
	return s;
}
static inline void osc_renorm(osc_t *o)
{
	float m = 1.0f / sqrtf(o->cr * o->cr + o->ci * o->ci);
	o->cr *= m; o->ci *= m;
}
static float frand(void) { return (float)rand() / RAND_MAX * 2.0f - 1.0f; }

static void signal_init(void)
{
	for (int i = 0; i < NFFT; i++) {
		han[i] = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * i / (NFFT - 1));
		wsum += han[i];
	}
	for (int k = 0; k < 3; k++) { tone[k].cr = 1; tone[k].ci = 0; tone[k].amp = atone[k]; osc_freq(&tone[k], ftone[k]); }
	sweep.cr = 1; sweep.ci = 0; sweep.amp = 0.8f; osc_freq(&sweep, 5000.0f);
	fin = fftwf_alloc_real(NFFT);
	fout = fftwf_alloc_complex(NFFT / 2 + 1);
	plan = fftwf_plan_dft_r2c_1d(NFFT, fin, fout, FFTW_ESTIMATE);
	for (int i = 0; i < NBIN; i++) peak_db[i] = DB_MIN;
}
static void gen_samples(double t)
{
	for (int k = 0; k < 3; k++) tone[k].amp = atone[k] * (0.85f + 0.15f * sinf(0.7f * (float)t + k));
	osc_freq(&sweep, 1000.0f + 9500.0f * (0.5f + 0.5f * sinf(0.25f * (float)t)));
	for (int n = 0; n < HOP; n++) {
		float s = osc_step(&tone[0]) + osc_step(&tone[1]) + osc_step(&tone[2]) + osc_step(&sweep) + 0.02f * frand();
		ring[ring_head] = s; ring_head = (ring_head + 1) & NMASK;
	}
	for (int k = 0; k < 3; k++) osc_renorm(&tone[k]);
	osc_renorm(&sweep);
	for (int j = 0; j < NFFT; j++) fin[j] = ring[(ring_head + j) & NMASK] * han[j];
}

// ---- render one spectrum frame into buffer b ------------------------------
static void draw(struct fb *b, double t)
{
	gen_samples(t);
	fftwf_execute(plan);

	clear(b, rgb(8, 10, 14));

	// graticule: horizontal dB lines every 10 dB, vertical every ~2 kHz
	for (int d = 0; d >= -90; d -= 10) {
		int y = (int)((float)(H - 1) * (1.0f - (d - DB_MIN) / (DB_MAX - DB_MIN)));
		hline(b, 0, W - 1, y, rgb(40, 48, 58));
	}
	for (float f = 0; f <= FS / 2; f += 2000.0f) {
		int x = (int)((float)(W - 1) * (f / (FS / 2)));
		vline(b, x, 0, H - 1, rgb(40, 48, 58));
	}

	float scale = 2.0f / wsum;
	int prev_y = H - 1;
	for (int x = 0; x < W; x++) {
		int i = (int)((long)x * (NBIN - 1) / (W - 1));
		float re = fout[i][0], im = fout[i][1];
		float mag = sqrtf(re * re + im * im) * scale;
		float db = 20.0f * log10f(mag + 1e-9f);
		if (db < DB_MIN) db = DB_MIN;
		if (db > DB_MAX) db = DB_MAX;
		float hnorm = (db - DB_MIN) / (DB_MAX - DB_MIN);
		int y_top = (int)((float)(H - 1) * (1.0f - hnorm));

		// filled bar with vertical colour ramp
		uint32_t stride = b->pitch / 4;
		for (int y = H - 1; y >= y_top; y--) {
			float hh = (float)(H - 1 - y) / (H - 1);
			b->px[(size_t)y * stride + x] = ramp(hh);
		}
		// bright trace line connecting tops
		putpx(b, x, y_top, rgb(220, 255, 235));
		if (x > 0) vline(b, x, prev_y, y_top, rgb(220, 255, 235));
		prev_y = y_top;

		// peak-hold (decaying), yellow
		peak_db[i] -= 0.35f;
		if (db > peak_db[i]) peak_db[i] = db;
		int yp = (int)((float)(H - 1) * (1.0f - (peak_db[i] - DB_MIN) / (DB_MAX - DB_MIN)));
		putpx(b, x, yp, rgb(255, 215, 60));
	}
}

// ---- page flip ------------------------------------------------------------
static void flip_handler(int fd, unsigned seq, unsigned s, unsigned us, void *d)
{ (void)fd; (void)seq; (void)s; (void)us; *(int *)d = 0; }

static double now_sec(void)
{
	struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec * 1e-9;
}

int main(int argc, char **argv)
{
	const char *card = (argc > 1) ? argv[1] : "/dev/dri/card0";
	signal(SIGINT, on_signal); signal(SIGTERM, on_signal);
	if (drm_init(card)) return 1;
	drmSetMaster(drm_fd);
	signal_init();

	double t0 = now_sec();
	int front = 0;
	draw(&bufs[front], 0);
	if (drmModeSetCrtc(drm_fd, crtc_id, bufs[front].fb_id, 0, 0, &conn_id, 1, &mode)) {
		fprintf(stderr, "drmModeSetCrtc failed: %s\n", strerror(errno)); return 1;
	}

	drmEventContext ev = { .version = 2, .page_flip_handler = flip_handler };
	struct pollfd pfd = { .fd = drm_fd, .events = POLLIN };

	unsigned frames = 0;
	double tfps = now_sec(), tdraw = 0.0;
	while (running) {
		int back = front ^ 1;
		double td0 = now_sec();
		draw(&bufs[back], td0 - t0);
		tdraw += now_sec() - td0;
		int waiting = 1;
		if (drmModePageFlip(drm_fd, crtc_id, bufs[back].fb_id, DRM_MODE_PAGE_FLIP_EVENT, &waiting)) {
			fprintf(stderr, "page flip failed: %s\n", strerror(errno)); break;
		}
		while (waiting && running)
			if (poll(&pfd, 1, 1000) > 0) drmHandleEvent(drm_fd, &ev);
		front = back;

		if (++frames == 30) {
			double t = now_sec();
			fprintf(stderr, "FPS %.1f (draw %.1f ms/frame)\n",
			        frames / (t - tfps), 1e3 * tdraw / frames);
			frames = 0; tfps = t; tdraw = 0.0;
		}
	}

	if (orig_crtc)
		drmModeSetCrtc(drm_fd, orig_crtc->crtc_id, orig_crtc->buffer_id, orig_crtc->x, orig_crtc->y,
		               &conn_id, 1, &orig_crtc->mode);
	drmDropMaster(drm_fd);
	close(drm_fd);
	return 0;
}

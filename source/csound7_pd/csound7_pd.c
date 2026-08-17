// csound7~ for Pure Data / plugdata — Csound 7 statically embedded, same
// engine and same lessons as the Max/MSP version (../../MAX/source/
// csound7_tilde/csound7_tilde.c), ported to Pd's own API (m_pd.h /
// g_canvas.h) instead of the Max SDK.
//
// FIRST DRAFT, NOT YET COMPILED - written the same way the Max side
// started: a full pass based on real, working Csound-side logic (the
// option-forcing order, the 0dbfs/CsOptions timing fix, the ring buffer,
// the MIDI queue - all identical to the Max version, since that part is
// pure Csound API, not host-specific), translated to Pd's own object
// model. Expect real compiler/runtime errors on the first build, same as
// happened repeatedly on the Max side - that's the normal next step, not
// a sign anything here was done carelessly. Search this file for "VERIFY"
// for the handful of spots where I'm least certain of the exact Pd API
// shape (currently just the array-resize call in tab2buf - the plugin-
// folder lookup used to be flagged here too, fixed after reading Pd's own
// m_class.c source directly, see csound7_load_plugins_dir() below).
//
// Key design differences from the Max version, on purpose:
//   - Control messages (buf2tab, event, amp 0.3, ...) go to the LEFTMOST
//     inlet here, not a dedicated rightmost one - Pd's proxy-inlet trick
//     for a "control only" inlet is real but adds real risk on a first,
//     untested pass, and putting control on inlet 0 (which also carries
//     audio channel 1) is itself completely idiomatic Pd. Extra signal
//     inlets 2..N are plain audio-only.
//   - No buffer~ - Pd's equivalent is an "array" (t_garray), accessed via
//     g_canvas.h instead of ext_buffer.h.
//   - No Macintosh-HD-style hybrid paths to clean up - Pd's own file
//     dialogs already return plain POSIX paths, so csound7_conform_path()
//     from the Max side simply isn't needed here.

#include "m_pd.h"
#include "g_canvas.h"   // t_garray, garray_getfloatarray, garray_redraw,
                         // canvas_getdir - all "semi-internal" but this is
                         // the standard, long-established way externals
                         // reach into arrays and ask a canvas for its own
                         // directory; tabread~/tabwrite~ and many
                         // third-party externals rely on the same header.
#include "csound.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Windows/MSVC ships none of strings.h, unistd.h or pthread.h - found one
// at a time via real GitHub Actions Windows CI failures (each one only
// surfaces once the previous is fixed and the build gets further), fixed
// here with small local shims rather than vendoring a whole compatibility
// library (e.g. pthreads-win32) for the sake of one mutex and two one-line
// calls.
#ifdef _WIN32
  #include <windows.h>   // CRITICAL_SECTION - stand-in for pthread_mutex_t
  #include <io.h>        // _close() - stand-in for close()
  #define strcasecmp _stricmp
  #define cs7_close  _close
  typedef CRITICAL_SECTION cs7_mutex_t;
  #define cs7_mutex_init(m)    InitializeCriticalSection(m)
  #define cs7_mutex_destroy(m) DeleteCriticalSection(m)
  #define cs7_mutex_lock(m)    EnterCriticalSection(m)
  #define cs7_mutex_unlock(m)  LeaveCriticalSection(m)
  // TryEnterCriticalSection returns nonzero on SUCCESS - opposite of
  // pthread_mutex_trylock, which returns 0 on success. Negate it once
  // here so every call site below can keep pthread's convention
  // ("if (!cs7_mutex_trylock(...))" means "if I got the lock").
  #define cs7_mutex_trylock(m) (!TryEnterCriticalSection(m))
#else
  #include <strings.h>   // strcasecmp
  #include <unistd.h>    // close()
  #include <pthread.h>
  #define cs7_close close
  typedef pthread_mutex_t cs7_mutex_t;
  #define cs7_mutex_init(m)    pthread_mutex_init(m, NULL)
  #define cs7_mutex_destroy(m) pthread_mutex_destroy(m)
  #define cs7_mutex_lock(m)    pthread_mutex_lock(m)
  #define cs7_mutex_unlock(m)  pthread_mutex_unlock(m)
  #define cs7_mutex_trylock(m) pthread_mutex_trylock(m)
#endif

#define CS7_MAX_PATH 4096
#define CS7_MAX_CHANNELS 16
#define CS7_DEFAULT_KSMPS 32
#define CS7_MIDI_QUEUE_SIZE 4096

static t_class *csound7_class = NULL;

typedef struct _csound7
{
    t_object    x_obj;
    t_float     x_f;              // CLASS_MAINSIGNALIN dummy (float-when-
                                   // unpatched value for inlet 0 / channel 1)

    CSOUND     *csound;
    int         cs_started;
    int         running;          // 1 = perform calls csoundPerformKsmps, 0 = paused
    double      cs_sr;
    long        ksmps;
    int         ksmps_forced;
    long        msg_level;

    long        nchnls_out;
    long        nchnls_in;

    t_symbol   *orc_path;
    t_symbol   *owner_dir;         // this patch's own folder, from canvas_getdir()

    // --- input/output ring buffer, decouples ksmps from Pd's block size ---
    MYFLT     **rb_in;             // [nchnls_in][rb_size]
    MYFLT     **rb_out;            // [nchnls_out][rb_size]
    long        rb_size;
    long        rb_in_write;
    long        rb_in_read;
    long        rb_in_filled;
    long        rb_out_write;
    long        rb_out_read;
    long        rb_out_filled;

    // --- lock-free single-producer/single-consumer MIDI queue ---
    unsigned char midi_queue[CS7_MIDI_QUEUE_SIZE];
    volatile long midi_head;
    volatile long midi_tail;

    cs7_mutex_t engine_lock;

    t_outlet   *ctl_out;            // rightmost outlet, "anything" (channel dumps etc)

} t_csound7;

// ---------------------------------------------------------------------
// prototypes
// ---------------------------------------------------------------------
static void *csound7_new(t_symbol *s, int argc, t_atom *argv);
static void  csound7_free(t_csound7 *x);

static void  csound7_dsp(t_csound7 *x, t_signal **sp);
static t_int *csound7_perform(t_int *w);

static void  csound7_anything(t_csound7 *x, t_symbol *s, int argc, t_atom *argv);
static void  csound7_list(t_csound7 *x, t_symbol *s, int argc, t_atom *argv);

static void csound7_do_compile(t_csound7 *x, t_symbol *path);
static void csound7_do_reset(t_csound7 *x);
static void csound7_dump_channels(t_csound7 *x);
static void csound7_buf2tab(t_csound7 *x, t_symbol *arrayname, long tabnum);
static void csound7_tab2buf(t_csound7 *x, long tabnum, t_symbol *arrayname);
static void csound7_diskinfile(t_csound7 *x, t_symbol *chan, t_symbol *path);
static void csound7_start_engine(t_csound7 *x, double sr);
static void csound7_apply_csoptions(CSOUND *csound, t_csound7 *x, const char *path);
static void csound7_load_plugins_dir(t_csound7 *x);
static long csound7_scan_nchnls(const char *path, const char *token, long fallback);
static t_symbol *csound7_resolve_path(t_csound7 *x, t_symbol *filename);
static void csound7_rb_alloc(t_csound7 *x, long size);
static void csound7_rb_free(t_csound7 *x);

static int32_t cs7_midi_in_open(CSOUND *csound, void **userData, const char *devName);
static int32_t cs7_midi_in_close(CSOUND *csound, void *userData);
static int32_t cs7_midi_read(CSOUND *csound, void *userData, unsigned char *buf, int32_t nBytes);

static void cs7_msg_callback(CSOUND *csound, int32_t attr, const char *fmt, va_list args);

// ---------------------------------------------------------------------
// setup — Pd's entry point naming convention: object "csound7~" ->
// function "csound7_tilde_setup" (the "~" becomes "_tilde").
// ---------------------------------------------------------------------
void csound7_tilde_setup(void)
{
    // Same reasoning as csoundInitialize(CSOUNDINIT_NO_SIGNAL_HANDLER |
    // CSOUNDINIT_NO_ATEXIT) on the Max side: must happen before the first
    // csoundCreate() anywhere in this external's lifetime, otherwise
    // Csound installs its own signal handlers / atexit hook, which call
    // exit() directly on certain conditions and would take down the whole
    // Pd (or plugdata) process, not just this object.
    csoundInitialize(CSOUNDINIT_NO_SIGNAL_HANDLER | CSOUNDINIT_NO_ATEXIT);

    csound7_class = class_new(gensym("csound7~"),
        (t_newmethod)csound7_new, (t_method)csound7_free,
        sizeof(t_csound7), CLASS_DEFAULT, A_GIMME, 0);

    CLASS_MAINSIGNALIN(csound7_class, t_csound7, x_f);

    class_addmethod(csound7_class, (t_method)csound7_dsp, gensym("dsp"), A_CANT, 0);

    class_addanything(csound7_class, csound7_anything);
    class_addlist(csound7_class, csound7_list);
}

// ---------------------------------------------------------------------
// new / free
// ---------------------------------------------------------------------
static void *csound7_new(t_symbol *s, int argc, t_atom *argv)
{
    (void)s;
    t_csound7 *x = (t_csound7 *)pd_new(csound7_class);
    if (!x) return NULL;

    x->csound       = NULL;
    x->cs_started   = 0;
    x->running      = 1;
    x->cs_sr        = 0.0;
    x->ksmps        = CS7_DEFAULT_KSMPS;
    x->ksmps_forced = 0;
    x->msg_level    = 0;
    x->orc_path     = NULL;
    x->owner_dir    = NULL;
    x->rb_in        = NULL;
    x->rb_out       = NULL;
    x->rb_size      = 0;
    x->midi_head    = 0;
    x->midi_tail    = 0;
    x->x_f          = 0;
    cs7_mutex_init(&x->engine_lock);

    // canvas_getcurrent() only works correctly while Pd is actually in the
    // middle of instantiating this object (i.e. right here, in the "new"
    // method) - grabbing the owning patch's own folder now, since we can't
    // reliably ask for it again later the way Max's jpatcher_get_filepath()
    // can be called any time via the #P lookup.
    t_canvas *owner = canvas_getcurrent();
    x->owner_dir = owner ? canvas_getdir(owner) : NULL;

    // first argument: path to the .csd/.orc file. second (optional): ksmps.
    if (argc >= 1 && argv[0].a_type == A_SYMBOL) {
        x->orc_path = csound7_resolve_path(x, atom_getsymbol(argv));
    } else {
        pd_error(x, "csound7~: a .csd/.orc file is required as the first argument");
        x->orc_path = NULL;
    }
    if (argc >= 2 && argv[1].a_type == A_FLOAT) {
        long v = (long)atom_getfloat(argv + 1);
        if (v >= 1) {
            x->ksmps = v;
            x->ksmps_forced = 1;
        }
    }

    // derive nchnls/nchnls_i from the file BEFORE creating inlets/outlets -
    // same reasoning as the Max side: Pd's inlet/outlet count is also
    // fixed at object-creation time.
    const char *path = x->orc_path ? x->orc_path->s_name : NULL;
    x->nchnls_out = path ? csound7_scan_nchnls(path, "nchnls", 2) : 2;
    x->nchnls_in  = path ? csound7_scan_nchnls(path, "nchnls_i", x->nchnls_out) : 2;
    if (x->nchnls_out < 1) x->nchnls_out = 1;
    if (x->nchnls_in  < 1) x->nchnls_in  = 1;
    if (x->nchnls_out > CS7_MAX_CHANNELS) {
        pd_error(x, "csound7~: nchnls %ld exceeds the %d channel safety limit, clamping",
            x->nchnls_out, CS7_MAX_CHANNELS);
        x->nchnls_out = CS7_MAX_CHANNELS;
    }
    if (x->nchnls_in > CS7_MAX_CHANNELS) {
        pd_error(x, "csound7~: nchnls_i %ld exceeds the %d channel safety limit, clamping",
            x->nchnls_in, CS7_MAX_CHANNELS);
        x->nchnls_in = CS7_MAX_CHANNELS;
    }

    // inlet 0 (channel 1 + all control messages) is created automatically
    // by CLASS_MAINSIGNALIN above. Extra signal-only inlets for channels
    // 2..nchnls_in, left-to-right in the order created (unlike Max, Pd
    // does NOT reverse inlet/outlet creation order).
    for (long ch = 1; ch < x->nchnls_in; ch++) {
        inlet_new(&x->x_obj, &x->x_obj.ob_pd, &s_signal, &s_signal);
    }

    // audio outlets, channel 1..N left to right, then one more "anything"
    // outlet at the far right for channel dumps / future use (mirrors the
    // Max version's third outlet).
    for (long ch = 0; ch < x->nchnls_out; ch++) {
        outlet_new(&x->x_obj, &s_signal);
    }
    x->ctl_out = outlet_new(&x->x_obj, 0);

    // create the Csound instance right away, with a provisional sr (Pd's
    // real one arrives later via the "dsp" method / t_signal**).
    csound7_start_engine(x, sys_getsr() > 0 ? sys_getsr() : 44100.0);

    return (void *)x;
}

static void csound7_free(t_csound7 *x)
{
    if (x->csound) {
        csoundDestroy(x->csound);
        x->csound = NULL;
    }
    csound7_rb_free(x);
    cs7_mutex_destroy(&x->engine_lock);
}

// ---------------------------------------------------------------------
// start the Csound engine — identical option-forcing order/reasoning to
// the Max version (this part is pure Csound API, not host-specific: the
// csoundStart()-before-compile ordering, the CsOptions timing fix, and
// the 0dbfs bug all apply exactly the same way here).
// ---------------------------------------------------------------------
static void csound7_start_engine(t_csound7 *x, double sr)
{
    cs7_mutex_lock(&x->engine_lock);

    if (x->csound) {
        csoundDestroy(x->csound);
        x->csound = NULL;
    }

    x->csound = csoundCreate(x, NULL);
    if (!x->csound) {
        pd_error(x, "csound7~: csoundCreate failed");
        cs7_mutex_unlock(&x->engine_lock);
        return;
    }

    csoundSetHostData(x->csound, x);
    csoundSetMessageCallback(x->csound, cs7_msg_callback);
    csoundSetMessageLevel(x->csound, x->msg_level);

    // Apply the file's own <CsOptions> BEFORE our own forced options below
    // - see the long comment on this in the Max version's start_engine();
    // same exact reason applies here: csoundCompileCSD()'s own <CsOptions>
    // handling runs after csoundStart(), too late to take effect.
    if (x->orc_path) csound7_apply_csoptions(x->csound, x, x->orc_path->s_name);

    char opt[64];
    snprintf(opt, sizeof(opt), "--sample-rate=%d", (int)sr);
    csoundSetOption(x->csound, opt);

    if (x->ksmps_forced) {
        snprintf(opt, sizeof(opt), "--ksmps=%ld", x->ksmps);
        csoundSetOption(x->csound, opt);
    }

    snprintf(opt, sizeof(opt), "--nchnls=%ld", x->nchnls_out);
    csoundSetOption(x->csound, opt);
    snprintf(opt, sizeof(opt), "--nchnls_i=%ld", x->nchnls_in);
    csoundSetOption(x->csound, opt);

    // 0dbfs - same forced-before-start fix as the Max side, same reason:
    // scanned from the file, defaulting to 1 (the modern convention every
    // .csd here declares) if not found.
    long odbfs_val = x->orc_path ? csound7_scan_nchnls(x->orc_path->s_name, "0dbfs", 1) : 1;
    snprintf(opt, sizeof(opt), "--0dbfs=%ld", odbfs_val);
    csoundSetOption(x->csound, opt);

    csoundSetOption(x->csound, "-n");
    csoundSetOption(x->csound, "-+rtaudio=null");
    csoundSetOption(x->csound, "-+rtmidi=null");

    // MIDI via host callbacks, not a real device - same "-M0" requirement
    // as the Max side (pure Csound API behavior, not host-specific).
    csoundSetOption(x->csound, "-M0");
    csoundSetHostMIDIIO(x->csound);
    csoundSetExternalMidiInOpenCallback(x->csound, cs7_midi_in_open);
    csoundSetExternalMidiReadCallback(x->csound, cs7_midi_read);
    csoundSetExternalMidiInCloseCallback(x->csound, cs7_midi_in_close);

    csound7_load_plugins_dir(x);

    // Start BEFORE CompileCSD/CompileOrc - same ordering as the Max side,
    // same reason: "i" events treated as realtime, performance never
    // auto-stops when the score ends.
    csoundStart(x->csound);
    x->cs_started = 1;
    x->cs_sr = sr;

    if (x->orc_path) {
        const char *p = x->orc_path->s_name;
        size_t len = strlen(p);
        int is_csd = (len > 4 && strcasecmp(p + len - 4, ".csd") == 0);
        if (is_csd) {
            int32_t csd_err = csoundCompileCSD(x->csound, p, 0, 0);
            if (csd_err != 0)
                pd_error(x, "csound7~: csoundCompileCSD failed on %s (code %d)", p, csd_err);
        } else {
            FILE *f = fopen(p, "rb");
            if (f) {
                fseek(f, 0, SEEK_END);
                long sz = ftell(f);
                fseek(f, 0, SEEK_SET);
                char *buf = (char *)malloc(sz + 1);
                fread(buf, 1, sz, f);
                buf[sz] = 0;
                fclose(f);
                csoundCompileOrc(x->csound, buf, 0);
                free(buf);
            } else {
                pd_error(x, "csound7~: could not open %s", p);
            }
        }
    }

    if (x->csound) {
        uint32_t real_ksmps = csoundGetKsmps(x->csound);
        if (real_ksmps > 0) x->ksmps = (long)real_ksmps;
    }

    csound7_rb_alloc(x, x->ksmps * 4 > 256 ? x->ksmps * 4 : 256);

    cs7_mutex_unlock(&x->engine_lock);
}

static void csound7_do_reset(t_csound7 *x)
{
    double sr = x->cs_sr > 0 ? x->cs_sr : sys_getsr();
    csound7_start_engine(x, sr);
}

static void csound7_do_compile(t_csound7 *x, t_symbol *path)
{
    if (path) x->orc_path = path;
    if (!x->orc_path) {
        pd_error(x, "csound7~: no file set (use 'read' first)");
        return;
    }
    if (!x->csound) return;

    const char *p = x->orc_path->s_name;
    FILE *f = fopen(p, "rb");
    if (!f) {
        pd_error(x, "csound7~: could not open %s", p);
        return;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc(sz + 1);
    fread(buf, 1, sz, f);
    buf[sz] = 0;
    fclose(f);

    cs7_mutex_lock(&x->engine_lock);
    int32_t err = csoundCompileOrc(x->csound, buf, 0);
    cs7_mutex_unlock(&x->engine_lock);
    free(buf);

    if (err != 0)
        pd_error(x, "csound7~: compile error (%d)", err);
    else
        post("csound7~: recompiled %s", p);
}

// ---------------------------------------------------------------------
// dsp — sync sr, (re)allocate the ring buffer, register the perform
// routine with a runtime-sized argument list (nchnls_in + nchnls_out
// signal vectors) via dsp_addv.
// ---------------------------------------------------------------------
static void csound7_dsp(t_csound7 *x, t_signal **sp)
{
    if (!x->csound || sp[0]->s_sr != x->cs_sr) {
        post("csound7~: Pd sr (%.0f) differs from Csound (%.0f), automatic reset+recompile",
            sp[0]->s_sr, x->cs_sr);
        csound7_start_engine(x, sp[0]->s_sr);
    }

    int nargs = 2 + (int)x->nchnls_in + (int)x->nchnls_out;
    t_int *vec = (t_int *)malloc(sizeof(t_int) * nargs);
    int i = 0;
    vec[i++] = (t_int)x;
    vec[i++] = (t_int)sp[0]->s_n;
    // sp[0..nchnls_in-1] = input signals (inlet 0 = channel 1, then the
    // extra signal-only inlets created in csound7_new, in order).
    for (long ch = 0; ch < x->nchnls_in; ch++) vec[i++] = (t_int)sp[ch]->s_vec;
    // sp[nchnls_in..nchnls_in+nchnls_out-1] = output signals, in the order
    // the outlets were created (channel 1 first).
    for (long ch = 0; ch < x->nchnls_out; ch++)
        vec[i++] = (t_int)sp[x->nchnls_in + ch]->s_vec;

    dsp_addv(csound7_perform, nargs, vec);
    free(vec); // dsp_addv copies the vector internally, safe to free right after
}

// ---------------------------------------------------------------------
// perform — feed/drain the ring buffer, calls csoundPerformKsmps in
// blocks of x->ksmps independently of Pd's block size. Same algorithm as
// the Max version's perform64, just t_sample (float) instead of double,
// and args read from the classic t_int* "w" array instead of ins[]/outs[].
// ---------------------------------------------------------------------
static t_int *csound7_perform(t_int *w)
{
    t_csound7 *x = (t_csound7 *)w[1];
    int n = (int)w[2];
    t_int *sigptrs = w + 3; // sigptrs[0..nchnls_in-1] = inputs, then outputs

    if (!x->csound || !x->cs_started || !x->running) {
        for (long ch = 0; ch < x->nchnls_out; ch++) {
            t_sample *out = (t_sample *)sigptrs[x->nchnls_in + ch];
            memset(out, 0, sizeof(t_sample) * n);
        }
        return w + 3 + x->nchnls_in + x->nchnls_out;
    }

    if (!cs7_mutex_trylock(&x->engine_lock)) {
        // 1) write Pd's incoming audio into the input ring buffer
        for (long ch = 0; ch < x->nchnls_in; ch++) {
            t_sample *in = (t_sample *)sigptrs[ch];
            for (int i = 0; i < n; i++) {
                long w2 = (x->rb_in_write + i) % x->rb_size;
                x->rb_in[ch][w2] = (MYFLT)in[i];
            }
        }
        x->rb_in_write = (x->rb_in_write + n) % x->rb_size;
        x->rb_in_filled += n;

        // 2) run Csound k-cycles as long as we have enough input buffered
        //    and room on output
        while (x->rb_in_filled >= x->ksmps &&
               (x->rb_out_filled + x->ksmps) <= x->rb_size) {

            MYFLT *spin  = csoundGetSpin(x->csound);
            const MYFLT *spout = csoundGetSpout(x->csound);
            uint32_t cs_ksmps = csoundGetKsmps(x->csound);

            for (uint32_t f = 0; f < cs_ksmps; f++) {
                long r = (x->rb_in_read + f) % x->rb_size;
                for (long ch = 0; ch < x->nchnls_in; ch++) {
                    spin[f * x->nchnls_in + ch] = x->rb_in[ch][r];
                }
            }
            x->rb_in_read = (x->rb_in_read + cs_ksmps) % x->rb_size;
            x->rb_in_filled -= cs_ksmps;

            csoundPerformKsmps(x->csound);

            for (uint32_t f = 0; f < cs_ksmps; f++) {
                long w2 = (x->rb_out_write + f) % x->rb_size;
                for (long ch = 0; ch < x->nchnls_out; ch++) {
                    x->rb_out[ch][w2] = spout[f * x->nchnls_out + ch];
                }
            }
            x->rb_out_write = (x->rb_out_write + cs_ksmps) % x->rb_size;
            x->rb_out_filled += cs_ksmps;
        }

        // 3) drain from the output ring into Pd's audio output
        for (long ch = 0; ch < x->nchnls_out; ch++) {
            t_sample *out = (t_sample *)sigptrs[x->nchnls_in + ch];
            for (int i = 0; i < n; i++) {
                if (x->rb_out_filled > 0) {
                    long r = (x->rb_out_read + i) % x->rb_size;
                    out[i] = (t_sample)x->rb_out[ch][r];
                } else {
                    out[i] = 0;
                }
            }
        }
        if (x->rb_out_filled >= n) {
            x->rb_out_read = (x->rb_out_read + n) % x->rb_size;
            x->rb_out_filled -= n;
        }

        cs7_mutex_unlock(&x->engine_lock);
    } else {
        for (long ch = 0; ch < x->nchnls_out; ch++) {
            t_sample *out = (t_sample *)sigptrs[x->nchnls_in + ch];
            memset(out, 0, sizeof(t_sample) * n);
        }
    }

    return w + 3 + x->nchnls_in + x->nchnls_out;
}

// ---------------------------------------------------------------------
// ring buffer alloc/free
// ---------------------------------------------------------------------
static void csound7_rb_free(t_csound7 *x)
{
    if (x->rb_in) {
        for (long ch = 0; ch < x->nchnls_in; ch++) if (x->rb_in[ch]) free(x->rb_in[ch]);
        free(x->rb_in);
        x->rb_in = NULL;
    }
    if (x->rb_out) {
        for (long ch = 0; ch < x->nchnls_out; ch++) if (x->rb_out[ch]) free(x->rb_out[ch]);
        free(x->rb_out);
        x->rb_out = NULL;
    }
    x->rb_size = 0;
}

static void csound7_rb_alloc(t_csound7 *x, long size)
{
    csound7_rb_free(x);
    x->rb_size = size;
    x->rb_in  = (MYFLT **)malloc(sizeof(MYFLT *) * x->nchnls_in);
    x->rb_out = (MYFLT **)malloc(sizeof(MYFLT *) * x->nchnls_out);
    for (long ch = 0; ch < x->nchnls_in; ch++) {
        x->rb_in[ch] = (MYFLT *)calloc(size, sizeof(MYFLT));
    }
    for (long ch = 0; ch < x->nchnls_out; ch++) {
        x->rb_out[ch] = (MYFLT *)calloc(size, sizeof(MYFLT));
    }
    x->rb_in_write = x->rb_in_read = x->rb_in_filled = 0;
    x->rb_out_write = x->rb_out_read = x->rb_out_filled = 0;
}

// ---------------------------------------------------------------------
// message dispatch
// ---------------------------------------------------------------------
static void csound7_list(t_csound7 *x, t_symbol *s, int argc, t_atom *argv)
{
    (void)s; (void)argc; (void)argv;
    pd_error(x, "csound7~: plain numeric lists aren't used for anything - to set a "
        "channel, use a message like \"<channel_name> <value>\" (e.g. \"amp 0.3\")");
}

static void csound7_anything(t_csound7 *x, t_symbol *s, int argc, t_atom *argv)
{
    if (s == gensym("event")) {
        char line[1024]; line[0] = 0;
        for (int i = 0; i < argc; i++) {
            char tmp[64];
            if (argv[i].a_type == A_SYMBOL)
                snprintf(tmp, sizeof(tmp), "%s ", atom_getsymbol(argv + i)->s_name);
            else
                snprintf(tmp, sizeof(tmp), "%g ", atom_getfloat(argv + i));
            strncat(line, tmp, sizeof(line) - strlen(line) - 1);
        }
        if (x->csound) csoundEventString(x->csound, line, 1);
        return;
    }
    if (s == gensym("start")) { x->running = 1; return; }
    if (s == gensym("stop"))  { x->running = 0; return; }
    if (s == gensym("channels")) { csound7_dump_channels(x); return; }
    if (s == gensym("verbose")) {
        if (argc >= 1) {
            long v = (long)atom_getfloat(argv);
            if (v < 0) v = 0;
            if (v > 231) v = 231;
            x->msg_level = v;
            if (x->csound) csoundSetMessageLevel(x->csound, x->msg_level);
        }
        return;
    }
    if (s == gensym("ksmps")) {
        if (argc >= 1) {
            long v = (long)atom_getfloat(argv);
            if (v < 1) v = 1;
            x->ksmps = v;
            x->ksmps_forced = 1;
            if (x->csound) csound7_do_reset(x);
        }
        return;
    }
    if (s == gensym("read")) {
        if (argc >= 1 && argv[0].a_type == A_SYMBOL)
            x->orc_path = csound7_resolve_path(x, atom_getsymbol(argv));
        return;
    }
    if (s == gensym("compile")) {
        t_symbol *path = (argc >= 1 && argv[0].a_type == A_SYMBOL)
            ? csound7_resolve_path(x, atom_getsymbol(argv)) : NULL;
        csound7_do_compile(x, path);
        return;
    }
    if (s == gensym("reset")) { csound7_do_reset(x); return; }
    if (s == gensym("midi")) {
        if (argc >= 3) {
            unsigned char status = (unsigned char)atom_getfloat(argv);
            unsigned char d1 = (unsigned char)atom_getfloat(argv + 1);
            unsigned char d2 = (unsigned char)atom_getfloat(argv + 2);
            unsigned char nbytes = 3;
            unsigned char hi = status & 0xF0;
            if (hi == 0xC0 || hi == 0xD0) nbytes = 2;

            long next = (x->midi_head + nbytes) % CS7_MIDI_QUEUE_SIZE;
            if (next != x->midi_tail) {
                x->midi_queue[x->midi_head] = status;
                x->midi_head = (x->midi_head + 1) % CS7_MIDI_QUEUE_SIZE;
                x->midi_queue[x->midi_head] = d1;
                x->midi_head = (x->midi_head + 1) % CS7_MIDI_QUEUE_SIZE;
                if (nbytes == 3) {
                    x->midi_queue[x->midi_head] = d2;
                    x->midi_head = (x->midi_head + 1) % CS7_MIDI_QUEUE_SIZE;
                }
            } else {
                pd_error(x, "csound7~: MIDI queue full, event dropped");
            }
        }
        return;
    }
    if (s == gensym("buf2tab")) {
        if (argc >= 2 && argv[0].a_type == A_SYMBOL) {
            csound7_buf2tab(x, atom_getsymbol(argv), (long)atom_getfloat(argv + 1));
        }
        return;
    }
    if (s == gensym("tab2buf")) {
        if (argc >= 2 && argv[1].a_type == A_SYMBOL) {
            csound7_tab2buf(x, (long)atom_getfloat(argv), atom_getsymbol(argv + 1));
        }
        return;
    }
    if (s == gensym("diskinfile")) {
        if (argc >= 2 && argv[0].a_type == A_SYMBOL && argv[1].a_type == A_SYMBOL) {
            csound7_diskinfile(x, atom_getsymbol(argv), atom_getsymbol(argv + 1));
        } else {
            pd_error(x, "csound7~: diskinfile needs 2 args: <channel name> <path>");
        }
        return;
    }

    // generic "<channel_name> <value>" set, same fallback as the Max side
    if (argc >= 1 && argv[0].a_type == A_FLOAT) {
        double val = atom_getfloat(argv);
        if (x->csound) csoundSetControlChannel(x->csound, s->s_name, (MYFLT)val);
        return;
    }

    pd_error(x, "csound7~: unrecognized message '%s'", s->s_name);
}

static void csound7_dump_channels(t_csound7 *x)
{
    if (!x->csound) return;
    controlChannelInfo_t *lst = NULL;
    int32_t n = csoundListChannels(x->csound, &lst);
    if (n <= 0 || !lst) return;

    for (int32_t i = 0; i < n; i++) {
        t_atom a[2];
        const char *type =
            (lst[i].type & CSOUND_AUDIO_CHANNEL)  ? "audio"  :
            (lst[i].type & CSOUND_STRING_CHANNEL) ? "string" :
            (lst[i].type & CSOUND_CONTROL_CHANNEL)? "control": "?";
        SETSYMBOL(&a[0], gensym(lst[i].name));
        SETSYMBOL(&a[1], gensym(type));
        outlet_anything(x->ctl_out, gensym("channel"), 2, a);
    }
    csoundDeleteChannelList(x->csound, lst);
}

// ---------------------------------------------------------------------
// array <-> Csound table (Pd's equivalent of the Max version's
// buffer~ <-> table buf2tab/tab2buf, using g_canvas.h's garray API
// instead of ext_buffer.h). Same safety caveat as the Max side: never
// buf2tab into a table a running note is actively reading from - see
// 3_buffer_player.csd's workflow comments (identical file, unchanged).
// ---------------------------------------------------------------------
static void csound7_buf2tab(t_csound7 *x, t_symbol *arrayname, long tabnum)
{
    if (!x->csound) return;
    t_garray *a = (t_garray *)pd_findbyclass(arrayname, garray_class);
    if (!a) { pd_error(x, "csound7~: array '%s' not found", arrayname->s_name); return; }

    int npts; t_word *vec;
    if (!garray_getfloatwords(a, &npts, &vec)) {
        pd_error(x, "csound7~: '%s' has no float array data", arrayname->s_name);
        return;
    }

    MYFLT *table = (MYFLT *)malloc(sizeof(MYFLT) * (npts + 1));
    for (int i = 0; i < npts; i++) table[i] = (MYFLT)vec[i].w_float;
    table[npts] = table[npts > 0 ? npts - 1 : 0]; // guard point

    char orc[256];
    snprintf(orc, sizeof(orc), "i_cs7buf ftgen %ld, 0, %d, -2, 0\n", tabnum, npts);
    csoundCompileOrc(x->csound, orc, 0); // async=0: must be synchronous, see the
                                          // Max version's buf2tab for why (the
                                          // resize race this fixes).

    csoundTableCopyIn(x->csound, (int32_t)tabnum, table, 0);
    free(table);

    // Pd arrays don't carry a native samplerate the way a Max buffer~
    // does (an array is just raw floats, from wherever - tabread, drawn by
    // hand, soundfiler, ...), so there's no automatic buf_sr_<n> channel
    // published here. If you loaded this array from a file via Pd's
    // "soundfiler" object, soundfiler reports the file's sr back to you
    // directly - forward that yourself with a plain "buf_sr_1 <value>"
    // message (handled by the generic channel-set fallback above), same
    // channel name 3_buffer_player.csd's instrument already reads.
    post("csound7~: copied %d points from array '%s' into table %ld",
        npts, arrayname->s_name, tabnum);
}

static void csound7_tab2buf(t_csound7 *x, long tabnum, t_symbol *arrayname)
{
    if (!x->csound) return;
    int32_t len = csoundTableLength(x->csound, (int32_t)tabnum);
    if (len <= 0) { pd_error(x, "csound7~: table %ld not found/empty", tabnum); return; }

    t_garray *a = (t_garray *)pd_findbyclass(arrayname, garray_class);
    if (!a) { pd_error(x, "csound7~: array '%s' not found", arrayname->s_name); return; }

    // VERIFY: not resizing the array to fit here (unlike buf2tab, which
    // DOES resize the Csound table) - Pd's array-resize call exists
    // (garray_resize_long in recent Pd) but I'm least sure of its exact
    // name/signature of anything in this file, so to keep this safe on a
    // first pass: make the array at least `len` points long yourself
    // first (e.g. an "array size" message to the array, or set it up that
    // size when you create it), then tab2buf copies min(table length,
    // array length) points in, starting at index 0.
    int npts; t_word *vec;
    if (!garray_getfloatwords(a, &npts, &vec)) {
        pd_error(x, "csound7~: '%s' has no float array data", arrayname->s_name);
        return;
    }

    MYFLT *table = (MYFLT *)malloc(sizeof(MYFLT) * len);
    csoundTableCopyOut(x->csound, (int32_t)tabnum, table, 0); // async=0: synchronous

    int n = (npts < (int)len) ? npts : (int)len;
    if (n < npts) {
        pd_error(x, "csound7~: array '%s' (%d points) is shorter than table %ld (%d points) - "
            "resize the array first, only copying the first %d points",
            arrayname->s_name, npts, tabnum, len, n);
    }
    for (int i = 0; i < n; i++) vec[i].w_float = (float)table[i];
    free(table);

    garray_redraw(a);
    post("csound7~: copied %d points from table %ld into array '%s'", n, tabnum, arrayname->s_name);
}

// ---------------------------------------------------------------------
// "diskinfile <channel name> <path>" - sets a Csound STRING channel to a
// file path. Identical mechanism/intent to the Max version - see
// 5_diskin_player.csd and 6_wav_table_player.csd (both unchanged, pure
// Csound code, reused as-is here).
// ---------------------------------------------------------------------
static void csound7_diskinfile(t_csound7 *x, t_symbol *chan, t_symbol *path)
{
    if (!x->csound) return;
    // No csound7_conform_path() equivalent needed here - Pd's own file
    // dialogs (e.g. "openpanel") already return clean, plain POSIX paths,
    // unlike Max's opendialog on this system (see the Max version's long
    // comment on that).
    csoundSetStringChannel(x->csound, chan->s_name, path->s_name);
    post("csound7~: diskinfile: channel '%s' = '%s'", chan->s_name, path->s_name);
}

// ---------------------------------------------------------------------
// dynamic plugins: load everything found in <external's own folder>/plugins/
// - same idea and same call (csoundLoadPlugins()) as the Max version's
// csound7_load_plugins_dir(), just a different way of finding "the
// external's own folder" since Pd has no bundle path API to call.
//
// Confirmed (not guessed) by reading Pd's own source directly
// (m_class.c, class_donew() and class_gethelpdir()): every class's
// c_externdir field is set from the file-scope class_extern_dir symbol
// at the moment class_new() runs; Pd's loader (s_loader.c's
// sys_load_lib()) sets class_extern_dir to the directory of the .pd_darwin/
// .pd_linux/.dll file it's about to load, right before calling this
// file's own csound7_tilde_setup() - so class_gethelpdir(csound7_class),
// called any time after class_new() (i.e. any time after setup, which
// covers every call here, all of which happen from csound7_new() onward),
// reliably returns this external's own folder. This is the same
// documented mechanism Pd uses to locate an object's help patch
// (hence the "helpdir" name) - it isn't a plugin-loading API by name.
// This external never calls class_sethelpsymbol(), so that value is
// never overridden away from the loader's own default.
// ---------------------------------------------------------------------
static void csound7_load_plugins_dir(t_csound7 *x)
{
    if (!x->csound) return;

    const char *ext_dir = class_gethelpdir(csound7_class);
    if (!ext_dir || !ext_dir[0]) {
        post("csound7~: could not determine the external's own folder, skipping plugins/ scan");
        return;
    }

    char plugins_path[CS7_MAX_PATH];
    snprintf(plugins_path, sizeof(plugins_path), "%s/plugins", ext_dir);

    // csoundLoadPlugins() is happy to be pointed at a folder that doesn't
    // exist or is empty - it just finds nothing to load, same behavior
    // relied on by the Max version. No separate existence check needed.
    csoundLoadPlugins(x->csound, plugins_path);
}

// ---------------------------------------------------------------------
// MIDI host IO callbacks - byte-identical to the Max version, pure
// Csound API, no host-specific code at all.
// ---------------------------------------------------------------------
static int32_t cs7_midi_in_open(CSOUND *csound, void **userData, const char *devName)
{
    (void)csound; (void)devName;
    *userData = NULL;
    return 0;
}

static int32_t cs7_midi_in_close(CSOUND *csound, void *userData)
{
    (void)csound; (void)userData;
    return 0;
}

static int32_t cs7_midi_read(CSOUND *csound, void *userData, unsigned char *buf, int32_t nBytes)
{
    (void)userData;
    t_csound7 *x = (t_csound7 *)csoundGetHostData(csound);
    if (!x) return 0;

    int32_t n = 0;
    while (n < nBytes && x->midi_tail != x->midi_head) {
        buf[n++] = x->midi_queue[x->midi_tail];
        x->midi_tail = (x->midi_tail + 1) % CS7_MIDI_QUEUE_SIZE;
    }
    return n;
}

// ---------------------------------------------------------------------
// Csound messages -> Pd console. Same reasoning as the Max version's
// cs7_msg_callback: Csound tags a lot of purely informational text with
// CSOUNDMSG_ERROR, so the message TYPE isn't a reliable "needs your
// attention" signal - everything here goes through post() (not the red
// pd_error path), our own explicit return-code checks elsewhere already
// handle real failures.
// ---------------------------------------------------------------------
static void cs7_msg_callback(CSOUND *csound, int32_t attr, const char *fmt, va_list args)
{
    (void)csound;
    char line[1024];
    vsnprintf(line, sizeof(line), fmt, args);

    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
        line[--len] = 0;
    }
    if (len == 0) return;

    int32_t type = attr & CSOUNDMSG_TYPE_MASK;
    if (type == CSOUNDMSG_WARNING)
        post("csound warning: %s", line);
    else
        post("csound: %s", line);
}

// ---------------------------------------------------------------------
// resolve a possibly-relative filename. Two strategies, same order as the
// Max version:
//   1) relative to the patch's own folder (canvas_getdir(), captured at
//      "new" time - see csound7_new()).
//   2) Pd's own search path, via open_via_path() - Pd's standard,
//      documented function for exactly this (patch folder + declared
//      search paths), the direct equivalent of Max's locatefile_extended.
// ---------------------------------------------------------------------
static t_symbol *csound7_resolve_path(t_csound7 *x, t_symbol *filename)
{
    if (!filename) return filename;
    const char *name = filename->s_name;
    if (name[0] == '/' || name[0] == '~') return filename; // already absolute

    if (x->owner_dir && x->owner_dir->s_name[0]) {
        char full[CS7_MAX_PATH];
        snprintf(full, sizeof(full), "%s/%s", x->owner_dir->s_name, name);
        FILE *test = fopen(full, "rb");
        if (test) {
            fclose(test);
            post("csound7~: resolved '%s' -> '%s' (next to patch)", name, full);
            return gensym(full);
        }
        post("csound7~: tried '%s' (next to patch), not found there", full);
    } else {
        post("csound7~: patch has no known folder yet (unsaved patch?), skipping patch-relative lookup");
    }

    char dirresult[CS7_MAX_PATH];
    char *nameresult = NULL;
    int fd = open_via_path(x->owner_dir ? x->owner_dir->s_name : "", name, "", dirresult,
        &nameresult, CS7_MAX_PATH, 1);
    if (fd >= 0) {
        cs7_close(fd);
        char full[CS7_MAX_PATH];
        snprintf(full, sizeof(full), "%s/%s", dirresult, nameresult ? nameresult : name);
        post("csound7~: resolved '%s' -> '%s' (Pd search path)", name, full);
        return gensym(full);
    }

    pd_error(x, "csound7~: could not locate '%s' next to the patch or via Pd's search "
        "path — using the name as given, this will likely fail to open", name);
    return filename;
}

// ---------------------------------------------------------------------
// apply <CsOptions> before csoundStart() - byte-identical logic/reasoning
// to the Max version's csound7_apply_csoptions(), just posting through
// Pd's post() instead of object_post().
// ---------------------------------------------------------------------
static void csound7_apply_csoptions(CSOUND *csound, t_csound7 *x, const char *path)
{
    size_t plen = strlen(path);
    if (plen < 4 || strcasecmp(path + plen - 4, ".csd") != 0) return;

    FILE *f = fopen(path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 1 << 20) { fclose(f); return; }
    char *buf = (char *)malloc(sz + 1);
    fread(buf, 1, sz, f);
    buf[sz] = 0;
    fclose(f);

    char *start = strstr(buf, "<CsOptions>");
    char *end   = start ? strstr(start, "</CsOptions>") : NULL;
    if (start && end) {
        start += strlen("<CsOptions>");
        *end = 0;

        char *saveptr_line = NULL;
        char *line = strtok_r(start, "\r\n", &saveptr_line);
        while (line) {
            char *comment = strchr(line, ';');
            if (comment) *comment = 0;

            char *saveptr_tok = NULL;
            char *tok = strtok_r(line, " \t", &saveptr_tok);
            while (tok) {
                csoundSetOption(csound, tok);
                post("csound7~: CsOptions: applied '%s' from %s", tok, path);
                tok = strtok_r(NULL, " \t", &saveptr_tok);
            }
            line = strtok_r(NULL, "\r\n", &saveptr_line);
        }
    }
    free(buf);
    (void)x;
}

// ---------------------------------------------------------------------
// scan a .csd/.orc's header text for "<token> = <number>" - byte-
// identical to the Max version's csound7_scan_nchnls() (pure C string
// parsing, zero host dependency).
// ---------------------------------------------------------------------
static long csound7_scan_nchnls(const char *path, const char *token, long fallback)
{
    FILE *f = fopen(path, "rb");
    if (!f) return fallback;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 1 << 20) { fclose(f); return fallback; }
    char *buf = (char *)malloc(sz + 1);
    fread(buf, 1, sz, f);
    buf[sz] = 0;
    fclose(f);

    long result = fallback;
    char *p = buf;
    size_t tlen = strlen(token);
    while ((p = strstr(p, token)) != NULL) {
        char *after = p + tlen;
        if (*after == '_' || isalnum((unsigned char)*after)) { p = after; continue; }
        while (*after == ' ' || *after == '\t') after++;
        if (*after == '=') {
            after++;
            result = strtol(after, NULL, 10);
            break;
        }
        p = after;
    }
    free(buf);
    return result;
}

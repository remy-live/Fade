/* voice.lv2 — vocal channel strip and effects rack for the MOD Dwarf.
 *
 * Why it exists: a VoiceLive does two jobs at once. It follows the PITCH of
 * the voice to build harmonies, and around that it runs a channel strip and
 * an effects rack. The pitch half is the half that fails on stage: it wants
 * a guide chord or a key, it smears on consonants, and it goes wrong most
 * where the stage is loudest. This plugin is the other half, on purpose.
 * There is no pitch detection anywhere in it — nothing to track, so nothing
 * to mistrack. Every block below works on LEVEL and TIME alone, which is
 * also why none of it needs to know what note is being sung.
 *
 * The chain, in order:
 *
 *   IN GAIN -> LOW CUT -> GATE -> COMP -> DE-ESS -> BODY/PRESENCE/AIR
 *           -> DRIVE -+-> (dry)  ------------------------------+-> OUT
 *                     +-> DOUBLE -----------------------------+
 *                     +-> MOD --------------------------------+
 *                     +-> DELAY --+--------------------------+
 *                                 +-> REVERB ----------------+
 *
 * The four blocks on the right of the split are the "FX". One switch feeds
 * them or stops feeding them, and it stops the SEND, not the return: switch
 * off and the delay and the reverb ring out instead of being chopped. That
 * is the same idea the Fade plugin in this repository exists for.
 *
 * MOD Dwarf constraints honoured here, same list as fade.c:
 *   - libc only, no libm calls, no allocation inside run()
 *   - every buffer this plugin reads or writes is allocated once, in
 *     instantiate(), out of ONE block, so cleanup() cannot leak a piece
 *   - no unbounded loop, no division by a value that can be zero
 *   - no buffer written without checking it points somewhere
 *   - the HMI struct comes FROM lv2-hmi.h, never retyped from memory
 *   - caps checked before every screen write
 *   - screen rate capped, caches forgotten once per second
 *   - indicator sent AS SOON AS the control is addressed, even at zero
 *
 * The two variants are mono (1 in, 1 out) and stereo (2 in, 2 out). They
 * are not a convenience: mod-ui's fill_iotype() only recognises exactly
 * 1-in/1-out or at least 2-in/2-out, and anything else lands in the same
 * drag-and-drop bucket as CV plugins. The stereo build is also where the
 * doubler and the modulation get their width, since both place their taps
 * left and right.
 */

#if defined(__has_include)
#  if __has_include(<lv2/core/lv2.h>)
#    include <lv2/core/lv2.h>
#  else
#    include <lv2.h>
#  endif
#else
#  include <lv2.h>
#endif

#include <stdlib.h>
#include <string.h>   /* also pulls in stddef for size_t, which lv2-hmi.h needs */

#include "lv2-hmi.h"

#define VOICE_URI        "http://remy-live.github.io/lv2/voice"
#define VOICE_STEREO_URI "http://remy-live.github.io/lv2/voice#stereo"

#define MAX_CH 2

/* Build stamp, readable on both ends with:
     grep -ao 'VOICE_BUILD[A-Za-z0-9_]*' voice.so
   The ARCHITECTURE is part of it on purpose: a stamp that does not name
   the architecture once let a 32-bit binary pass a check meant to catch
   exactly that. */
__attribute__((used))
static const volatile char build_tag[] = "VOICE_BUILD1_AARCH64_20260904";

/* ------------------------------------------------------------------ */
/* Maths without libm.                                                 */
/*                                                                     */
/* The binary must depend on libc alone: the build refuses an ELF with  */
/* libm in NEEDED. Everything below is therefore a polynomial or a bit  */
/* trick, and the test bench measures each one against real libm.       */
/* ------------------------------------------------------------------ */

/* log2 of a positive float. The exponent comes out of the float's own
   bits, the mantissa through a degree-5 least-squares fit on [1,2).
   Measured error: 3.2e-5 in log2, i.e. 0.0002 dB. */
static float log2_approx(float x)
{
    uint32_t bits;
    float    m;

    if (!(x > 1.0e-30f)) {   /* also catches NaN and every negative */
        return -100.0f;      /* -602 dB: silence, and nothing divides by it */
    }

    memcpy(&bits, &x, sizeof(bits));
    const int e = (int)((bits >> 23) & 0xFFu) - 127;
    bits = (bits & 0x807FFFFFu) | 0x3F800000u;   /* mantissa into [1,2) */
    memcpy(&m, &bits, sizeof(m));

    const float p = -2.786805564f + m * (5.046852936f + m * (-3.492466043f
                  + m * (1.593884548f + m * (-0.404862309f + m * 0.043428363f))));
    return (float)e + p;
}

/* 2^x. Integer part goes straight into the exponent field, fractional
   part through a degree-5 fit on [0,1). Measured error: 2.3e-7 relative,
   i.e. 0.000002 dB. */
static float exp2_approx(float x)
{
    uint32_t bits;
    float    s;

    if (!(x > -60.0f)) {     /* also NaN. 2^-60 is far below any audio */
        return 0.0f;
    }
    if (x > 60.0f) {
        x = 60.0f;
    }

    int i = (int)x;
    if (x < 0.0f && (float)i != x) {
        --i;                 /* (int) truncates towards zero, we want floor */
    }
    const float f = x - (float)i;
    const float p = 0.999999769f + f * (0.693156779f + f * (0.240131684f
                  + f * (0.055876569f + f * (0.008940578f + f * 0.001894379f))));

    bits = (uint32_t)((i + 127) << 23);
    memcpy(&s, &bits, sizeof(s));
    return p * s;
}

#define DB_PER_OCTAVE 6.020599913f      /* 20*log10(2) */

static float lin_to_db(float x)
{
    return DB_PER_OCTAVE * log2_approx(x);
}

static float db_to_lin(float db)
{
    if (!(db > -90.0f)) {    /* also NaN: below -90 dB nothing is audible */
        return 0.0f;
    }
    if (db > 40.0f) {
        db = 40.0f;
    }
    return exp2_approx(db * (1.0f / DB_PER_OCTAVE));
}

static float absf(float x)
{
    return x < 0.0f ? -x : x;
}

/* Denormals cost tens of cycles each on this CPU and only ever appear in
   the tails of feedback loops, where they are inaudible anyway. Flushing
   them costs one comparison. */
static float flush(float x)
{
    return (x > -1.0e-20f && x < 1.0e-20f) ? 0.0f : x;
}

/* Sine for the LFOs, phase in [0,1). Parabola plus one refinement pass:
   0.06 % peak error, which is a thousand times better than an LFO can be
   heard to need, for three multiplies. */
static float lfo_sin(float phase)
{
    float p = phase - (float)(int)phase;
    if (p < 0.0f) { p += 1.0f; }
    if (p > 0.5f) { p -= 1.0f; }          /* -0.5 .. 0.5 */
    const float a = absf(p);
    const float y = 8.0f * p - 16.0f * p * a;
    return 0.775f * y + 0.225f * y * absf(y);
}

/* One-pole lowpass coefficient for a cutoff in Hz.
   g = w/(1+w) with w = 2*pi*f/fs is the bilinear one-pole with the
   prewarp tangent approximated by its argument. It is monotonic in f and
   always strictly inside (0,1), so the filter cannot be made unstable by
   any value a control port can hold — which matters more here than the
   half-decibel of cutoff error near Nyquist. */
static float onepole_coef(float hz, float rate)
{
    if (!(hz > 0.0f)) {      /* also NaN */
        return 0.0f;
    }
    if (hz > rate * 0.45f) {
        hz = rate * 0.45f;
    }
    const float w = 6.2831853f * hz / rate;
    return w / (1.0f + w);
}

/* Coefficient of an envelope follower whose time constant is ms.
   exp(-1/(t*fs)) is approximated by 1/(1+1/(t*fs)), which is exact in the
   limit and always lands in (0,1]. */
static float env_coef(float ms, float rate)
{
    if (!(ms > 0.0f)) {      /* also NaN: an instant follower */
        return 1.0f;
    }
    const float n = ms * 0.001f * rate;
    return 1.0f / (n + 1.0f);
}

/* Soft saturation: the Pade approximation of tanh, clamped where it stops
   being one. Above |x| = 3 the rational form turns back around and grows
   again, so it is pinned to +/-1 there; the two halves meet exactly at 1,
   with the same slope, so nothing clicks at the join. */
static float softclip(float x)
{
    if (x > 3.0f)  { return 1.0f; }
    if (x < -3.0f) { return -1.0f; }
    const float x2 = x * x;
    const float y  = x * (27.0f + x2) / (27.0f + 9.0f * x2);
    /* Just below the join the rounding of the division overshoots by one
       ULP - measured at 1.000000119. Nothing downstream would notice, but
       a saturation that can hand out more than full scale is not a
       saturation, and the clamp is two comparisons. */
    if (y > 1.0f)  { return 1.0f; }
    if (y < -1.0f) { return -1.0f; }
    return y;
}

/* The drive control sets how hard the signal hits the saturation, and how
   much of the level that costs is given back. Both live here so activate()
   and run() cannot compute them differently.

   The gain given back is measured AT A REFERENCE LEVEL rather than
   guessed at: -12 dBFS, which is about where a voice sits after the
   compressor. A signal at that level comes out at that level whatever the
   drive is set to, so the control changes the colour without changing how
   loud the singer is. Anything above the reference is held down - that is
   what saturation is for - and the OUTPUT control makes up the difference
   if a whole take wants it. */
#define DRIVE_REF 0.25f

static float drive_pre_of(float amount)
{
    return 1.0f + amount * 0.19f;
}

static float drive_post_of(float pre)
{
    const float y = softclip(pre * DRIVE_REF);
    return (y > 1.0e-6f) ? DRIVE_REF / y : 1.0f;
}

/* Output ceiling. Transparent below 0.85, and above it the excess is bent
   towards an asymptote at 1.0, so the sum of four wet effects and a drive
   stage cannot hand the converter a sample outside [-1,1]. Slope is 1 at
   the join: it is a ceiling, not a compressor, and does nothing at all to
   a signal that was already inside it. */
static float ceiling(float x)
{
    const float t = 0.85f;
    float a = absf(x);
    if (a <= t) {
        return x;
    }
    const float u = (a - t) / (1.0f - t);
    a = t + (1.0f - t) * (u / (1.0f + u));
    return x < 0.0f ? -a : a;
}

/* ------------------------------------------------------------------ */
/* Ports                                                               */
/*                                                                     */
/* Audio ports come first — 2 for the mono variant, 4 for the stereo   */
/* one — then the controls, in the same order for both. The absolute   */
/* index of a control is n_audio + one of these.                       */
/* ------------------------------------------------------------------ */

typedef enum {
    CTL_IN_GAIN     = 0,
    CTL_LOW_CUT     = 1,
    CTL_GATE        = 2,
    CTL_COMP        = 3,
    CTL_DE_ESS      = 4,
    CTL_BODY        = 5,
    CTL_PRESENCE    = 6,
    CTL_AIR         = 7,
    CTL_DRIVE       = 8,
    CTL_DOUBLER     = 9,
    CTL_MOD         = 10,
    CTL_MOD_SPEED   = 11,
    CTL_DELAY_TIME  = 12,
    CTL_DELAY_REPEATS    = 13,
    CTL_DELAY_MIX   = 14,
    CTL_REVERB      = 15,
    CTL_REVERB_MIX  = 16,
    CTL_FX          = 17,   /* toggle, for a footswitch */
    CTL_FX_TRIGGER  = 18,   /* trigger, for MIDI: same state as the toggle */
    CTL_TAP         = 19,   /* trigger: two taps set the delay time */
    CTL_OUTPUT      = 20,
    CTL_GR          = 21,   /* output: compressor gain reduction, dB */
    CTL_LEVEL       = 22,   /* output: peak out level, 0..1 */
    CTL_GATE_OPEN   = 23,   /* output: 1 while the gate is open */
    CTL_FX_STATE    = 24,   /* output: the FX state actually in force */
    CTL_TIME_OUT    = 25,   /* output: delay time in force, tap included */
    CTL_COUNT       = 26
} ControlIndex;

/* Widest port count of the two variants: 4 audio + the controls. */
#define PORT_COUNT (4 + CTL_COUNT)

/* Range and default of every control, in ONE place.
 *
 * run() clamps each input through this table, so a host that sends
 * nonsense — or a NaN — cannot reach the DSP with it. The same table is
 * what an unconnected port reads, which matters more than it sounds: with
 * the single shared zero cell fade.c uses, an unconnected GATE port would
 * read 0 dB and gate the whole performance into silence.
 *
 * check_descriptor.py reads this table out of the source and compares it,
 * line by line, with the .ttl files. The two cannot drift apart. */
typedef struct {
    const char* symbol;
    float       min;
    float       max;
    float       def;
} CtlSpec;

static const CtlSpec ctl_spec[CTL_COUNT] = {
    /* symbol           min      max      default */
    { "in_gain",      -20.0f,   40.0f,     0.0f },
    { "low_cut",        0.0f,  400.0f,    90.0f },
    { "gate",         -80.0f,  -20.0f,   -80.0f },
    { "comp",           0.0f,  100.0f,    30.0f },
    { "de_ess",         0.0f,  100.0f,     0.0f },
    { "body",         -12.0f,   12.0f,     0.0f },
    { "presence",     -12.0f,   12.0f,     0.0f },
    { "air",          -12.0f,   12.0f,     0.0f },
    { "drive",          0.0f,  100.0f,     0.0f },
    { "doubler",        0.0f,  100.0f,     0.0f },
    { "modulation",     0.0f,  100.0f,     0.0f },
    { "mod_speed",      0.05f,   8.0f,     0.6f },
    { "delay_time",    20.0f, 2000.0f,   400.0f },
    { "delay_repeats",  0.0f,   95.0f,    30.0f },
    { "delay_mix",      0.0f,  100.0f,     0.0f },
    { "reverb",         0.0f,  100.0f,    40.0f },
    { "reverb_mix",     0.0f,  100.0f,     0.0f },
    { "fx",             0.0f,    1.0f,     1.0f },
    { "fx_trigger",     0.0f,    1.0f,     0.0f },
    { "tap",            0.0f,    1.0f,     0.0f },
    { "output",       -60.0f,   12.0f,     0.0f },
    { "gr",           -24.0f,    0.0f,     0.0f },
    { "level",          0.0f,    1.0f,     0.0f },
    { "gate_open",      0.0f,    1.0f,     0.0f },
    { "fx_state",       0.0f,    1.0f,     1.0f },
    { "time_out",      20.0f, 2000.0f,   400.0f },
};

/* ------------------------------------------------------------------ */
/* Blocks                                                              */
/* ------------------------------------------------------------------ */

/* A delay line. w is where the NEXT sample goes, so a delay of d samples
   reads w-d, and d = 1 is the sample just written. */
typedef struct {
    float*   buf;
    uint32_t len;
    uint32_t w;
} Ring;

static void ring_write(Ring* r, float x)
{
    r->buf[r->w] = x;
    if (++r->w >= r->len) { r->w = 0u; }
}

/* Fractional read, linearly interpolated. The delay is clamped inside the
   buffer before anything is indexed, so no control value and no modulation
   depth can walk off the end of it. */
static float ring_read(const Ring* r, float d)
{
    const float dmax = (float)(r->len - 2u);
    if (!(d >= 1.0f)) { d = 1.0f; }      /* also NaN */
    if (d > dmax)     { d = dmax; }

    const uint32_t di = (uint32_t)d;
    const float    f  = d - (float)di;

    uint32_t i0 = r->w + r->len - di;
    while (i0 >= r->len) { i0 -= r->len; }
    uint32_t i1 = (i0 == 0u) ? r->len - 1u : i0 - 1u;

    return r->buf[i0] + (r->buf[i1] - r->buf[i0]) * f;
}

/* Freeverb's comb: a delay line with a one-pole lowpass in its feedback,
   which is what makes the tail lose its top before it loses its level. */
typedef struct {
    float*   buf;
    uint32_t len;
    uint32_t p;
    float    store;
} Comb;

static float comb_run(Comb* c, float x, float fb, float damp1, float damp2)
{
    const float y = c->buf[c->p];
    c->store = flush(y * damp2 + c->store * damp1);
    c->buf[c->p] = flush(x + c->store * fb);
    if (++c->p >= c->len) { c->p = 0u; }
    return y;
}

/* Freeverb's allpass: scatters what the combs produced, so the tail stops
   sounding like eight separate echoes. */
typedef struct {
    float*   buf;
    uint32_t len;
    uint32_t p;
} Allpass;

static float allpass_run(Allpass* a, float x)
{
    const float y   = a->buf[a->p];
    const float out = y - x;
    a->buf[a->p] = flush(x + y * 0.5f);
    if (++a->p >= a->len) { a->p = 0u; }
    return out;
}

/* Reverb sizes, in samples at 44.1 kHz, scaled to the real rate at
   instantiate. They are the Freeverb figures: mutually prime lengths, so
   the echoes do not line up into a ringing note. */
#define N_COMB    8
#define N_ALLPASS 4
static const uint16_t comb_base[N_COMB]    = { 1116, 1188, 1277, 1356,
                                               1422, 1491, 1557, 1617 };
static const uint16_t allpass_base[N_ALLPASS] = { 556, 441, 341, 225 };
/* The right channel's lines are 23 samples longer than the left's. That
   is the whole of Freeverb's stereo image, and it costs nothing. */
#define REV_SPREAD 23

/* Everything the two channels do not share. */
typedef struct {
    /* channel strip filter states */
    float   lc_z;            /* low cut */
    float   de_z1, de_z2;    /* de-esser band split, two poles */
    float   eq_low, eq_mid_hi, eq_mid_lo, eq_air;
    float   dc_x, dc_y;      /* DC blocker after the drive stage */

    /* effects */
    Ring    shortline;       /* doubler and modulation taps */
    Ring    delay;
    float   dly_lp, dly_hp;  /* tone shaping inside the feedback path */
    Comb    comb[N_COMB];
    Allpass allpass[N_ALLPASS];
} Chan;

/* Smoothed values. Anything a hand can turn is walked to its new value
   across one block instead of jumping to it, because a gain step is a
   click. They are indexed rather than named one by one so the walk is a
   single loop rather than a dozen lines that have to stay in step. */
typedef enum {
    SM_IN = 0, SM_OUT, SM_BODY, SM_PRESENCE, SM_AIR,
    SM_DRIVE_PRE, SM_DRIVE_POST, SM_DRIVE_MIX, SM_DOUBLER, SM_MOD, SM_DELAY, SM_REVERB,
    SM_COUNT
} SmoothIndex;

/* Screen slots: the controls this plugin has something to SAY about when
   they are addressed to a knob or a footswitch. Each keeps its own cache;
   sharing one between the FX toggle and the FX trigger would mean the
   second write is skipped because the first already matched. */
typedef enum {
    SLOT_FX = 0, SLOT_FX_TRIGGER, SLOT_TAP, SLOT_DELAY,
    SLOT_COMP, SLOT_GATE, SLOT_OUT, SLOT_COUNT
} ScreenSlot;

static const uint8_t slot_ctl[SLOT_COUNT] = {
    CTL_FX, CTL_FX_TRIGGER, CTL_TAP, CTL_DELAY_TIME,
    CTL_COMP, CTL_GATE, CTL_OUTPUT
};

/* Everything from CTL_GR on is an output port. */
#define CTL_FIRST_OUTPUT CTL_GR
#define is_output_ctl(i) ((i) >= (int)CTL_FIRST_OUTPUT)

/* Screen rate: 25 passes per second for the WHOLE screen. One send per
   audio block would be closer to 400. */
#define SCREEN_HZ 25
/* The firmware repaints the screen when the page changes, so a plugin
   that only sends what changed leaves stale text behind. Once a second,
   we forget what we sent and send it again. */
#define FORGET_HZ  1

#define SHORT_MS      140.0f    /* doubler and modulation taps live here */
#define DELAY_MAX_MS 2000.0f
#define FX_RAMP_MS     40.0f    /* switching the FX send in and out */

typedef struct {
    /* --- ports --- */
    uint32_t     n_ch;        /* 1 for mono, 2 for stereo */
    uint32_t     n_audio;     /* 2 or 4: where the control ports start */
    const float* in[MAX_CH];
    float*       out[MAX_CH];
    const float* ctl[CTL_COUNT];
    float*       ctl_out[CTL_COUNT];
    float        neutral[CTL_COUNT];   /* what an unconnected port reads */

    float  rate;
    float* pool;              /* ONE allocation holding every buffer */
    Chan   ch[MAX_CH];

    /* --- smoothed controls --- */
    float sm[SM_COUNT];

    /* --- dynamics --- */
    float    gate_env;
    float    gate_gain;
    int      gate_is_open;
    uint32_t gate_hold;
    float    comp_env;
    float    de_env;
    float    gr_db;           /* worst reduction seen in the last block */
    float    meter;           /* output peak, fast up, slow down */

    /* --- FX switch: one state, two ways in, exactly as in fade.c --- */
    int   fx_state;
    int   fx_toggle_prev;
    int   fx_trigger_prev;
    float fx_gain;            /* ramped, so the send does not click */

    /* --- tap tempo --- */
    int      tap_prev;
    uint32_t tap_count;       /* samples since the last tap */
    int      tap_active;      /* the tap owns the time until the knob moves */
    float    tap_ms;
    float    knob_ms_prev;
    float    delay_ms;        /* the time in force, glided towards its target */

    /* --- LFOs --- */
    float ph_double_a, ph_double_b, ph_mod;

    /* --- screen --- */
    const LV2_HMI_WidgetControl* hmi;
    LV2_HMI_Addressing           addr[PORT_COUNT];
    uint32_t                     caps[PORT_COUNT];

    uint32_t screen_left, screen_period;
    uint32_t forget_left, forget_period;

    char cache_label[SLOT_COUNT][12];
    char cache_value[SLOT_COUNT][12];
    char cache_unit[SLOT_COUNT][8];
    int  cache_bar[SLOT_COUNT];      /* hundredths, -1 = never sent */
    int  cache_led[SLOT_COUNT];      /* -1 = never sent */
    int  cache_blink[SLOT_COUNT];    /* blink period in ms, -1 = never sent */
} Voice;

/* ------------------------------------------------------------------ */
/* Text, without the printf family                                     */
/* ------------------------------------------------------------------ */

static void write_int(char* buf, size_t size, int v)
{
    char   tmp[12];
    size_t n = 0, i = 0;

    if (size == 0) {
        return;
    }
    if (v < 0) {
        if (size > 1) { buf[i++] = '-'; }
        v = -v;
    }
    do {
        tmp[n++] = (char)('0' + (v % 10));
        v /= 10;
    } while (v > 0 && n < sizeof(tmp));

    while (n > 0 && i + 1 < size) {
        buf[i++] = tmp[--n];
    }
    buf[i] = '\0';
}

/* Bounded copy, always terminated. Measured first, then copied: the
   single-loop form is correct but gcc at -O3 cannot see that the
   end-of-string test bounds it, and reports a read past the end of short
   literals like "ON". */
static void copy_bounded(char* dst, size_t size, const char* src)
{
    size_t n = 0;
    if (size == 0) {
        return;
    }
    while (src[n] != '\0') {
        ++n;
    }
    if (n > size - 1) {
        n = size - 1;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* ------------------------------------------------------------------ */
/* Life cycle                                                          */
/* ------------------------------------------------------------------ */

/* Rounded up so a comb is never shorter than its own read, whatever the
   sample rate. */
static uint32_t scaled_len(uint32_t base, float rate)
{
    uint32_t n = (uint32_t)((float)base * rate * (1.0f / 44100.0f) + 0.5f);
    if (n < 8u) { n = 8u; }
    return n;
}

static void
forget_caches(Voice* self)
{
    for (int s = 0; s < (int)SLOT_COUNT; ++s) {
        self->cache_label[s][0] = '\0';
        self->cache_value[s][0] = '\0';
        self->cache_unit[s][0]  = '\0';
        self->cache_bar[s]      = -1;
        self->cache_led[s]      = -1;
        self->cache_blink[s]    = -1;
    }
}

static LV2_Handle
instantiate(const LV2_Descriptor*     descriptor,
            double                    rate,
            const char*               bundle_path,
            const LV2_Feature* const* features)
{
    (void)bundle_path;

    Voice* self = (Voice*)calloc(1, sizeof(Voice));
    if (!self) {
        return NULL;
    }

    self->n_ch    = (descriptor && !strcmp(descriptor->URI, VOICE_STEREO_URI)) ? 2u : 1u;
    self->n_audio = self->n_ch * 2u;
    self->rate    = (rate > 0.0) ? (float)rate : 48000.0f;

    /* Each control reads its OWN default until the host connects it. One
       shared zero cell would leave the gate threshold at 0 dB, which
       gates everything, and the low cut at 0 Hz, which is at least
       harmless — the difference is why this is a table. */
    for (int i = 0; i < (int)CTL_COUNT; ++i) {
        self->neutral[i] = ctl_spec[i].def;
        self->ctl[i]     = &self->neutral[i];
        self->ctl_out[i] = NULL;
    }
    for (uint32_t c = 0; c < MAX_CH; ++c) {
        self->in[c]  = NULL;
        self->out[c] = NULL;
    }

    /* --- one allocation for every buffer --- */
    const uint32_t n_short = (uint32_t)(self->rate * (SHORT_MS * 0.001f)) + 8u;
    const uint32_t n_delay = (uint32_t)(self->rate * (DELAY_MAX_MS * 0.001f)) + 8u;

    size_t total = 0;
    for (uint32_t c = 0; c < self->n_ch; ++c) {
        total += n_short + n_delay;
        for (int i = 0; i < N_COMB; ++i) {
            total += scaled_len(comb_base[i], self->rate) + (c ? REV_SPREAD : 0u);
        }
        for (int i = 0; i < N_ALLPASS; ++i) {
            total += scaled_len(allpass_base[i], self->rate) + (c ? REV_SPREAD : 0u);
        }
    }

    self->pool = (float*)calloc(total, sizeof(float));
    if (!self->pool) {
        free(self);
        return NULL;
    }

    float* p = self->pool;
    for (uint32_t c = 0; c < self->n_ch; ++c) {
        Chan* ch = &self->ch[c];

        ch->shortline.buf = p; ch->shortline.len = n_short; p += n_short;
        ch->delay.buf     = p; ch->delay.len     = n_delay; p += n_delay;

        for (int i = 0; i < N_COMB; ++i) {
            const uint32_t n = scaled_len(comb_base[i], self->rate) + (c ? REV_SPREAD : 0u);
            ch->comb[i].buf = p; ch->comb[i].len = n; p += n;
        }
        for (int i = 0; i < N_ALLPASS; ++i) {
            const uint32_t n = scaled_len(allpass_base[i], self->rate) + (c ? REV_SPREAD : 0u);
            ch->allpass[i].buf = p; ch->allpass[i].len = n; p += n;
        }
    }

    self->screen_period = (uint32_t)(self->rate / SCREEN_HZ);
    if (self->screen_period < 1u) { self->screen_period = 1u; }
    self->forget_period = (uint32_t)(self->rate / FORGET_HZ);
    if (self->forget_period < 1u) { self->forget_period = 1u; }

    /* Screen feature, optional: without it everything else still works. */
    self->hmi = NULL;
    if (features) {
        for (int i = 0; features[i]; ++i) {
            if (!strcmp(features[i]->URI, LV2_HMI__WidgetControl)) {
                self->hmi = (const LV2_HMI_WidgetControl*)features[i]->data;
            }
        }
    }

    return (LV2_Handle)self;
}

static void
connect_port(LV2_Handle instance, uint32_t port, void* data)
{
    Voice* self = (Voice*)instance;
    if (!self) {
        return;
    }

    if (port < self->n_audio) {
        if (port < self->n_ch) {
            self->in[port] = (const float*)data;
        } else {
            self->out[port - self->n_ch] = (float*)data;
        }
        return;
    }

    const uint32_t i = port - self->n_audio;
    if (i >= (uint32_t)CTL_COUNT) {
        return;
    }
    if (is_output_ctl((int)i)) {
        self->ctl_out[i] = (float*)data;
    } else {
        self->ctl[i] = data ? (const float*)data : &self->neutral[i];
    }
}

/* Every control input reaches the DSP through here: clamped to its own
   declared range, NaN included. */
static float ctl_read(const Voice* self, int i)
{
    float v = *self->ctl[i];
    if (!(v >= ctl_spec[i].min)) {   /* also NaN */
        v = ctl_spec[i].min;
    }
    if (v > ctl_spec[i].max) {
        v = ctl_spec[i].max;
    }
    return v;
}

static void
activate(LV2_Handle instance)
{
    Voice* self = (Voice*)instance;
    if (!self) {
        return;
    }

    for (uint32_t c = 0; c < self->n_ch; ++c) {
        Chan* ch = &self->ch[c];

        ch->lc_z = ch->de_z1 = ch->de_z2 = 0.0f;
        ch->eq_low = ch->eq_mid_hi = ch->eq_mid_lo = ch->eq_air = 0.0f;
        ch->dc_x = ch->dc_y = 0.0f;
        ch->dly_lp = ch->dly_hp = 0.0f;

        memset(ch->shortline.buf, 0, ch->shortline.len * sizeof(float));
        memset(ch->delay.buf,     0, ch->delay.len     * sizeof(float));
        ch->shortline.w = 0u;
        ch->delay.w     = 0u;

        for (int i = 0; i < N_COMB; ++i) {
            memset(ch->comb[i].buf, 0, ch->comb[i].len * sizeof(float));
            ch->comb[i].p = 0u;
            ch->comb[i].store = 0.0f;
        }
        for (int i = 0; i < N_ALLPASS; ++i) {
            memset(ch->allpass[i].buf, 0, ch->allpass[i].len * sizeof(float));
            ch->allpass[i].p = 0u;
        }
    }

    /* Start on the values the controls already hold, with no ramp: a plugin
       that fades its own gains up on every activate() clicks at every
       pedalboard load. */
    self->sm[SM_IN]         = db_to_lin(ctl_read(self, CTL_IN_GAIN));
    self->sm[SM_OUT]        = db_to_lin(ctl_read(self, CTL_OUTPUT));
    self->sm[SM_BODY]       = db_to_lin(ctl_read(self, CTL_BODY)) - 1.0f;
    self->sm[SM_PRESENCE]   = db_to_lin(ctl_read(self, CTL_PRESENCE)) - 1.0f;
    self->sm[SM_AIR]        = db_to_lin(ctl_read(self, CTL_AIR)) - 1.0f;
    self->sm[SM_DRIVE_PRE]  = drive_pre_of(ctl_read(self, CTL_DRIVE));
    self->sm[SM_DRIVE_POST] = drive_post_of(self->sm[SM_DRIVE_PRE]);
    self->sm[SM_DRIVE_MIX]  = ctl_read(self, CTL_DRIVE) * 0.01f;
    self->sm[SM_DOUBLER]    = ctl_read(self, CTL_DOUBLER)    * 0.01f;
    self->sm[SM_MOD]        = ctl_read(self, CTL_MOD)        * 0.01f;
    self->sm[SM_DELAY]      = ctl_read(self, CTL_DELAY_MIX)  * 0.01f;
    self->sm[SM_REVERB]     = ctl_read(self, CTL_REVERB_MIX) * 0.01f;

    self->gate_env  = 0.0f;
    self->gate_gain = 1.0f;
    self->gate_is_open = 1;
    self->gate_hold = 0u;
    self->comp_env  = 0.0f;
    self->de_env    = 0.0f;
    self->gr_db     = 0.0f;
    self->meter     = 0.0f;

    self->fx_state        = (ctl_read(self, CTL_FX) > 0.5f) ? 1 : 0;
    self->fx_toggle_prev  = self->fx_state;
    self->fx_trigger_prev = (ctl_read(self, CTL_FX_TRIGGER) > 0.5f) ? 1 : 0;
    self->fx_gain         = self->fx_state ? 1.0f : 0.0f;

    self->tap_prev     = (ctl_read(self, CTL_TAP) > 0.5f) ? 1 : 0;
    self->tap_count    = 0u;
    self->tap_active   = 0;
    self->tap_ms       = ctl_read(self, CTL_DELAY_TIME);
    self->knob_ms_prev = ctl_read(self, CTL_DELAY_TIME);
    self->delay_ms     = ctl_read(self, CTL_DELAY_TIME);

    self->ph_double_a = 0.0f;
    self->ph_double_b = 0.37f;
    self->ph_mod      = 0.0f;

    self->screen_left = 1u;
    self->forget_left = self->forget_period;
    forget_caches(self);
}

static void
deactivate(LV2_Handle instance)
{
    (void)instance;
}

static void
cleanup(LV2_Handle instance)
{
    Voice* self = (Voice*)instance;
    if (!self) {
        return;
    }
    free(self->pool);   /* one block: nothing else can be left behind */
    free(self);
}

/* ------------------------------------------------------------------ */
/* Screen                                                              */
/*                                                                     */
/* Seven controls have something to say when they are addressed to a   */
/* knob or a footswitch. Three of them say something the knob CANNOT   */
/* know: the delay time after a tap, how hard the compressor is        */
/* working, and how loud the output actually is. The other four just   */
/* label themselves properly.                                          */
/*                                                                     */
/* Every string sent from here is uppercase ASCII and short: 8          */
/* characters for a label or a value, 7 for a unit. The test bench      */
/* fails the build on anything else, because the device silently        */
/* truncates it instead.                                                */
/* ------------------------------------------------------------------ */

static void
paint(Voice* self, int force)
{
    const LV2_HMI_WidgetControl* hmi = self->hmi;
    if (!hmi) {
        return;
    }

    /* The time in force: the tap owns it until the knob moves. */
    const float time_ms = self->tap_active ? self->tap_ms : self->knob_ms_prev;

    for (int s = 0; s < (int)SLOT_COUNT; ++s) {
        const uint32_t idx = self->n_audio + (uint32_t)slot_ctl[s];
        const LV2_HMI_Addressing a = self->addr[idx];
        if (!a) {
            continue;
        }
        const uint32_t caps = self->caps[idx];

        char        vbuf[12];
        const char* label = NULL;
        const char* value = NULL;
        const char* unit  = NULL;
        float       bar   = 0.0f;
        int         bar_h = -1;     /* bar in hundredths, -1 = nothing to draw */
        int         led   = -1;     /* -1 = nothing to light */
        int         blink = 0;      /* blink period in ms, 0 = steady */

        switch ((ScreenSlot)s) {
        case SLOT_FX:
        case SLOT_FX_TRIGGER:
            /* Both handles on one state, each with its own cache: a
               shared one would skip the second write for matching. */
            label = "FX";
            value = self->fx_state ? "ON" : "OFF";
            led   = self->fx_state ? LV2_HMI_LED_Colour_Green
                                   : LV2_HMI_LED_Colour_Off;
            break;

        case SLOT_TAP: {
            const int ms = (int)(time_ms + 0.5f);
            label = "TAP";
            if (ms > 0) {
                write_int(vbuf, sizeof(vbuf), (60000 + ms / 2) / ms);
                value = vbuf;
                unit  = "BPM";
                /* The LED blinks the tempo back: 60 ms on, the rest off.
                   set_led_with_blink takes milliseconds up to 5000, and
                   the delay line stops at 2000, so no clamp is needed. */
                led   = LV2_HMI_LED_Colour_Cyan;
                blink = ms;
            }
            break;
        }

        case SLOT_DELAY: {
            const int ms = (int)(time_ms + 0.5f);
            /* The label says WHERE the value comes from. After a tap the
               knob position is stale and there is no honest way to move
               it, so the screen says TAP instead of pretending. */
            label = self->tap_active ? "TAP" : "DELAY";
            write_int(vbuf, sizeof(vbuf), ms);
            value = vbuf;
            unit  = "MS";
            bar   = (float)ms / DELAY_MAX_MS;
            bar_h = (int)(bar * 100.0f + 0.5f);
            break;
        }

        case SLOT_COMP: {
            /* The knob sets an amount; the screen shows what that amount
               is actually doing to this voice, right now. */
            const int db = (int)(self->gr_db - 0.5f);
            label = "COMP GR";
            write_int(vbuf, sizeof(vbuf), db);
            value = vbuf;
            unit  = "DB";
            bar   = -self->gr_db * (1.0f / 24.0f);
            if (bar > 1.0f) { bar = 1.0f; }
            bar_h = (int)(bar * 100.0f + 0.5f);
            break;
        }

        case SLOT_GATE:
            label = "GATE";
            value = self->gate_is_open ? "OPEN" : "SHUT";
            led   = self->gate_is_open ? LV2_HMI_LED_Colour_Green
                                       : LV2_HMI_LED_Colour_Red;
            bar   = self->gate_gain;
            if (bar < 0.0f) { bar = 0.0f; }
            if (bar > 1.0f) { bar = 1.0f; }
            bar_h = (int)(bar * 100.0f + 0.5f);
            break;

        case SLOT_OUT: {
            /* A level meter on the output knob. On a stage this is the
               one readout a singer actually looks at. */
            float db = lin_to_db(self->meter);
            if (db < -60.0f) { db = -60.0f; }
            if (db > 12.0f)  { db = 12.0f; }
            label = "OUT";
            write_int(vbuf, sizeof(vbuf), (int)(db < 0.0f ? db - 0.5f : db + 0.5f));
            value = vbuf;
            unit  = "DB";
            bar   = (db + 60.0f) * (1.0f / 72.0f);
            bar_h = (int)(bar * 100.0f + 0.5f);
            /* Red past -1 dB: that is where the ceiling starts working. */
            led   = (db > -1.0f) ? LV2_HMI_LED_Colour_Red
                                 : LV2_HMI_LED_Colour_Green;
            break;
        }

        case SLOT_COUNT:
        default:
            break;
        }

        if (label && (caps & LV2_HMI_AddressingCapability_Label)) {
            if (force || strcmp(label, self->cache_label[s])) {
                hmi->set_label(hmi->handle, a, label);
                copy_bounded(self->cache_label[s], sizeof(self->cache_label[s]), label);
            }
        }
        if (value && (caps & LV2_HMI_AddressingCapability_Value)) {
            if (force || strcmp(value, self->cache_value[s])) {
                hmi->set_value(hmi->handle, a, value);
                copy_bounded(self->cache_value[s], sizeof(self->cache_value[s]), value);
            }
        }
        if (unit && (caps & LV2_HMI_AddressingCapability_Unit)) {
            if (force || strcmp(unit, self->cache_unit[s])) {
                hmi->set_unit(hmi->handle, a, unit);
                copy_bounded(self->cache_unit[s], sizeof(self->cache_unit[s]), unit);
            }
        }
        if (bar_h >= 0 && (caps & LV2_HMI_AddressingCapability_Indicator)) {
            /* Compared in hundredths: no send for a change the screen
               cannot show anyway. */
            if (force || bar_h != self->cache_bar[s]) {
                hmi->set_indicator(hmi->handle, a, bar);
                self->cache_bar[s] = bar_h;
            }
        }
        if (led >= 0 && (caps & LV2_HMI_AddressingCapability_LED)) {
            if (force || led != self->cache_led[s] || blink != self->cache_blink[s]) {
                if (blink > 0) {
                    const int on = 60;
                    int off = blink - on;
                    if (off < 40) { off = 40; }
                    hmi->set_led_with_blink(hmi->handle, a,
                                            (LV2_HMI_LED_Colour)led, on, off);
                } else {
                    /* Never Brightness_Low: the RGB LEDs skew colour at
                       low brightness on this device. */
                    hmi->set_led_with_brightness(hmi->handle, a,
                                                 (LV2_HMI_LED_Colour)led,
                                                 LV2_HMI_LED_Brightness_High);
                }
                self->cache_led[s]   = led;
                self->cache_blink[s] = blink;
            }
        }
    }
}

static void
addressed(LV2_Handle handle, uint32_t index,
          LV2_HMI_Addressing addressing,
          const LV2_HMI_AddressingInfo* info)
{
    Voice* self = (Voice*)handle;
    if (!self || index >= (uint32_t)PORT_COUNT) {
        return;
    }

    self->addr[index] = addressing;
    self->caps[index] = info ? (uint32_t)info->caps : 0u;

    /* Paint AS SOON AS the control is addressed, even at zero: until the
       plugin has sent an indicator, the firmware draws its own default
       representation of the port instead of a readout. */
    forget_caches(self);
    paint(self, 1);
}

static void
unaddressed(LV2_Handle handle, uint32_t index)
{
    Voice* self = (Voice*)handle;
    if (!self || index >= (uint32_t)PORT_COUNT) {
        return;
    }
    /* No further call must target this addressing. */
    self->addr[index] = NULL;
    self->caps[index] = 0u;
}

/* ------------------------------------------------------------------ */
/* run(): the chain itself                                             */
/*                                                                     */
/* Everything that depends only on a control port is computed ONCE per  */
/* block; the per-sample loop below carries no divisions, no branches   */
/* on control values that could change under it, and no allocation.     */
/*                                                                     */
/* Both channels ride the SAME gate, compressor and de-esser gain. A    */
/* stereo pair of independent detectors pulls the image sideways every  */
/* time one side is louder, which on a voice is every sibilant.         */
/* ------------------------------------------------------------------ */

static void
run(LV2_Handle instance, uint32_t n_samples)
{
    Voice* self = (Voice*)instance;
    if (!self) {
        return;
    }
    /* Every output must point somewhere before a whole block is written. */
    for (uint32_t c = 0; c < self->n_ch; ++c) {
        if (!self->out[c]) {
            return;
        }
    }

    const uint32_t n_ch = self->n_ch;
    const float    rate = self->rate;
    const float    ms2n = rate * 0.001f;     /* milliseconds to samples */

    /* ---------------- controls, read and clamped once ---------------- */
    const float lowcut_hz = ctl_read(self, CTL_LOW_CUT);
    const float gate_db   = ctl_read(self, CTL_GATE);
    const float comp_amt  = ctl_read(self, CTL_COMP);
    const float deess_amt = ctl_read(self, CTL_DE_ESS);
    const float drive_amt = ctl_read(self, CTL_DRIVE);
    const float mod_speed = ctl_read(self, CTL_MOD_SPEED);
    const float mod_amt   = ctl_read(self, CTL_MOD) * 0.01f;
    const float fb_amt    = ctl_read(self, CTL_DELAY_REPEATS) * 0.01f;
    const float rev_amt   = ctl_read(self, CTL_REVERB) * 0.01f;
    const float out_db    = ctl_read(self, CTL_OUTPUT);

    /* ---------------- the FX switch: one state, two ways in ----------
       Same reasoning as fade.c. The toggle is followed by its CHANGES so
       it does not overwrite what the trigger just did, and the trigger by
       its RISING EDGES so a momentary footswitch counts once. */
    const int toggle_now = (ctl_read(self, CTL_FX) > 0.5f) ? 1 : 0;
    if (toggle_now != self->fx_toggle_prev) {
        self->fx_state       = toggle_now;
        self->fx_toggle_prev = toggle_now;
    }
    const int trigger_now = (ctl_read(self, CTL_FX_TRIGGER) > 0.5f) ? 1 : 0;
    if (trigger_now && !self->fx_trigger_prev) {
        self->fx_state = !self->fx_state;
    }
    self->fx_trigger_prev = trigger_now;

    /* ---------------- tap tempo ----------------
       Two taps set the delay time. The gap is counted in samples and
       resolved to the block, so a tap is accurate to one buffer — under
       three milliseconds at any rate the Dwarf runs, which is finer than
       a foot. A gap longer than the delay line means the player stopped
       tapping and started again, so it is dropped rather than clamped. */
    if (self->tap_count < (uint32_t)(rate * 10.0f)) {
        self->tap_count += n_samples;   /* counted BEFORE the edge is read:
                                           the block carrying the second tap
                                           is part of the gap, and leaving it
                                           out makes every tapped tempo one
                                           buffer fast */
    }
    const int tap_now = (ctl_read(self, CTL_TAP) > 0.5f) ? 1 : 0;
    if (tap_now && !self->tap_prev) {
        const float gap_ms = (float)self->tap_count / ms2n;
        if (gap_ms >= ctl_spec[CTL_DELAY_TIME].min && gap_ms <= DELAY_MAX_MS) {
            self->tap_ms     = gap_ms;
            self->tap_active = 1;
        }
        self->tap_count = 0u;
    }
    self->tap_prev = tap_now;

    /* Moving the knob takes the time back from the tap. The knob is
       followed by its CHANGES for the same reason the toggle is: the
       plugin cannot write the tapped value back into an input port, so
       its position goes stale the moment a tap lands. TIME publishes what
       is really in force. */
    const float knob_ms = ctl_read(self, CTL_DELAY_TIME);
    if (knob_ms > self->knob_ms_prev + 0.5f || knob_ms < self->knob_ms_prev - 0.5f) {
        self->tap_active = 0;
    }
    self->knob_ms_prev = knob_ms;
    const float time_target = self->tap_active ? self->tap_ms : knob_ms;

    /* ---------------- coefficients ---------------- */
    const float lc_c     = onepole_coef(lowcut_hz, rate);
    const float de_c     = onepole_coef(5500.0f, rate);
    const float eq_low_c = onepole_coef(240.0f, rate);
    const float eq_mh_c  = onepole_coef(4500.0f, rate);
    const float eq_ml_c  = onepole_coef(1000.0f, rate);
    const float eq_air_c = onepole_coef(6000.0f, rate);
    const float dc_r     = 1.0f - onepole_coef(20.0f, rate);

    const float gate_att = env_coef(1.0f, rate);
    const float gate_rel = env_coef(60.0f, rate);
    const float gg_att   = env_coef(2.0f, rate);
    const float gg_rel   = env_coef(120.0f, rate);
    const float comp_att = env_coef(5.0f, rate);
    const float comp_rel = env_coef(120.0f, rate);
    const float de_att   = env_coef(0.5f, rate);
    const float de_rel   = env_coef(30.0f, rate);

    /* Gate. At its minimum the control means OFF, not "threshold at
       -80 dB": a threshold that low would chatter on room noise. */
    const int      gate_on    = gate_db > ctl_spec[CTL_GATE].min + 0.5f;
    const float    gate_open  = db_to_lin(gate_db);
    const float    gate_close = gate_open * 0.5f;         /* -6 dB hysteresis */
    const uint32_t gate_hold  = (uint32_t)(rate * 0.08f); /* 80 ms */

    /* Compressor: one knob. It opens the threshold downwards and the
       ratio upwards together, which is how a singer thinks about "more
       compression", and adds back most of what it takes off. */
    const int   comp_on     = comp_amt > 0.5f;
    const float comp_thr    = -comp_amt * 0.4f;              /* 0 .. -40 dB */
    const float comp_ratio  = 1.0f + comp_amt * 0.05f;       /* 1 .. 6 : 1 */
    const float comp_slope  = 1.0f - 1.0f / comp_ratio;
    const float comp_makeup = -comp_thr * comp_slope * 0.6f;
    const float knee        = 6.0f;
    const float knee_half   = knee * 0.5f;

    /* De-esser: a compressor on the band above 5.5 kHz only, so it takes
       the edge off an S without dulling the whole word.

       The split has to be COMPLEMENTARY - the two bands must add back up
       to the input exactly - or the phase between them eats the
       reduction. Two high passes in series measured 0.2 dB of ducking on
       an 8 kHz tone where the arithmetic promised 26; a two-pole low pass
       with the band taken as (input - low pass) gives 11 dB, because
       there the two halves really do sum to the input. */
    const int   deess_on    = deess_amt > 0.5f;
    const float deess_thr   = -12.0f - deess_amt * 0.28f;    /* -12 .. -40 dB */
    const float deess_slope = 1.0f - 1.0f / (1.0f + deess_amt * 0.07f);

    /* Delay feedback tone: repeats lose their top and their bottom, so a
       long tail sits behind the voice instead of fighting it. */
    const float fb_lp_c = onepole_coef(3500.0f, rate);
    const float fb_hp_c = onepole_coef(120.0f, rate);

    /* Reverb: one control moves the tail length and the damping together.
       0.015 is Freeverb's input gain; the eight combs sum to far more
       than unity without it. */
    const float rev_room  = 0.70f + rev_amt * 0.28f;
    const float rev_damp1 = (0.20f + rev_amt * 0.40f) * 0.4f;
    const float rev_damp2 = 1.0f - rev_damp1;
    const float rev_in_g  = 0.015f;

    const float drive_pre  = drive_pre_of(drive_amt);
    const float drive_post = drive_post_of(drive_pre);

    /* ---------------- smoothing ----------------
       Anything a hand can turn walks to its new value across the block. */
    float target[SM_COUNT];
    target[SM_IN]         = db_to_lin(ctl_read(self, CTL_IN_GAIN));
    target[SM_OUT]        = (out_db <= ctl_spec[CTL_OUTPUT].min + 0.5f)
                          ? 0.0f : db_to_lin(out_db);   /* the minimum is silence */
    target[SM_BODY]       = db_to_lin(ctl_read(self, CTL_BODY))     - 1.0f;
    target[SM_PRESENCE]   = db_to_lin(ctl_read(self, CTL_PRESENCE)) - 1.0f;
    target[SM_AIR]        = db_to_lin(ctl_read(self, CTL_AIR))      - 1.0f;
    target[SM_DRIVE_PRE]  = drive_pre;
    target[SM_DRIVE_POST] = drive_post;
    target[SM_DRIVE_MIX]  = drive_amt * 0.01f;
    target[SM_DOUBLER]    = ctl_read(self, CTL_DOUBLER) * 0.01f;
    target[SM_MOD]        = mod_amt;
    target[SM_DELAY]      = ctl_read(self, CTL_DELAY_MIX)  * 0.01f;
    target[SM_REVERB]     = ctl_read(self, CTL_REVERB_MIX) * 0.01f;

    float sm[SM_COUNT], sm_step[SM_COUNT];
    for (int k = 0; k < (int)SM_COUNT; ++k) {
        sm[k]      = self->sm[k];
        sm_step[k] = (n_samples > 0u) ? (target[k] - sm[k]) / (float)n_samples : 0.0f;
    }

    const float fx_target = self->fx_state ? 1.0f : 0.0f;
    const float fx_step   = 1.0f / (FX_RAMP_MS * 0.001f * rate);
    const float glide     = env_coef(120.0f, rate);

    const float inc_a = 0.27f / rate;    /* the two doubler drifts, slow  */
    const float inc_b = 0.19f / rate;    /* and mutually prime in period  */
    const float inc_m = mod_speed / rate;

    float gr_worst = 0.0f;
    float peak     = 0.0f;

    /* ---------------- the loop ---------------- */
    for (uint32_t i = 0; i < n_samples; ++i) {
        for (int k = 0; k < (int)SM_COUNT; ++k) {
            sm[k] += sm_step[k];
        }

        if (self->fx_gain < fx_target) {
            self->fx_gain += fx_step;
            if (self->fx_gain > fx_target) { self->fx_gain = fx_target; }
        } else if (self->fx_gain > fx_target) {
            self->fx_gain -= fx_step;
            if (self->fx_gain < fx_target) { self->fx_gain = fx_target; }
        }

        self->delay_ms += glide * (time_target - self->delay_ms);
        const float delay_n = self->delay_ms * ms2n;

        /* --- input gain and low cut --- */
        float x[MAX_CH];
        float det = 0.0f;
        for (uint32_t c = 0; c < n_ch; ++c) {
            Chan* ch = &self->ch[c];
            float v = self->in[c] ? self->in[c][i] : 0.0f;
            v *= sm[SM_IN];
            ch->lc_z = flush(ch->lc_z + lc_c * (v - ch->lc_z));
            v -= ch->lc_z;                 /* high pass = input minus its low pass */
            x[c] = v;
            const float a = absf(v);
            if (a > det) { det = a; }
        }

        /* --- gate --- */
        if (gate_on) {
            const float ce = (det > self->gate_env) ? gate_att : gate_rel;
            self->gate_env = flush(self->gate_env + ce * (det - self->gate_env));

            if (self->gate_env > gate_open) {
                self->gate_is_open = 1;
                self->gate_hold    = gate_hold;
            } else if (self->gate_hold > 0u) {
                --self->gate_hold;
            } else if (self->gate_env < gate_close) {
                self->gate_is_open = 0;
            }
            const float t  = self->gate_is_open ? 1.0f : 0.0f;
            const float cg = (t > self->gate_gain) ? gg_att : gg_rel;
            /* Flushed: a shut gate decays towards zero for ever, and the
               last stretch of that curve is nothing but denormals. */
            self->gate_gain = flush(self->gate_gain + cg * (t - self->gate_gain));
        } else {
            self->gate_is_open = 1;
            self->gate_gain += gg_att * (1.0f - self->gate_gain);
        }
        for (uint32_t c = 0; c < n_ch; ++c) {
            x[c] *= self->gate_gain;
        }
        det *= self->gate_gain;

        /* --- compressor --- */
        if (comp_on) {
            const float ce = (det > self->comp_env) ? comp_att : comp_rel;
            self->comp_env = flush(self->comp_env + ce * (det - self->comp_env));

            const float over = lin_to_db(self->comp_env) - comp_thr;
            float red = 0.0f;
            if (over >= knee_half) {
                red = comp_slope * over;
            } else if (over > -knee_half) {
                const float t = over + knee_half;      /* soft knee, 6 dB wide */
                red = comp_slope * t * t / (2.0f * knee);
            }
            if (red > gr_worst) { gr_worst = red; }

            const float g = db_to_lin(comp_makeup - red);
            for (uint32_t c = 0; c < n_ch; ++c) {
                x[c] *= g;
            }
        }

        /* --- de-esser --- */
        if (deess_on) {
            float hf[MAX_CH];
            float hdet = 0.0f;
            for (uint32_t c = 0; c < n_ch; ++c) {
                Chan* ch = &self->ch[c];
                ch->de_z1 = flush(ch->de_z1 + de_c * (x[c] - ch->de_z1));
                ch->de_z2 = flush(ch->de_z2 + de_c * (ch->de_z1 - ch->de_z2));
                hf[c] = x[c] - ch->de_z2;   /* what the two poles did not keep */
                const float a = absf(hf[c]);
                if (a > hdet) { hdet = a; }
            }
            const float ce = (hdet > self->de_env) ? de_att : de_rel;
            self->de_env = flush(self->de_env + ce * (hdet - self->de_env));

            const float over = lin_to_db(self->de_env) - deess_thr;
            if (over > 0.0f) {
                const float g = db_to_lin(-deess_slope * over);
                for (uint32_t c = 0; c < n_ch; ++c) {
                    x[c] -= (1.0f - g) * hf[c];        /* the band, quieter */
                }
            }
        }

        /* --- tone: three bands taken from the same signal, added back ---
           Parallel rather than cascaded: the bands overlap, so the knobs
           interact a little, but nothing can ring and the phase stays
           gentle, which is what a voice needs. */
        for (uint32_t c = 0; c < n_ch; ++c) {
            Chan* ch = &self->ch[c];
            const float v = x[c];
            ch->eq_low    = flush(ch->eq_low    + eq_low_c * (v - ch->eq_low));
            ch->eq_mid_hi = flush(ch->eq_mid_hi + eq_mh_c  * (v - ch->eq_mid_hi));
            ch->eq_mid_lo = flush(ch->eq_mid_lo + eq_ml_c  * (v - ch->eq_mid_lo));
            ch->eq_air    = flush(ch->eq_air    + eq_air_c * (v - ch->eq_air));

            x[c] = v + sm[SM_BODY]     * ch->eq_low
                     + sm[SM_PRESENCE] * (ch->eq_mid_hi - ch->eq_mid_lo)
                     + sm[SM_AIR]      * (v - ch->eq_air);
        }

        /* --- drive, then a DC blocker: saturation on a signal that
               already carries an offset makes that offset bigger.
               The saturated signal is BLENDED in rather than switched in,
               so DRIVE at zero leaves the sample exactly as it was. A
               stage that is bypassed by a branch instead steps the level
               of a loud passage by nearly two decibels the moment the
               control leaves zero. --- */
        for (uint32_t c = 0; c < n_ch; ++c) {
            Chan* ch = &self->ch[c];
            const float sat = softclip(x[c] * sm[SM_DRIVE_PRE]) * sm[SM_DRIVE_POST];
            const float v   = x[c] + sm[SM_DRIVE_MIX] * (sat - x[c]);
            const float y = v - ch->dc_x + dc_r * ch->dc_y;
            ch->dc_x = v;
            ch->dc_y = flush(y);
            x[c] = y;
        }

        /* --- what goes to the effects. The switch acts HERE, on the
               send: cutting the return would chop the tails. --- */
        float send[MAX_CH];
        float send_sum = 0.0f;
        for (uint32_t c = 0; c < n_ch; ++c) {
            send[c]   = x[c] * self->fx_gain;
            send_sum += send[c];
        }
        if (n_ch > 1u) { send_sum *= 0.5f; }

        /* --- doubler and modulation, both reading one short line ---
               The doubler is two taps a few tens of milliseconds apart,
               each drifting on its own slow LFO. The drift is what makes
               it sound like a second take rather than a copy: a moving
               delay is a pitch difference, and it needs no pitch
               detection to produce one. */
        const float lfo_a = lfo_sin(self->ph_double_a);
        const float lfo_b = lfo_sin(self->ph_double_b);
        const float dbl_a = (19.0f + 1.2f * lfo_a) * ms2n;
        const float dbl_b = (31.0f + 1.2f * lfo_b) * ms2n;
        const float depth = 0.5f + 3.5f * mod_amt;

        float wet[MAX_CH];
        float dly_sum = 0.0f;
        for (uint32_t c = 0; c < n_ch; ++c) {
            Chan* ch = &self->ch[c];
            ring_write(&ch->shortline, send[c]);

            float w;
            if (n_ch > 1u) {
                /* one tap each side: that IS the width */
                w = ring_read(&ch->shortline, c == 0u ? dbl_a : dbl_b);
            } else {
                w = 0.5f * (ring_read(&ch->shortline, dbl_a)
                          + ring_read(&ch->shortline, dbl_b));
            }
            wet[c] = w * sm[SM_DOUBLER] * 0.9f;

            const float mod_ph = self->ph_mod + (c ? 0.25f : 0.0f);
            const float d_mod  = (8.0f + depth * lfo_sin(mod_ph)) * ms2n;
            wet[c] += ring_read(&ch->shortline, d_mod) * sm[SM_MOD] * 0.7f;

            /* --- delay --- */
            const float dly = ring_read(&ch->delay, delay_n);
            ch->dly_lp = flush(ch->dly_lp + fb_lp_c * (dly - ch->dly_lp));
            const float band = ch->dly_lp;
            ch->dly_hp = flush(ch->dly_hp + fb_hp_c * (band - ch->dly_hp));
            ring_write(&ch->delay, flush(send[c] + (band - ch->dly_hp) * fb_amt));

            wet[c] += dly * sm[SM_DELAY];
            dly_sum += dly;
        }
        if (n_ch > 1u) { dly_sum *= 0.5f; }

        /* --- reverb, fed by the send AND by the delay, so the repeats
               are in the room too --- */
        const float rev_in = (send_sum + dly_sum * sm[SM_DELAY]) * rev_in_g;
        for (uint32_t c = 0; c < n_ch; ++c) {
            Chan* ch = &self->ch[c];
            float r = 0.0f;
            for (int k = 0; k < N_COMB; ++k) {
                r += comb_run(&ch->comb[k], rev_in, rev_room, rev_damp1, rev_damp2);
            }
            for (int k = 0; k < N_ALLPASS; ++k) {
                r = allpass_run(&ch->allpass[k], r);
            }
            /* 3.0 is measured, not chosen: it puts the tail level with
               the mix at maximum within half a decibel of the dry signal,
               so REVERB MIX at 100 means a wet sound rather than a hint
               of one. */
            wet[c] += r * sm[SM_REVERB] * 3.0f;

            /* --- out --- */
            const float y = ceiling((x[c] + wet[c]) * sm[SM_OUT]);
            self->out[c][i] = y;
            const float a = absf(y);
            if (a > peak) { peak = a; }
        }

        self->ph_double_a += inc_a;
        if (self->ph_double_a >= 1.0f) { self->ph_double_a -= 1.0f; }
        self->ph_double_b += inc_b;
        if (self->ph_double_b >= 1.0f) { self->ph_double_b -= 1.0f; }
        self->ph_mod += inc_m;
        if (self->ph_mod >= 1.0f) { self->ph_mod -= 1.0f; }
    }

    /* Land exactly on the targets rather than on what the ramp
       accumulated: otherwise rounding leaves an offset that never
       settles, and a gain that never quite reaches 0 dB is audible on a
       bypass comparison. */
    for (int k = 0; k < (int)SM_COUNT; ++k) {
        self->sm[k] = target[k];
    }

    /* Meter: instant on the way up, about 300 ms on the way down, so a
       peak stays visible long enough to be seen. */
    if (peak > self->meter) {
        self->meter = peak;
    } else {
        const float mc = (float)n_samples / (0.3f * rate + (float)n_samples);
        self->meter += mc * (peak - self->meter);
    }
    self->gr_db = -gr_worst;

    /* --- outputs. Each one is the honest answer to a question an input
           port cannot answer, because a plugin must not write into one. */
    if (self->ctl_out[CTL_GR]) {
        float v = self->gr_db;
        if (v < ctl_spec[CTL_GR].min) { v = ctl_spec[CTL_GR].min; }
        *self->ctl_out[CTL_GR] = v;
    }
    if (self->ctl_out[CTL_LEVEL]) {
        *self->ctl_out[CTL_LEVEL] = (self->meter > 1.0f) ? 1.0f : self->meter;
    }
    if (self->ctl_out[CTL_GATE_OPEN]) {
        *self->ctl_out[CTL_GATE_OPEN] = self->gate_is_open ? 1.0f : 0.0f;
    }
    if (self->ctl_out[CTL_FX_STATE]) {
        *self->ctl_out[CTL_FX_STATE] = self->fx_state ? 1.0f : 0.0f;
    }
    if (self->ctl_out[CTL_TIME_OUT]) {
        *self->ctl_out[CTL_TIME_OUT] = time_target;
    }

    /* Screen: at most SCREEN_HZ passes per second, plus a full cache
       flush once a second to survive the firmware's repaints. */
    if (self->hmi) {
        int force = 0;

        if (self->forget_left <= n_samples) {
            self->forget_left = self->forget_period;
            forget_caches(self);
            force = 1;
        } else {
            self->forget_left -= n_samples;
        }

        if (self->screen_left <= n_samples || force) {
            self->screen_left = self->screen_period;
            paint(self, force);
        } else {
            self->screen_left -= n_samples;
        }
    }
}

/* ------------------------------------------------------------------ */

static const LV2_HMI_PluginNotification notification = {
    addressed,
    unaddressed
};

static const void*
extension_data(const char* uri)
{
    if (uri && !strcmp(uri, LV2_HMI__PluginNotification)) {
        return &notification;
    }
    return NULL;
}

static const LV2_Descriptor descriptor_mono = {
    VOICE_URI,
    instantiate,
    connect_port,
    activate,
    run,
    deactivate,
    cleanup,
    extension_data
};

static const LV2_Descriptor descriptor_stereo = {
    VOICE_STEREO_URI,
    instantiate,
    connect_port,
    activate,
    run,
    deactivate,
    cleanup,
    extension_data
};

LV2_SYMBOL_EXPORT
const LV2_Descriptor*
lv2_descriptor(uint32_t index)
{
    switch (index) {
    case 0:  return &descriptor_mono;
    case 1:  return &descriptor_stereo;
    default: return NULL;
    }
}

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

#if defined(__has_include)
#  if __has_include(<lv2/urid/urid.h>)
#    include <lv2/urid/urid.h>
#    include <lv2/state/state.h>
#    include <lv2/atom/atom.h>
#  else
#    include <lv2/lv2plug.in/ns/ext/urid/urid.h>
#    include <lv2/lv2plug.in/ns/ext/state/state.h>
#    include <lv2/lv2plug.in/ns/ext/atom/atom.h>
#  endif
#else
#  include <lv2/lv2plug.in/ns/ext/urid/urid.h>
#  include <lv2/lv2plug.in/ns/ext/state/state.h>
#  include <lv2/lv2plug.in/ns/ext/atom/atom.h>
#endif

#include <stdlib.h>
#include <string.h>   /* also pulls in stddef for size_t, which lv2-hmi.h needs */

#include "lv2-hmi.h"

#define VOICE_URI        "http://remy-live.github.io/lv2/voice"
/* Where the four USER slots live in the host's saved state. */
#define VOICE_SLOTS_URI  "http://remy-live.github.io/lv2/voice#userSlots"
#define VOICE_STEREO_URI "http://remy-live.github.io/lv2/voice#stereo"

#define MAX_CH 2

/* Build stamp, readable on both ends with:
     grep -ao 'VOICE_BUILD[A-Za-z0-9_]*' voice.so
   The ARCHITECTURE is part of it on purpose: a stamp that does not name
   the architecture once let a 32-bit binary pass a check meant to catch
   exactly that. */
__attribute__((used))
static const volatile char build_tag[] = "VOICE_BUILD7_AARCH64_20260905";

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
/* -12 dBFS in decibels: where a voice sits after the compressor, and the
   level at which both the compressor and the drive are matched. */
#define REF_DB (-12.0f)

static float drive_pre_of(float amount)
{
    return 1.0f + amount * 0.19f;
}

static float drive_post_of(float pre)
{
    const float y = softclip(pre * DRIVE_REF);
    return (y > 1.0e-6f) ? DRIVE_REF / y : 1.0f;
}

/* How many dB the compressor takes off a signal that is `over` dB past the
   threshold. Shared by the loop and by the makeup calculation below, which
   is the whole point: the gain given back is the reduction this same curve
   applies at the reference level, not a guess derived from the threshold. */
static float comp_reduction(float over, float slope, float knee)
{
    const float half = knee * 0.5f;
    if (over >= half) {
        return slope * over;
    }
    if (over > -half) {
        const float t = over + half;
        return slope * t * t / (2.0f * knee);
    }
    return 0.0f;
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
    CTL_PROGRAM       = 0,   /* the list: MANUAL, the built-in sounds, USER 1..6 */
    CTL_USER_SLOT     = 1,   /* where SAVE writes - a list of its own */
    CTL_SAVE          = 2,   /* trigger: stores what is being heard */
    CTL_IN_GAIN       = 3,
    CTL_LOW_CUT       = 4,
    CTL_GATE_ON       = 5,   /* every effect has a switch of its own, and    */
    CTL_GATE          = 6,   /* it sits immediately before the controls it   */
    CTL_COMP_ON       = 7,   /* switches: mod-ui lists the ports in index    */
    CTL_COMP          = 8,   /* order, so the order IS the layout            */
    CTL_DE_ESS_ON     = 9,
    CTL_DE_ESS        = 10,
    CTL_EQ_ON         = 11,
    CTL_BODY          = 12,
    CTL_MID_FREQ      = 13,
    CTL_PRESENCE      = 14,
    CTL_AIR           = 15,
    CTL_DRIVE_ON      = 16,
    CTL_DRIVE         = 17,
    CTL_PITCH_ON      = 18,
    CTL_PITCH         = 19,  /* semitones, no pitch detection anywhere */
    CTL_PITCH_MIX     = 20,
    CTL_DOUBLER_ON    = 21,
    CTL_DOUBLER       = 22,
    CTL_SPREAD        = 23,  /* how far apart the doubled voices stand */
    CTL_VOICES        = 24,  /* 2, 3 or 4 of them */
    CTL_MOD_ON        = 25,
    CTL_MOD           = 26,
    CTL_MOD_SPEED     = 27,
    CTL_FEEDBACK_ON   = 28,
    CTL_FEEDBACK      = 29,  /* the anti-Larsen hunter */
    CTL_DELAY_ON      = 30,
    CTL_DELAY_TIME    = 31,
    CTL_DELAY_REPEATS = 32,
    CTL_DELAY_MIX     = 33,
    CTL_REVERB_ON     = 34,
    CTL_REVERB        = 35,
    CTL_REVERB_MIX    = 36,
    CTL_FX            = 37,  /* the master: it feeds all four at once */
    CTL_FX_2          = 38,  /* a second switch on the same state */
    CTL_TAP           = 39,  /* trigger: two taps set the delay time */
    CTL_OUTPUT        = 40,
    CTL_GR            = 41,  /* output: compressor gain reduction, dB */
    CTL_LEVEL         = 42,  /* output: peak out level, 0..1 */
    CTL_GATE_OPEN     = 43,  /* output: 1 while the gate is open */
    CTL_FX_STATE      = 44,  /* output: the FX state actually in force */
    CTL_NOTCHES       = 45,  /* output: anti-Larsen notches in place */
    CTL_TIME_OUT      = 46,  /* output: delay time in force, tap included */
    CTL_COUNT         = 47
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
    { "program",        0.0f,   72.0f,     0.0f },
    { "user_slot",      1.0f,    6.0f,     1.0f },
    { "save",           0.0f,    1.0f,     0.0f },
    { "in_gain",      -20.0f,   40.0f,     0.0f },
    { "low_cut",        0.0f,  400.0f,    90.0f },
    { "gate_on",        0.0f,    1.0f,     1.0f },
    { "gate",         -80.0f,  -20.0f,   -80.0f },
    { "comp_on",        0.0f,    1.0f,     1.0f },
    { "comp",           0.0f,  100.0f,    30.0f },
    { "de_ess_on",      0.0f,    1.0f,     1.0f },
    { "de_ess",         0.0f,  100.0f,     0.0f },
    { "eq_on",          0.0f,    1.0f,     1.0f },
    { "body",         -12.0f,   12.0f,     0.0f },
    { "mid_freq",     300.0f, 5000.0f,  2200.0f },
    { "presence",     -12.0f,   12.0f,     0.0f },
    { "air",          -12.0f,   12.0f,     0.0f },
    { "drive_on",       0.0f,    1.0f,     1.0f },
    { "drive",          0.0f,  100.0f,     0.0f },
    { "pitch_on",       0.0f,    1.0f,     1.0f },
    { "pitch",        -12.0f,   12.0f,     0.0f },
    { "pitch_mix",      0.0f,  100.0f,   100.0f },
    { "doubler_on",     0.0f,    1.0f,     1.0f },
    { "doubler",        0.0f,  100.0f,     0.0f },
    { "spread",         0.0f,  100.0f,    50.0f },
    { "voices",         2.0f,    4.0f,     3.0f },
    { "mod_on",         0.0f,    1.0f,     1.0f },
    { "modulation",     0.0f,  100.0f,     0.0f },
    { "mod_speed",      0.05f,   8.0f,     0.6f },
    { "feedback_on",    0.0f,    1.0f,     1.0f },
    { "feedback",       0.0f,  100.0f,     0.0f },
    { "delay_on",       0.0f,    1.0f,     1.0f },
    { "delay_time",    20.0f, 2000.0f,   400.0f },
    { "delay_repeats",  0.0f,   95.0f,    30.0f },
    { "delay_mix",      0.0f,  100.0f,     0.0f },
    { "reverb_on",      0.0f,    1.0f,     1.0f },
    { "reverb",         0.0f,  100.0f,    40.0f },
    { "reverb_mix",     0.0f,  100.0f,     0.0f },
    { "fx",             0.0f,    1.0f,     1.0f },
    { "fx_2",           0.0f,    1.0f,     1.0f },
    { "tap",            0.0f,    1.0f,     0.0f },
    { "output",       -60.0f,   12.0f,     0.0f },
    { "gr",           -24.0f,    0.0f,     0.0f },
    { "level",          0.0f,    1.0f,     0.0f },
    { "gate_open",      0.0f,    1.0f,     0.0f },
    { "fx_state",       0.0f,    1.0f,     1.0f },
    { "notches",        0.0f,    4.0f,     0.0f },
    { "time_out",      20.0f, 2000.0f,   400.0f },
};

/* The built-in sounds, generated from the same table that writes
   presets.ttl. See programs.h. */
#include "programs.h"



/* One ramp per switch, so a foot on any of them fades rather than clicks.
   The four effect switches and the FX master multiply together: the master
   is the "all of it, off" stomp, each switch is "this one, off". */
typedef enum {
    SW_GATE = 0, SW_COMP, SW_DE_ESS, SW_EQ, SW_DRIVE, SW_PITCH,
    SW_DOUBLER, SW_MOD, SW_FEEDBACK, SW_DELAY, SW_REVERB, SW_COUNT
} SwitchIndex;

static const uint8_t switch_ctl[SW_COUNT] = {
    CTL_GATE_ON, CTL_COMP_ON, CTL_DE_ESS_ON, CTL_EQ_ON, CTL_DRIVE_ON,
    CTL_PITCH_ON, CTL_DOUBLER_ON, CTL_MOD_ON, CTL_FEEDBACK_ON,
    CTL_DELAY_ON, CTL_REVERB_ON
};

/* Eight characters at most: the device truncates silently. */
static const char* const switch_label[SW_COUNT] = {
    "GATE", "COMP", "DE-ESS", "EQ", "DRIVE", "PITCH",
    "DOUBLE", "MOD", "NO HOWL", "DELAY", "REVERB"
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

/* The three doubled voices. Delays in the twenties and thirties of
   milliseconds read as a second take; below about fifteen they start to
   comb, above about fifty they become a slapback. The depths give a few
   cents of drift each - depth * 2 * pi * rate, in seconds per second -
   and the rates share no common period. */
#define MAX_VOICES 4

/* The doubled voices.
 *
 * The first version of this was four taps of one delay line, each wobbled
 * by a slow LFO. It sounded like a comb filter, because that is what it
 * was: four copies of one voice at four fixed pitches, which is one voice.
 * The drift it had was worth about two cents - a twentieth of what a room
 * of singers is worth.
 *
 * These voices are each a separate micro-shifter running through the same
 * granular reader the PITCH control uses. What makes a stack read as
 * PEOPLE rather than as an effect is three things at once, and all three
 * have to be per voice:
 *   - a STATIC detune, ten to twenty cents apart, so they are not the
 *     same note;
 *   - a SLOW drift, seconds long, so the intervals between them keep
 *     changing - a chord that never settles;
 *   - a VIBRATO around five hertz at its own rate and phase, because a
 *     singer's pitch is never still and two singers are never still
 *     together.
 * On top of that they enter at different times, and each is filtered a
 * little differently, because two people do not have one throat.
 */
static const float choir_ms[MAX_VOICES]       = { 26.0f, 34.0f, 42.0f, 52.0f };
/* The detunes at SPREAD 50. No two are symmetric about zero and no two
   are in a small-integer ratio: a voice at -9 and a voice at +9 beat
   against the lead at the same rate and their beats lock into one
   pulsation, which the ear hears as a tremolo rather than as a group. */
static const float choir_cents[MAX_VOICES]    = { -7.0f, 11.0f, -16.0f, 23.0f };
static const float choir_drift[MAX_VOICES]    = { 6.0f, 7.0f, 5.0f, 7.5f };
static const float choir_drift_hz[MAX_VOICES] = { 0.073f, 0.119f, 0.167f, 0.101f };
/* A little vibrato, not a lot: the singer already has their own, and a
   copy that wobbles hard is a chorus pedal rather than a second person. */
static const float choir_vib[MAX_VOICES]      = { 5.0f, 4.0f, 6.0f, 4.5f };
static const float choir_vib_hz[MAX_VOICES]   = { 4.7f, 5.3f, 6.1f, 5.7f };
/* And the vibrato itself swells and relaxes, over half a minute or so,
   each voice at its own rate. A vibrato of constant depth is the one
   thing in the stack that no singer does: it is what makes four copies
   read as four oscillators rather than as four people. */
static const float choir_swell_hz[MAX_VOICES] = { 0.041f, 0.067f, 0.031f, 0.089f };
/* Each voice through its own throat, top and bottom: identical spectra
   fuse back into one object however far apart they are tuned. */
static const float choir_tone[MAX_VOICES]     = { 8500.0f, 5200.0f, 6800.0f, 4200.0f };
static const float choir_cut[MAX_VOICES]      = { 150.0f, 115.0f, 220.0f, 130.0f };
/* Different window lengths as well, so no two voices turn their grains
   over together. */
static const float choir_win_ms[MAX_VOICES]   = { 58.0f, 66.0f, 74.0f, 82.0f };

/* The level is held steady as the count changes, so VOICES picks a
   texture and not a volume. Decorrelated copies add in power, so the
   gain goes as one over the root of the count - written out rather than
   computed, because there is no sqrt in this binary. Indexed by the
   count, so the first two entries are never used. */
static const float double_gain[MAX_VOICES + 1]    = { 0.0f, 0.0f, 0.760f, 0.620f, 0.540f };
static const float double_gain_st[MAX_VOICES + 1] = { 0.0f, 0.0f, 1.170f, 0.960f, 0.830f };

/* ------------------------------------------------------------------ */
/* The anti-Larsen hunter                                              */
/*                                                                     */
/* A distorted guitar in front of its own monitor howls, and a noise   */
/* gate does nothing about it: the howl happens while you are playing. */
/* What stops it is a narrow notch exactly where it is ringing.        */
/*                                                                     */
/* No FFT here - a bank of band-pass filters does the listening. The   */
/* hard part is not hearing the howl, it is telling a howl from a held */
/* note, because both are loud, both are narrow and both last. The     */
/* difference this uses is STEADINESS: a howl sits at one level for    */
/* seconds because the room is holding it there, while a played or     */
/* sung note decays, breathes, and has vibrato on it. So a band has to */
/* dominate the whole signal AND stay within a couple of decibels of   */
/* itself for the best part of a second before a notch lands on it.    */
/* ------------------------------------------------------------------ */

#define N_BAND  16
#define N_NOTCH  4
/* Every third of an octave from 160 Hz to 8 kHz: where feedback lives on
   a stage, and fine enough that a notch of the same width covers whatever
   is howling inside the band that spotted it. */
static const float band_hz[N_BAND] = {
    160.0f,  207.0f,  267.0f,  345.0f,  446.0f,  576.0f,  744.0f,  961.0f,
   1241.0f, 1603.0f, 2071.0f, 2675.0f, 3455.0f, 4463.0f, 5765.0f, 7446.0f
};

/* A topology-preserving state variable filter. Its band-pass output is
   what the detector listens to, and `in - k*bp` is a notch at the same
   frequency - the same three coefficients serve both jobs. */
typedef struct {
    float ic1, ic2;
} SVF;

typedef struct {
    float a1, a2, a3, k;
} SVFCoef;

/* tan(pi*f/fs), which is the coefficient a TPT filter needs and the one
   thing about it that would want libm. The series is good to a third of
   a percent up to 0.6 radians, which reaches 9 kHz at 48 kHz - past the
   highest band here. */
static float tan_approx(float x)
{
    if (x > 0.6f) { x = 0.6f; }
    const float x2 = x * x;
    return x * (1.0f + x2 * (1.0f / 3.0f + x2 * (2.0f / 15.0f + x2 * (17.0f / 315.0f))));
}

static void svf_set(SVFCoef* c, float hz, float q, float rate)
{
    if (!(hz > 0.0f))        { hz = 1.0f; }
    if (hz > rate * 0.45f)   { hz = rate * 0.45f; }
    const float g = tan_approx(3.1415927f * hz / rate);
    c->k  = 1.0f / q;
    c->a1 = 1.0f / (1.0f + g * (g + c->k));
    c->a2 = g * c->a1;
    c->a3 = g * c->a2;
}

/* Returns the band-pass output and leaves the state ready for the next
   sample. The notch is the caller's business: input minus k times this. */
static float svf_bp(SVF* s, const SVFCoef* c, float x)
{
    const float v3 = x - s->ic2;
    const float v1 = c->a1 * s->ic1 + c->a2 * v3;
    const float v2 = s->ic2 + c->a2 * s->ic1 + c->a3 * v3;
    s->ic1 = flush(2.0f * v1 - s->ic1);
    s->ic2 = flush(2.0f * v2 - s->ic2);
    return v1;
}

/* One decision every 64 samples - about every millisecond and a half.
   The filters themselves run at full rate; only the arithmetic that
   decides anything is decimated, and nothing it looks at moves faster
   than a tenth of a second. */
#define HUNT_PERIOD 64u

/* Everything the two channels do not share. */
typedef struct {
    /* channel strip filter states */
    float   lc_z;            /* low cut */
    float   de_z1, de_z2;    /* de-esser band split, two poles */
    float   eq_low, eq_mid_hi, eq_mid_lo, eq_air;
    float   dc_x, dc_y;      /* DC blocker after the drive stage */

    /* effects */
    Ring    shortline;       /* doubler and modulation taps */
    Ring    pitchline;       /* what the shifter reads at another rate */
    float   choir_lp[MAX_VOICES];   /* each voice its own throat */
    float   choir_hp[MAX_VOICES];
    Ring    delay;
    float   dly_lp, dly_hp;  /* tone shaping inside the feedback path */
    SVF     notch[N_NOTCH];  /* the same four frequencies on every channel */
    Comb    comb[N_COMB];
    Allpass allpass[N_ALLPASS];
} Chan;

/* Smoothed values. Anything a hand can turn is walked to its new value
   across one block instead of jumping to it, because a gain step is a
   click. They are indexed rather than named one by one so the walk is a
   single loop rather than a dozen lines that have to stay in step. */
typedef enum {
    SM_IN = 0, SM_OUT, SM_BODY, SM_PRESENCE, SM_AIR,
    SM_DRIVE_PRE, SM_DRIVE_POST, SM_DRIVE_MIX, SM_PITCH,
    SM_DOUBLER, SM_MOD, SM_DELAY, SM_REVERB,
    /* These four used to be read once a block and applied whole. Each of
       them steps the sound when it moves: the makeup gain by up to 8 dB
       when COMP turns, the detune and the entries when SPREAD does - and
       a program change turns all of them at once, which is a bang. */
    SM_MAKEUP, SM_COMP_THR, SM_COMP_SLOPE, SM_SPREAD, SM_MOD_DEPTH,
    SM_VOICE_GAIN,
    SM_COUNT
} SmoothIndex;

/* Screen slots: the controls this plugin has something to SAY about when
   they are addressed to a knob or a footswitch. Each keeps its own cache;
   sharing one between the FX toggle and the FX trigger would mean the
   second write is skipped because the first already matched.

   The eight per-effect switches all say the same kind of thing, so they
   share one branch and differ only by their label. */
typedef enum {
    SLOT_FX = 0, SLOT_FX_2, SLOT_TAP, SLOT_DELAY,
    SLOT_COMP, SLOT_GATE, SLOT_OUT, SLOT_PROGRAM, SLOT_VOICES,
    SLOT_PITCH, SLOT_SAVE, SLOT_SPREAD, SLOT_HOWL, SLOT_USER,
    SLOT_SWITCH,                      /* the first of SW_COUNT switch slots */
    SLOT_COUNT = SLOT_SWITCH + SW_COUNT
} ScreenSlot;

static uint8_t slot_ctl_of(int slot)
{
    static const uint8_t fixed[SLOT_SWITCH] = {
        CTL_FX, CTL_FX_2, CTL_TAP, CTL_DELAY_TIME,
        CTL_COMP, CTL_GATE, CTL_OUTPUT, CTL_PROGRAM, CTL_VOICES,
        CTL_PITCH, CTL_SAVE, CTL_SPREAD, CTL_FEEDBACK, CTL_USER_SLOT
    };
    return (slot < SLOT_SWITCH) ? fixed[slot] : switch_ctl[slot - SLOT_SWITCH];
}

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

#define SHORT_MS      220.0f    /* doubler and modulation taps live here */
/* The shifter's window. Long enough that a low voice has a period or two
   inside it - shorter and the grain boundaries buzz - and short enough
   that the wet path does not feel late. */
#define PITCH_MS      180.0f
#define PITCH_WIN_MS   55.0f
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
    /* One gain per doubled voice, so a voice that VOICES has just added
       fades in over a block instead of arriving at full level. */
    float vg[MAX_VOICES];

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
    float fx_gain;            /* ramped, so the send does not click */
    float sw[SW_COUNT];       /* one ramp per effect switch, same reason */

    /* Which program is in force, and the switch positions that go with it.
       A program ADOPTS the switches when it is selected and then lets go:
       the port wins again as soon as it MOVES, because a footswitch that
       stops working when a program is chosen is a broken footswitch. */
    int program;
    int sw_state[SW_COUNT];   /* what is really in force */
    int sw_prev[SW_COUNT];    /* the port position we last saw */

    /* A program is a starting point, not a cage. Every control it owns is
       watched: the moment the PORT moves, that one control goes back to
       the player and stays theirs until another program is chosen. Same
       idea as the switches above and as the tapped tempo - follow the
       CHANGE, never the value, because the plugin cannot write a knob back
       to where its own program put it. */
    float   ctl_seen[CTL_COUNT];
    uint8_t ctl_mine[CTL_COUNT];

    /* --- tap tempo --- */
    int      tap_prev;
    uint32_t tap_count;       /* samples since the last tap */
    int      tap_active;      /* the tap owns the time until the knob moves */
    float    tap_ms;
    float    knob_ms_prev;
    float    delay_ms;        /* the time in force, glided towards its target */
    float    spread_entry;    /* SPREAD as the entry times have reached it */

    /* --- the anti-Larsen hunter. It listens to the mono sum, once, and
           the notches it places are applied to every channel: a howl is a
           property of the room, not of a channel. --- */
    SVF      band[N_BAND];
    SVFCoef  band_coef[N_BAND];
    float    band_peak[N_BAND];   /* accumulated between decisions */
    float    band_env[N_BAND];    /* smoothed */
    float    band_slow[N_BAND];   /* what it has been sitting at */
    uint16_t band_steady[N_BAND]; /* decisions spent dominant AND still */
    float    total_peak;
    float    total_env;
    uint32_t hunt_left;           /* samples until the next decision */

    struct {
        int     active;
        int     band;
        int     pending;          /* a band waiting for this notch to empty */
        float   hz;               /* where it actually sits, interpolated */
        float   depth;            /* ramped, 0 .. 0.87 = -18 dB */
        float   target;
        uint32_t idle;            /* decisions since the band last howled.
                                     32 bits: at 96 kHz a repeat offender's
                                     patience passes what 16 would hold, and
                                     the notch would never be given back */
    } notch[N_NOTCH];
    uint8_t  band_strikes[N_BAND];  /* how often this one has howled before */
    SVFCoef  notch_coef[N_NOTCH];
    int      n_notch;             /* how many are in place, for the port */

    /* --- the pitch shifter: one phase, shared by both channels, or the
           image would drift apart --- */
    float pitch_phase;
    int   pitch_was_moving;   /* so crossing zero can restart the grain */

    /* --- the four slots the player fills in. Saved with the pedalboard
           through the State extension, which is the only reason this
           plugin asks the host for anything at all. --- */
    struct {
        float   value[N_PROGRAM_COL];
        uint8_t sw[SW_COUNT];
        uint8_t filled;
    } user[N_USER];
    int save_prev;
    int fx2_prev;

    LV2_URID_Map* map;
    LV2_URID      urid_slots;
    LV2_URID      urid_chunk;

    /* --- LFOs --- */
    float ph_choir[MAX_VOICES];        /* grain phase, one per voice */
    float ph_choir_drift[MAX_VOICES];  /* the slow wander */
    float ph_choir_vib[MAX_VOICES];    /* and the vibrato on top of it */
    float ph_choir_swell[MAX_VOICES];  /* and how deep that vibrato is now */
    float ph_mod;

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

/* Called once every HUNT_PERIOD samples with the peaks gathered since the
   last time. Everything here is on the control side of the plugin: no
   audio passes through it, it only decides where the notches go.

   `sensitivity` is the FEEDBACK control, 0..1. It buys two things: how
   long a band must hold still before it is believed (900 ms down to
   300 ms) and how far it must stick out of the rest of the signal. */
/* Where is it REALLY howling? The bank is a third of an octave wide, so
   the band that spotted the howl only says "somewhere in here". Its two
   neighbours say where inside: fitting a parabola through the three
   levels, in the log domain, puts the peak within a few percent, and a
   notch can then be half the width it would otherwise need - which is the
   difference between a hole you can hear and one you cannot. */
static void
hunt_place(Voice* self, int slot, int b)
{
    float hz = band_hz[b];
    if (b > 0 && b < N_BAND - 1) {
        const float g = lin_to_db(self->band_env[b - 1]);
        const float c = lin_to_db(self->band_env[b]);
        const float d = lin_to_db(self->band_env[b + 1]);
        const float bas = g - 2.0f * c + d;
        if (bas < -0.001f) {              /* a real peak, not a plateau */
            float delta = 0.5f * (g - d) / bas;
            if (delta > 0.5f)  { delta = 0.5f; }
            if (delta < -0.5f) { delta = -0.5f; }
            /* the bands are 1.2925 apart, which is 0.369 in log2 */
            hz = band_hz[b] * exp2_approx(0.369f * delta);
        }
    }

    self->notch[slot].active  = 1;
    self->notch[slot].band    = b;
    self->notch[slot].pending = -1;
    self->notch[slot].hz      = hz;
    self->notch[slot].idle    = 0u;
    self->notch[slot].target  = 0.55f;      /* -7 dB to start with */
    self->notch[slot].depth   = 0.0f;
    /* Q of 4.5, measured rather than chosen: the interpolation buys a
       slightly narrower hole than the bank's own 4, and anything past
       about 5 starts missing - the howl walks to the next peak of the
       room and a second notch has to be spent on it. */
    svf_set(&self->notch_coef[slot], hz, 4.5f, self->rate);
    for (uint32_t c = 0; c < self->n_ch; ++c) {
        self->ch[c].notch[slot].ic1 = 0.0f;
        self->ch[c].notch[slot].ic2 = 0.0f;
    }
}

static void
hunt_decide(Voice* self, float sensitivity)
{
    const float rel = 1.0f - (float)HUNT_PERIOD / (0.20f * self->rate);
    const float lent = (float)HUNT_PERIOD / (0.70f * self->rate);
    const uint16_t besoin = (uint16_t)((0.90f - 0.60f * sensitivity)
                                       * self->rate / (float)HUNT_PERIOD);
    const uint32_t patience = (uint32_t)(20.0f * self->rate / (float)HUNT_PERIOD);
    /* how far out of the whole signal a band must stand: 0.40 of the peak
       when hunting hard, 0.60 when barely */
    const float domination = 0.60f - 0.20f * sensitivity;
    const float plancher = 0.0056f;    /* -45 dBFS: below this, who cares */

    /* the broadband level first, since every band is compared to it */
    self->total_env = (self->total_peak > self->total_env)
                    ? self->total_peak
                    : self->total_env * rel;
    self->total_peak = 0.0f;

    for (int b = 0; b < N_BAND; ++b) {
        const float peak = self->band_peak[b];
        self->band_peak[b] = 0.0f;
        self->band_env[b] = (peak > self->band_env[b]) ? peak
                                                       : self->band_env[b] * rel;
        const float e = self->band_env[b];
        self->band_slow[b] += lent * (e - self->band_slow[b]);

        /* Dominant: this band is carrying most of what is coming out.
           Still: it is within a couple of decibels of what it has been
           sitting at for the last second - a held note breathes and a
           vibrato does not sit still, a howl does. */
        const int fort  = (e > plancher) && (e > self->total_env * domination);
        const float d = e - self->band_slow[b];
        const int stable = (d > -0.25f * self->band_slow[b])
                        && (d <  0.25f * self->band_slow[b]);

        /* Does it have a second harmonic? Three bands up is an octave, and
           a note played or sung has something there while a room mode
           ringing on its own does not. This is the test that keeps a held
           vibrato note out of the notches: steadiness alone cannot tell
           the two apart, because a vibrato moves the pitch and not the
           level, and inside a third of an octave the level does not move
           at all. The price is honest and worth writing down: a genuinely
           pure, harmonic-free sustained tone - a whistle, a sine pad, a
           test tone - looks exactly like feedback and will be notched. */
        /* The octave partner is three bands up. The top three have
           nothing above them, so they ask the other question instead -
           am I myself the octave of something loud below? - which is the
           same test read backwards and keeps the top of the bank from
           being the trigger-happy end of it. */
        /* Read backwards, the question has to be asked with the other
           threshold. "Is there a fifth of my energy three bands below
           me?" is true of practically any voice - there is always more
           energy low than high - so the top three bands were vetoed on
           every block and could never be notched at all, which is
           precisely where a bright PA howls. What makes a high band a
           harmonic rather than a howl is a fundamental that is LOUDER
           than it. */
        const int harmonique = (b + 3 < N_BAND)
                             ? (self->band_env[b + 3] > 0.20f * e)
                             : (self->band_env[b - 3] > e);

        if (fort && stable && !harmonique) {
            if (self->band_steady[b] < 0xFFFFu) { self->band_steady[b]++; }
        } else if (self->band_steady[b] > 1u) {
            self->band_steady[b] -= 2u;    /* doubt fades faster than it builds */
        } else {
            self->band_steady[b] = 0u;
        }

        /* is this band already notched? then this is a howl that survived
           one, and the notch goes deeper rather than a second one being
           spent on the same frequency */
        int deja = -1;
        for (int i = 0; i < N_NOTCH; ++i) {
            if (self->notch[i].active && self->notch[i].band == b) { deja = i; }
        }
        if (deja >= 0) {
            if (fort && !harmonique) {
                self->notch[deja].idle = 0u;
                if (self->band_steady[b] > besoin) {
                    self->band_steady[b] = 0u;
                    self->notch[deja].target += 0.16f;
                    if (self->notch[deja].target > 0.874f) {   /* -18 dB */
                        self->notch[deja].target = 0.874f;
                    }
                }
            }
            continue;
        }

        if (self->band_steady[b] <= besoin) {
            continue;
        }
        self->band_steady[b] = 0u;

        /* a free notch, or the one that has been doing the least */
        int choix = -1;
        uint32_t plus_vieux = 0u;
        for (int i = 0; i < N_NOTCH; ++i) {
            if (self->notch[i].pending >= 0) { continue; }   /* spoken for */
            if (!self->notch[i].active) { choix = i; break; }
            if (self->notch[i].idle >= plus_vieux) {
                plus_vieux = self->notch[i].idle;
                choix = i;
            }
        }
        if (choix < 0) { continue; }
        if (self->notch[choix].active && self->notch[choix].idle < patience / 4u) {
            continue;                      /* all four are busy and earning it */
        }

        if (self->band_strikes[b] < 250u) { self->band_strikes[b]++; }

        if (self->notch[choix].active || self->notch[choix].depth > 0.0f) {
            /* Taking a working notch away from its band means moving a
               filter that is holding 18 dB. Ask it to empty first and
               re-tune it when it has: setting the depth to zero outright
               is that same 18 dB in one sample, which is a click. */
            self->notch[choix].target  = 0.0f;
            self->notch[choix].pending = b;
            continue;
        }

        hunt_place(self, choix, b);
    }

    /* a notch that was asked to empty, and has, takes its new band now */
    for (int i = 0; i < N_NOTCH; ++i) {
        if (self->notch[i].pending >= 0 && self->notch[i].depth <= 0.0f) {
            const int b = self->notch[i].pending;
            self->notch[i].pending = -1;
            hunt_place(self, i, b);
        }
    }

    /* a notch whose band has behaved for twenty seconds is given back */
    int compte = 0;
    for (int i = 0; i < N_NOTCH; ++i) {
        if (!self->notch[i].active) { continue; }
        /* Two ways to stop earning a notch: fall below the floor, or stop
           dominating. The floor is not decoration - in silence the band
           and the broadband reference decay at the SAME rate, so their
           ratio never changes and a band that was dominant when the room
           went quiet would look dominant for ever. */
        const float e_band = self->band_env[self->notch[i].band];
        if (e_band <= plancher || e_band <= self->total_env * domination) {
            if (self->notch[i].idle < 0xFFFFFFu) { self->notch[i].idle++; }
        }
        /* A room mode that has howled once will howl again, so the
           second notch on the same band is kept two, three, four times
           as long before it is given back. */
        const uint8_t strikes = self->band_strikes[self->notch[i].band];
        const uint32_t patience_b = patience
                                  * (uint32_t)(1u + (strikes > 3u ? 3u : strikes));
        if (self->notch[i].idle > patience_b) {
            self->notch[i].target = 0.0f;
            if (self->notch[i].depth <= 0.0f) { self->notch[i].active = 0; }
        }
        if (self->notch[i].active) { ++compte; }
    }
    self->n_notch = compte;
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
    const uint32_t n_pitch = (uint32_t)(self->rate * (PITCH_MS * 0.001f)) + 8u;
    const uint32_t n_delay = (uint32_t)(self->rate * (DELAY_MAX_MS * 0.001f)) + 8u;

    size_t total = 0;
    for (uint32_t c = 0; c < self->n_ch; ++c) {
        total += n_short + n_pitch + n_delay;
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
        ch->pitchline.buf = p; ch->pitchline.len = n_pitch; p += n_pitch;
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

    for (int b = 0; b < N_BAND; ++b) {
        /* Q of 4 is a third of an octave: the same width as the spacing,
           so nothing between two bands can hide from both. */
        svf_set(&self->band_coef[b], band_hz[b], 4.0f, self->rate);
    }

    self->screen_period = (uint32_t)(self->rate / SCREEN_HZ);
    if (self->screen_period < 1u) { self->screen_period = 1u; }
    self->forget_period = (uint32_t)(self->rate / FORGET_HZ);
    if (self->forget_period < 1u) { self->forget_period = 1u; }

    /* Screen feature, optional: without it everything else still works. */
    self->hmi = NULL;
    self->map = NULL;
    if (features) {
        for (int i = 0; features[i]; ++i) {
            if (!strcmp(features[i]->URI, LV2_HMI__WidgetControl)) {
                self->hmi = (const LV2_HMI_WidgetControl*)features[i]->data;
            } else if (!strcmp(features[i]->URI, LV2_URID__map)) {
                self->map = (LV2_URID_Map*)features[i]->data;
            }
        }
    }
    /* Without a URID map there is no way to name a property, so the USER
       slots simply do not persist. Everything else still works. */
    if (self->map) {
        self->urid_slots = self->map->map(self->map->handle, VOICE_SLOTS_URI);
        self->urid_chunk = self->map->map(self->map->handle, LV2_ATOM__Chunk);
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

/* What the DSP actually gets. With a program selected it comes from the
   built-in table; with MANUAL it comes from the port. IN GAIN, OUTPUT,
   the switches and the performance controls are never in the table, so
   they are always the player's. */
static float param_read(const Voice* self, int i)
{
    if (self->program > 0 && program_col[i] >= 0 && !self->ctl_mine[i]) {
        const int   u = self->program - N_PROGRAM;
        const float* row = (u >= 0)
                         ? ((u < N_USER && self->user[u].filled)
                            ? self->user[u].value : NULL)
                         : program_value[self->program];
        if (!row) {
            return ctl_read(self, i);   /* an empty slot leaves the knobs alone */
        }
        float v = row[program_col[i]];
        if (!(v >= ctl_spec[i].min)) { v = ctl_spec[i].min; }
        if (v > ctl_spec[i].max)     { v = ctl_spec[i].max; }
        return v;
    }
    return ctl_read(self, i);
}

/* The three values below are needed in two places - activate(), which
   must land on them with no ramp at all, and run(), which walks to them
   across the block. Written once so the two cannot drift apart. */
static int voices_of(const Voice* self)
{
    int n = (int)(param_read(self, CTL_VOICES) + 0.5f);
    if (n < 2)          { n = 2; }
    if (n > MAX_VOICES) { n = MAX_VOICES; }
    return n;
}

static float voice_gain_of(const Voice* self, uint32_t n_ch)
{
    const int n = voices_of(self);
    return (n_ch == 1u) ? double_gain[n] : double_gain_st[n];
}

static float comp_slope_of(const Voice* self)
{
    const float amt = param_read(self, CTL_COMP);
    return 1.0f - 1.0f / (1.0f + amt * 0.05f);       /* 1 .. 6 : 1 */
}

static float comp_makeup_of(const Voice* self)
{
    const float thr = -param_read(self, CTL_COMP) * 0.4f;
    return comp_reduction(REF_DB - thr, comp_slope_of(self), 6.0f);
}


static void
activate(LV2_Handle instance)
{
    Voice* self = (Voice*)instance;
    if (!self) {
        return;
    }

    /* FIRST, before anything reads a parameter: which program is in force
       decides what every param_read() below returns. Working this out
       halfway down instead left the smoothed values starting from the
       knobs and sliding to the program over the first block - inaudible,
       but enough to make a program and its preset differ sample for
       sample, which is a thing the bench checks and should.

       The switch positions come from the PORTS, never from the program
       table: a pedalboard being reloaded has them saved in those ports,
       and adopting the program's here would throw them away every load. */
    self->program = (int)(ctl_read(self, CTL_PROGRAM) + 0.5f);
    if (self->program < 0)          { self->program = 0; }
    if (self->program >= N_PROGRAM + N_USER) {
        self->program = N_PROGRAM + N_USER - 1;
    }
    for (int k = 0; k < (int)SW_COUNT; ++k) {
        const int on = (ctl_read(self, switch_ctl[k]) > 0.5f) ? 1 : 0;
        self->sw_state[k] = on;
        self->sw_prev[k]  = on;
        self->sw[k]       = on ? 1.0f : 0.0f;
    }
    for (int i = 0; i < (int)CTL_COUNT; ++i) {
        self->ctl_seen[i] = ctl_read(self, i);
        self->ctl_mine[i] = 0u;
    }

    for (uint32_t c = 0; c < self->n_ch; ++c) {
        Chan* ch = &self->ch[c];

        ch->lc_z = ch->de_z1 = ch->de_z2 = 0.0f;
        ch->eq_low = ch->eq_mid_hi = ch->eq_mid_lo = ch->eq_air = 0.0f;
        ch->dc_x = ch->dc_y = 0.0f;
        ch->dly_lp = ch->dly_hp = 0.0f;
        for (int k = 0; k < MAX_VOICES; ++k) {
            ch->choir_lp[k] = 0.0f;
            ch->choir_hp[k] = 0.0f;
        }

        memset(ch->shortline.buf, 0, ch->shortline.len * sizeof(float));
        memset(ch->pitchline.buf, 0, ch->pitchline.len * sizeof(float));
        ch->pitchline.w = 0u;
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
    self->sm[SM_BODY]       = db_to_lin(param_read(self, CTL_BODY)) - 1.0f;
    self->sm[SM_PRESENCE]   = db_to_lin(param_read(self, CTL_PRESENCE)) - 1.0f;
    self->sm[SM_AIR]        = db_to_lin(param_read(self, CTL_AIR)) - 1.0f;
    self->sm[SM_DRIVE_PRE]  = drive_pre_of(param_read(self, CTL_DRIVE));
    self->sm[SM_DRIVE_POST] = drive_post_of(self->sm[SM_DRIVE_PRE]);
    self->sm[SM_DRIVE_MIX]  = param_read(self, CTL_DRIVE) * 0.01f;
    self->sm[SM_PITCH]      = param_read(self, CTL_PITCH_MIX)  * 0.01f;
    self->sm[SM_DOUBLER]    = param_read(self, CTL_DOUBLER)    * 0.01f;
    self->sm[SM_MOD]        = param_read(self, CTL_MOD)        * 0.01f;
    self->sm[SM_DELAY]      = param_read(self, CTL_DELAY_MIX)  * 0.01f;
    self->sm[SM_REVERB]     = param_read(self, CTL_REVERB_MIX) * 0.01f;
    self->sm[SM_MAKEUP]     = comp_makeup_of(self);
    self->sm[SM_COMP_THR]   = -param_read(self, CTL_COMP) * 0.4f;
    self->sm[SM_COMP_SLOPE] = comp_slope_of(self);
    self->sm[SM_SPREAD]     = param_read(self, CTL_SPREAD) * 0.01f;
    self->spread_entry      = self->sm[SM_SPREAD];
    self->sm[SM_MOD_DEPTH]  = 0.5f + 3.5f * param_read(self, CTL_MOD) * 0.01f;
    self->sm[SM_VOICE_GAIN] = voice_gain_of(self, self->n_ch);
    {
        const int n = voices_of(self);
        for (int k = 0; k < MAX_VOICES; ++k) {
            self->vg[k] = (k < n) ? 1.0f : 0.0f;
        }
    }

    for (int b = 0; b < N_BAND; ++b) {
        self->band[b].ic1 = self->band[b].ic2 = 0.0f;
        self->band_peak[b] = self->band_env[b] = self->band_slow[b] = 0.0f;
        self->band_steady[b] = 0u;
        self->band_strikes[b] = 0u;
    }
    self->total_peak = self->total_env = 0.0f;
    self->hunt_left = 1u;
    self->n_notch = 0;
    for (int i = 0; i < N_NOTCH; ++i) {
        self->notch[i].active = 0;
        self->notch[i].band = 0;
        self->notch[i].pending = -1;
        self->notch[i].hz = band_hz[0];
        self->notch[i].depth = 0.0f;
        self->notch[i].target = 0.0f;
        self->notch[i].idle = 0u;
        svf_set(&self->notch_coef[i], band_hz[0], 4.0f, self->rate);
        for (uint32_t c = 0; c < self->n_ch; ++c) {
            self->ch[c].notch[i].ic1 = self->ch[c].notch[i].ic2 = 0.0f;
        }
    }

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
    self->fx2_prev        = (ctl_read(self, CTL_FX_2) > 0.5f) ? 1 : 0;
    self->save_prev       = (ctl_read(self, CTL_SAVE) > 0.5f) ? 1 : 0;
    self->fx_gain         = self->fx_state ? 1.0f : 0.0f;

    self->tap_prev     = (ctl_read(self, CTL_TAP) > 0.5f) ? 1 : 0;
    self->tap_count    = 0u;
    self->tap_active   = 0;
    self->tap_ms       = param_read(self, CTL_DELAY_TIME);
    self->knob_ms_prev = param_read(self, CTL_DELAY_TIME);
    self->delay_ms     = param_read(self, CTL_DELAY_TIME);

    /* Start every voice at a different point of every one of its cycles.
       Voices that begin together drift together for the first few seconds
       and the stack sounds like one singer until they separate. */
    static const float depart[MAX_VOICES]      = { 0.00f, 0.37f, 0.71f, 0.13f };
    static const float depart_lent[MAX_VOICES] = { 0.11f, 0.63f, 0.29f, 0.83f };
    static const float depart_vib[MAX_VOICES]  = { 0.47f, 0.05f, 0.79f, 0.23f };
    static const float depart_swell[MAX_VOICES]= { 0.00f, 0.53f, 0.17f, 0.87f };
    for (int k = 0; k < MAX_VOICES; ++k) {
        self->ph_choir[k]       = depart[k];
        self->ph_choir_drift[k] = depart_lent[k];
        self->ph_choir_vib[k]   = depart_vib[k];
        self->ph_choir_swell[k] = depart_swell[k];
    }
    self->ph_mod       = 0.0f;
    self->pitch_phase  = 0.0f;
    self->pitch_was_moving = 0;

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
        const uint32_t idx = self->n_audio + (uint32_t)slot_ctl_of(s);
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
        case SLOT_FX_2:
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

        case SLOT_PITCH: {
            const int st = (int)(param_read(self, CTL_PITCH)
                                 + (param_read(self, CTL_PITCH) < 0.0f ? -0.5f : 0.5f));
            label = "PITCH";
            write_int(vbuf, sizeof(vbuf), st);
            value = vbuf;
            unit  = "SEMI";
            bar   = ((float)st + 12.0f) * (1.0f / 24.0f);
            bar_h = (int)(bar * 100.0f + 0.5f);
            break;
        }

        case SLOT_SAVE: {
            /* On a footswitch, this says WHERE it would save - which is
               USER SLOT's business now, not the program's. */
            const int u = (int)(ctl_read(self, CTL_USER_SLOT) + 0.5f);
            label = "SAVE";
            copy_bounded(vbuf, sizeof(vbuf), "USER ");
            write_int(vbuf + 5, sizeof(vbuf) - 5, u);
            value = vbuf;
            break;
        }

        case SLOT_SPREAD: {
            const int pc = (int)(param_read(self, CTL_SPREAD) + 0.5f);
            label = "SPREAD";
            write_int(vbuf, sizeof(vbuf), pc);
            value = vbuf;
            unit  = "%";
            bar   = (float)pc * 0.01f;
            bar_h = (int)(bar * 100.0f + 0.5f);
            break;
        }

        case SLOT_HOWL:
            /* The useful readout is not the setting, it is how many
               notches the room has cost you. Four means the stage is
               fighting you, not the plugin. */
            label = "NO HOWL";
            write_int(vbuf, sizeof(vbuf), self->n_notch);
            value = vbuf;
            unit  = "CUTS";
            bar   = (float)self->n_notch * (1.0f / (float)N_NOTCH);
            bar_h = (int)(bar * 100.0f + 0.5f);
            led   = (self->n_notch > 0) ? LV2_HMI_LED_Colour_Yellow
                                        : LV2_HMI_LED_Colour_Green;
            break;

        case SLOT_USER: {
            const int u = (int)(ctl_read(self, CTL_USER_SLOT) + 0.5f);
            label = "SAVE TO";
            copy_bounded(vbuf, sizeof(vbuf), "USER ");
            write_int(vbuf + 5, sizeof(vbuf) - 5, u);
            value = vbuf;
            bar   = (float)(u - 1) * (1.0f / (float)(N_USER - 1));
            bar_h = (int)(bar * 100.0f + 0.5f);
            break;
        }

        case SLOT_PROGRAM:
            /* The list, on an encoder: turn it and the name changes. */
            label = "PROGRAM";
            if (self->program >= N_PROGRAM) {
                copy_bounded(vbuf, sizeof(vbuf), "USER ");
                write_int(vbuf + 5, sizeof(vbuf) - 5,
                          self->program - N_PROGRAM + 1);
                value = vbuf;
            } else {
                value = program_name[(self->program > 0) ? self->program : 0];
            }
            bar   = (float)self->program
                  * (1.0f / (float)(N_PROGRAM - 1 + N_USER));
            bar_h = (int)(bar * 100.0f + 0.5f);
            break;

        case SLOT_VOICES: {
            int n = (int)(param_read(self, CTL_VOICES) + 0.5f);
            if (n < 2)          { n = 2; }
            if (n > MAX_VOICES) { n = MAX_VOICES; }
            label = "VOICES";
            write_int(vbuf, sizeof(vbuf), n);
            value = vbuf;
            bar   = (float)(n - 2) * 0.5f;
            bar_h = (int)(bar * 100.0f + 0.5f);
            break;
        }

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

        default:
            /* one of the eight per-effect switches */
            if (s >= (int)SLOT_SWITCH && s < (int)SLOT_COUNT) {
                const int k = s - (int)SLOT_SWITCH;
                const int on = self->sw_state[k];
                label = switch_label[k];
                value = on ? "ON" : "OFF";
                led   = on ? LV2_HMI_LED_Colour_Green : LV2_HMI_LED_Colour_Off;
            }
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

    /* ---------------- the program list ----------------
       Changing programs is the only moment the switch positions are taken
       from the table. After that the ports own them again, so a foot on a
       switch always wins - and the screen shows what is in force rather
       than what the knob says, exactly as it does for the tapped tempo. */
    int prog = (int)(ctl_read(self, CTL_PROGRAM) + 0.5f);
    if (prog < 0)          { prog = 0; }
    if (prog >= N_PROGRAM + N_USER) { prog = N_PROGRAM + N_USER - 1; }
    if (prog != self->program) {
        self->program = prog;
        /* A new program starts clean: nothing is the player's yet, and
           the values it is about to install must not read as changes. */
        for (int i = 0; i < (int)CTL_COUNT; ++i) {
            self->ctl_seen[i] = ctl_read(self, i);
            self->ctl_mine[i] = 0u;
        }
        const int u = prog - N_PROGRAM;
        const uint8_t* adopt = NULL;
        if (prog > 0 && prog < N_PROGRAM) {
            adopt = program_switch[prog];
        } else if (u >= 0 && u < N_USER && self->user[u].filled) {
            adopt = self->user[u].sw;
        }
        if (adopt) {
            for (int k = 0; k < (int)SW_COUNT; ++k) {
                self->sw_state[k] = adopt[k] ? 1 : 0;
                self->sw_prev[k]  = (ctl_read(self, switch_ctl[k]) > 0.5f) ? 1 : 0;
            }
        }
    }
    /* Any control a program owns goes back to the knob the moment the knob
       moves. Compared against what was last SEEN, not against the
       program's value: those two differ from the instant a program is
       selected, which would make every control read as edited. */
    for (int i = 0; i < (int)CTL_COUNT; ++i) {
        if (program_col[i] < 0) {
            continue;
        }
        const float v = ctl_read(self, i);
        if (v != self->ctl_seen[i]) {
            self->ctl_seen[i] = v;
            self->ctl_mine[i] = 1u;
        }
    }

    /* ---------------- controls, read and clamped once ---------------- */
    const float lowcut_hz = param_read(self, CTL_LOW_CUT);
    const float gate_db   = param_read(self, CTL_GATE);
    const float comp_amt  = param_read(self, CTL_COMP);
    const float deess_amt = param_read(self, CTL_DE_ESS);
    const float drive_amt = param_read(self, CTL_DRIVE);
    const float mod_speed = param_read(self, CTL_MOD_SPEED);
    const float mod_amt   = param_read(self, CTL_MOD) * 0.01f;
    const float fb_amt    = param_read(self, CTL_DELAY_REPEATS) * 0.01f;
    const float rev_amt   = param_read(self, CTL_REVERB) * 0.01f;
    /* The anti-Larsen hunter. Off by default and free when off: nothing
       in the bank runs unless the control is up. */
    const float hunt_amt = param_read(self, CTL_FEEDBACK) * 0.01f;
    const int   hunt_on  = (hunt_amt > 0.005f) && self->sw_state[SW_FEEDBACK];
    const float notch_step = 1.0f / (0.12f * rate);   /* a notch fades in */

    /* Pitch. At zero semitones the two grains would sit still and comb
       the signal, so the whole block steps aside instead - which is also
       what makes PITCH at 0 exactly transparent. */
    const float semitones  = param_read(self, CTL_PITCH);
    const int   pitch_moves = (semitones > 0.01f || semitones < -0.01f);
    /* Crossing zero - a knob swept from -1 to +1, or a program that turns
       PITCH on - restarts the grain at the newest sample. Left where it
       was, the first sample after the step is spliced to one up to
       fifty-five milliseconds old, which is a click and then a stutter. */
    if (pitch_moves && !self->pitch_was_moving) { self->pitch_phase = 0.0f; }
    self->pitch_was_moving = pitch_moves;
    const float pitch_ratio = exp2_approx(semitones * (1.0f / 12.0f));
    const float pitch_win   = PITCH_WIN_MS * ms2n;
    const float pitch_step  = (1.0f - pitch_ratio) / pitch_win;

    const int n_voices = voices_of(self);
    const float out_db    = ctl_read(self, CTL_OUTPUT);


    for (int k = 0; k < (int)SW_COUNT; ++k) {
        const int now = (ctl_read(self, switch_ctl[k]) > 0.5f) ? 1 : 0;
        if (now != self->sw_prev[k] || self->program == 0) {
            self->sw_state[k] = now;
        }
        self->sw_prev[k] = now;
    }

    /* ---------------- SAVE ----------------
       It stores what the KNOBS say, not what is being heard: the web UI
       shows you the knobs, so what you see is what gets written. Selecting
       an empty slot leaves the knobs in charge, which makes dialling a
       sound and storing it one continuous action. */
    const int save_now = (ctl_read(self, CTL_SAVE) > 0.5f) ? 1 : 0;
    if (save_now && !self->save_prev) {
        int u = (int)(ctl_read(self, CTL_USER_SLOT) + 0.5f) - 1;
        if (u < 0)       { u = 0; }
        if (u >= N_USER) { u = N_USER - 1; }
        /* What is HEARD, not what the knobs say: with a program selected
           and three of its controls taken back by hand, those two are
           different things, and the one worth keeping is the sound. The
           slot comes from USER SLOT, a list of its own, so a built-in
           sound can be changed and stored somewhere else without the
           original being touched. */
        for (int i = 0; i < (int)CTL_COUNT; ++i) {
            if (program_col[i] >= 0) {
                self->user[u].value[program_col[i]] = param_read(self, i);
            }
        }
        for (int k = 0; k < (int)SW_COUNT; ++k) {
            self->user[u].sw[k] = (uint8_t)self->sw_state[k];
        }
        self->user[u].filled = 1u;
    }
    self->save_prev = save_now;

    /* ---------------- the FX switch: one state, two ways in ----------
       Same reasoning as fade.c. The toggle is followed by its CHANGES so
       it does not overwrite what the trigger just did, and the trigger by
       its RISING EDGES so a momentary footswitch counts once. */
    const int toggle_now = (ctl_read(self, CTL_FX) > 0.5f) ? 1 : 0;
    if (toggle_now != self->fx_toggle_prev) {
        self->fx_state       = toggle_now;
        self->fx_toggle_prev = toggle_now;
    }
    /* FX 2 is a second switch on the same state rather than a pulse: a
       latching footswitch sends a level, not an edge, and a trigger port
       made the second switch useless for one. Either switch moving flips
       the state; FX STATE publishes which way it really is. */
    const int fx2_now = (ctl_read(self, CTL_FX_2) > 0.5f) ? 1 : 0;
    if (fx2_now != self->fx2_prev) {
        self->fx_state = !self->fx_state;
        self->fx2_prev = fx2_now;
    }

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
    const float knob_ms = param_read(self, CTL_DELAY_TIME);
    if (knob_ms > self->knob_ms_prev + 0.5f || knob_ms < self->knob_ms_prev - 0.5f) {
        self->tap_active = 0;
    }
    self->knob_ms_prev = knob_ms;
    const float time_target = self->tap_active ? self->tap_ms : knob_ms;

    /* ---------------- coefficients ---------------- */
    /* LOW CUT at 0 means off - but a coefficient of exactly zero freezes
       the low pass and goes on subtracting whatever was in it, for ever:
       the gate and the compressor read that stuck offset and never let
       go. One hertz is inaudible and still lets the state find its way
       back to nothing. */
    const float lc_c     = onepole_coef(lowcut_hz > 1.0f ? lowcut_hz : 1.0f,
                                        rate);
    const float de_c     = onepole_coef(5500.0f, rate);
    /* The middle band is the difference of two low passes an octave and a
       half apart, centred wherever MID FREQ says. That difference peaks at
       about 0.44, so it is scaled by 1.8 to make +12 dB on the control
       mean something close to +12 dB in the air. */
    const float mid_hz   = param_read(self, CTL_MID_FREQ);
    const float eq_low_c = onepole_coef(240.0f, rate);
    const float eq_mh_c  = onepole_coef(mid_hz * 1.6f, rate);
    const float eq_ml_c  = onepole_coef(mid_hz * (1.0f / 1.6f), rate);
    const float eq_air_c = onepole_coef(6000.0f, rate);
    const float mid_gain = 1.8f;
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
       -80 dB": a threshold that low would chatter on room noise. Its own
       switch is the other way to turn it off, and the one a foot can
       reach. */
    const int      gate_on    = gate_db > ctl_spec[CTL_GATE].min + 0.5f
                                && self->sw_state[SW_GATE];
    const float    gate_open  = db_to_lin(gate_db);
    const float    gate_close = gate_open * 0.5f;         /* -6 dB hysteresis */
    const uint32_t gate_hold  = (uint32_t)(rate * 0.08f); /* 80 ms */

    /* Compressor: one knob. It opens the threshold downwards and the ratio
       upwards together, which is how a singer thinks about "more
       compression".

       The gain it gives back is MEASURED, not derived: it is exactly what
       this compressor takes off a signal sitting at the reference level,
       so a voice at -12 dBFS leaves at -12 dBFS whatever the knob says.
       Deriving it from the threshold instead - the obvious formula, and
       what this did first - handed a preset with COMP at 65 nearly 12 dB
       of makeup on top of everything else, and the whole preset came out
       shouting. */
    const int   comp_on     = comp_amt > 0.5f
                              || self->sm[SM_COMP_SLOPE] > 1.0e-4f;
    const float comp_thr    = -comp_amt * 0.4f;              /* 0 .. -40 dB */
    const float comp_ratio  = 1.0f + comp_amt * 0.05f;       /* 1 .. 6 : 1 */
    const float comp_slope  = 1.0f - 1.0f / comp_ratio;
    const float knee        = 6.0f;
    const float comp_makeup = comp_reduction(REF_DB - comp_thr, comp_slope, knee);

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
    /* SPREAD moves three things at once: how far apart the voices are
       detuned, how far their entries are staggered, and therefore how much
       they sound like separate people. At zero they are a tight double -
       still detuned, because at no detune at all four copies of one voice
       are a comb filter and nothing else. */
    const float spread_amt   = param_read(self, CTL_SPREAD) * 0.01f;
    /* The table holds the values at SPREAD 50, so the middle of the
       control is the classic micro-shift double and the top is half again
       as wide. It never reaches zero: at true unison the copies stop being
       separate voices and add coherently into one static comb - louder AND
       worse than no doubler at all, at the one setting a player will try
       first. */

    float choir_win[MAX_VOICES], choir_lp_c[MAX_VOICES], choir_hp_c[MAX_VOICES];
    for (int k = 0; k < MAX_VOICES; ++k) {
        choir_win[k]  = choir_win_ms[k] * ms2n;
        choir_lp_c[k] = onepole_coef(choir_tone[k], rate);
        choir_hp_c[k] = onepole_coef(choir_cut[k], rate);
    }
    const float fb_lp_c  = onepole_coef(3500.0f, rate);
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
    target[SM_BODY]       = db_to_lin(param_read(self, CTL_BODY))     - 1.0f;
    target[SM_PRESENCE]   = db_to_lin(param_read(self, CTL_PRESENCE)) - 1.0f;
    target[SM_AIR]        = db_to_lin(param_read(self, CTL_AIR))      - 1.0f;
    target[SM_DRIVE_PRE]  = drive_pre;
    target[SM_DRIVE_POST] = drive_post;
    target[SM_DRIVE_MIX]  = drive_amt * 0.01f;
    target[SM_PITCH]      = param_read(self, CTL_PITCH_MIX) * 0.01f;
    target[SM_DOUBLER]    = param_read(self, CTL_DOUBLER) * 0.01f;
    target[SM_MOD]        = mod_amt;
    target[SM_DELAY]      = param_read(self, CTL_DELAY_MIX)  * 0.01f;
    target[SM_REVERB]     = param_read(self, CTL_REVERB_MIX) * 0.01f;
    target[SM_MAKEUP]     = comp_makeup;
    target[SM_COMP_THR]   = comp_thr;
    target[SM_COMP_SLOPE] = comp_slope;
    target[SM_SPREAD]     = spread_amt;
    target[SM_MOD_DEPTH]  = 0.5f + 3.5f * mod_amt;
    target[SM_VOICE_GAIN] = voice_gain_of(self, n_ch);

    float sm[SM_COUNT], sm_step[SM_COUNT];
    for (int k = 0; k < (int)SM_COUNT; ++k) {
        sm[k]      = self->sm[k];
        sm_step[k] = (n_samples > 0u) ? (target[k] - sm[k]) / (float)n_samples : 0.0f;
    }

    const float fx_target = self->fx_state ? 1.0f : 0.0f;
    const float fx_step   = 1.0f / (FX_RAMP_MS * 0.001f * rate);
    const float glide     = env_coef(120.0f, rate);
    const float glide_lent = env_coef(400.0f, rate);

    float sw_target[SW_COUNT];
    for (int k = 0; k < (int)SW_COUNT; ++k) {
        sw_target[k] = self->sw_state[k] ? 1.0f : 0.0f;
    }

    /* A voice VOICES has just added fades in across the block rather than
       arriving at full level, and one it has dropped fades out - so the
       loop below still has to run every voice that is not yet silent. */
    float vg_step[MAX_VOICES];
    int   n_run = n_voices;
    for (int k = 0; k < MAX_VOICES; ++k) {
        const float cible = (k < n_voices) ? 1.0f : 0.0f;
        vg_step[k] = (n_samples > 0u)
                   ? (cible - self->vg[k]) / (float)n_samples : 0.0f;
        if (k >= n_run && (self->vg[k] > 0.0f || vg_step[k] != 0.0f)) {
            n_run = k + 1;
        }
    }

    /* The three doubled voices drift on their own slow LFO. The rates are
       mutually prime so the three never line up: a doubler whose copies
       move together is one copy with a wobble. */
    const float inc_m = mod_speed / rate;

    float gr_worst = 0.0f;
    float peak     = 0.0f;

    /* ---------------- the loop ---------------- */
    for (uint32_t i = 0; i < n_samples; ++i) {
        for (int k = 0; k < (int)SM_COUNT; ++k) {
            sm[k] += sm_step[k];
        }
        for (int k = 0; k < n_run; ++k) {
            self->vg[k] += vg_step[k];
        }

        if (self->fx_gain < fx_target) {
            self->fx_gain += fx_step;
            if (self->fx_gain > fx_target) { self->fx_gain = fx_target; }
        } else if (self->fx_gain > fx_target) {
            self->fx_gain -= fx_step;
            if (self->fx_gain < fx_target) { self->fx_gain = fx_target; }
        }
        for (int k = 0; k < (int)SW_COUNT; ++k) {
            if (self->sw[k] < sw_target[k]) {
                self->sw[k] += fx_step;
                if (self->sw[k] > sw_target[k]) { self->sw[k] = sw_target[k]; }
            } else if (self->sw[k] > sw_target[k]) {
                self->sw[k] -= fx_step;
                if (self->sw[k] < sw_target[k]) { self->sw[k] = sw_target[k]; }
            }
        }

        self->delay_ms += glide * (time_target - self->delay_ms);
        /* SPREAD moves the entries, and an entry is a delay: ramping one
           across a block moves the read point fifteen milliseconds in
           one, which is not a ramp, it is a jump at twelve times speed.
           It glides like the delay time instead, and the detune - a pitch
           offset, not a position - keeps the fast ramp. */
        self->spread_entry += glide_lent * (spread_amt - self->spread_entry);
        const float delay_n = self->delay_ms * ms2n;

        /* --- input gain and low cut --- */
        float x[MAX_CH];
        float det = 0.0f;
        for (uint32_t c = 0; c < n_ch; ++c) {
            Chan* ch = &self->ch[c];
            /* Whatever the pedalboard hands us. One NaN or infinity from
               a plugin upstream, latched into the first filter state, is
               subtracted from every sample after it for the rest of the
               session: the channel is dead until the board is reloaded.
               The comparison is written so that a NaN, which compares
               false against everything, comes out as silence. */
            float v = self->in[c] ? self->in[c][i] : 0.0f;
            if (!(v > -64.0f && v < 64.0f)) { v = 0.0f; }
            v *= sm[SM_IN];
            ch->lc_z = flush(ch->lc_z + lc_c * (v - ch->lc_z));
            v -= ch->lc_z;                 /* high pass = input minus its low pass */
            x[c] = v;
            const float a = absf(v);
            if (a > det) { det = a; }
        }

        /* --- anti-Larsen: the notches come first, and the bank listens
               to what is left. Listening AFTER them is the point: a notch
               that is working makes its own band stop shouting, which is
               how the hunter knows it can eventually let go. --- */
        if (hunt_on) {
            float mono = 0.0f;
            for (uint32_t c = 0; c < n_ch; ++c) {
                Chan* ch = &self->ch[c];
                for (int i = 0; i < N_NOTCH; ++i) {
                    if (self->notch[i].active || self->notch[i].depth > 0.0f) {
                        const float bp = svf_bp(&ch->notch[i], &self->notch_coef[i],
                                                x[c]);
                        x[c] -= self->notch[i].depth * self->notch_coef[i].k * bp;
                    }
                }
                mono += x[c];
            }
            if (n_ch > 1u) { mono *= 0.5f; }

            const float a = absf(mono);
            if (a > self->total_peak) { self->total_peak = a; }
            for (int b = 0; b < N_BAND; ++b) {
                const float bp = absf(svf_bp(&self->band[b], &self->band_coef[b],
                                             mono));
                if (bp > self->band_peak[b]) { self->band_peak[b] = bp; }
            }

            for (int i = 0; i < N_NOTCH; ++i) {
                if (self->notch[i].depth < self->notch[i].target) {
                    self->notch[i].depth += notch_step;
                    if (self->notch[i].depth > self->notch[i].target) {
                        self->notch[i].depth = self->notch[i].target;
                    }
                } else if (self->notch[i].depth > self->notch[i].target) {
                    self->notch[i].depth -= notch_step;
                    if (self->notch[i].depth < self->notch[i].target) {
                        self->notch[i].depth = self->notch[i].target;
                        if (self->notch[i].target <= 0.0f) {
                            self->notch[i].active = 0;
                        }
                    }
                }
            }

            if (--self->hunt_left == 0u) {
                self->hunt_left = HUNT_PERIOD;
                hunt_decide(self, hunt_amt);
            }
        } else if (self->n_notch != 0 || self->notch[0].depth > 0.0f) {
            /* switched off: let every notch out rather than dropping the
               tone of the room in one sample */
            int reste = 0;
            for (int i = 0; i < N_NOTCH; ++i) {
                self->notch[i].target = 0.0f;
                if (self->notch[i].depth > 0.0f) {
                    self->notch[i].depth -= notch_step;
                    if (self->notch[i].depth < 0.0f) { self->notch[i].depth = 0.0f; }
                    for (uint32_t c = 0; c < n_ch; ++c) {
                        Chan* ch = &self->ch[c];
                        const float bp = svf_bp(&ch->notch[i], &self->notch_coef[i],
                                                x[c]);
                        x[c] -= self->notch[i].depth * self->notch_coef[i].k * bp;
                    }
                    ++reste;
                } else {
                    self->notch[i].active = 0;
                }
                /* And nothing is owed to a band any more: a slot still
                   holding one would plant a notch on it the instant the
                   hunter came back, on a room it has not listened to. */
                self->notch[i].pending = -1;
            }
            self->n_notch = reste;
            for (int b = 0; b < N_BAND; ++b) { self->band_steady[b] = 0u; }
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
        /* The detector runs whether or not the compressor is working.
           Entering with a cold envelope means the full makeup gain and no
           reduction at all for the first few milliseconds, which is a
           jump UP of several decibels the moment COMP leaves zero. */
        {
            const float ce = (det > self->comp_env) ? comp_att : comp_rel;
            self->comp_env = flush(self->comp_env + ce * (det - self->comp_env));
        }
        if (comp_on) {
            const float over = lin_to_db(self->comp_env) - sm[SM_COMP_THR];
            /* The switch scales what the compressor does rather than
               branching around it, so a foot on it fades instead of
               stepping, and the detector stays warm either way. */
            const float red = comp_reduction(over, sm[SM_COMP_SLOPE], knee)
                            * self->sw[SW_COMP];
            if (red > gr_worst) { gr_worst = red; }

            const float g = db_to_lin(sm[SM_MAKEUP] * self->sw[SW_COMP] - red);
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
                const float g = db_to_lin(-deess_slope * over * self->sw[SW_DE_ESS]);
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

            x[c] = v + self->sw[SW_EQ]
                       * (sm[SM_BODY]     * ch->eq_low
                        + sm[SM_PRESENCE] * mid_gain
                                          * (ch->eq_mid_hi - ch->eq_mid_lo)
                        + sm[SM_AIR]      * (v - ch->eq_air));
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
            const float v   = x[c] + sm[SM_DRIVE_MIX] * self->sw[SW_DRIVE]
                                     * (sat - x[c]);
            const float y = v - ch->dc_x + dc_r * ch->dc_y;
            ch->dc_x = v;
            ch->dc_y = flush(y);
            x[c] = y;
        }

        /* --- pitch, in the chain rather than beside it: a baritone is
               the voice, not something added to it ---

               No detection anywhere. The signal is written to a line and
               read back at another rate, which IS a pitch shift; the read
               point walks off the end, so two of them run half a window
               apart and are crossfaded with complementary raised cosines,
               each fading to nothing exactly where it wraps. Formants move
               with the note, which is why down sounds like a bigger singer
               and up sounds like helium. */
        for (uint32_t c = 0; c < n_ch; ++c) {
            Chan* ch = &self->ch[c];
            ring_write(&ch->pitchline, x[c]);
            if (pitch_moves) {
                const float pa = self->pitch_phase;
                float pb = pa + 0.5f;
                if (pb >= 1.0f) { pb -= 1.0f; }
                /* 0.5 - 0.5*cos(2*pi*p), written with the sine we have */
                const float wa = 0.5f - 0.5f * lfo_sin(pa + 0.25f);
                const float a = ring_read(&ch->pitchline, 2.0f + pa * pitch_win);
                const float b = ring_read(&ch->pitchline, 2.0f + pb * pitch_win);
                const float shifted = a * wa + b * (1.0f - wa);
                x[c] += sm[SM_PITCH] * self->sw[SW_PITCH] * (shifted - x[c]);
            }
        }
        if (pitch_moves) {
            self->pitch_phase += pitch_step;
            /* wrapped by adding or subtracting one, never by a modulo:
               the step can be either sign and is always far below one */
            if (self->pitch_phase >= 1.0f) { self->pitch_phase -= 1.0f; }
            if (self->pitch_phase < 0.0f)  { self->pitch_phase += 1.0f; }
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
               THREE voices, twenty to forty milliseconds late, each
               drifting on its own slow LFO. The drift is what makes it a
               second and third take rather than a copy: a delay that
               moves IS a pitch difference, a few cents of it, and
               producing one that way needs no pitch detection at all.
               Three voices at mutually prime rates never line up, which
               is the difference between a chorus of singers and one
               singer through a wobble. */
        /* Each voice: where its grain is reading from, how the two halves
           of the crossfade are weighted, and how far its own detune has
           moved the grain on since the last sample. */
        float d_a[MAX_VOICES], d_b[MAX_VOICES], w_a[MAX_VOICES];
        for (int k = 0; k < n_run; ++k) {
            /* 0.55 to 1.00 of the nominal depth, so the vibrato breathes */
            const float swell = 0.775f
                              + 0.225f * lfo_sin(self->ph_choir_swell[k]);
            const float detune_scale = 0.45f + 1.10f * sm[SM_SPREAD];
            const float entry_scale  = 0.70f + 0.30f * self->spread_entry;
            const float cents = (choir_cents[k]
                               + choir_drift[k] * lfo_sin(self->ph_choir_drift[k])
                               + choir_vib[k]   * swell
                                                * lfo_sin(self->ph_choir_vib[k]))
                              * detune_scale;
            const float ratio = exp2_approx(cents * (1.0f / 1200.0f));
            float p = self->ph_choir[k] + (1.0f - ratio) / choir_win[k];
            if (p >= 1.0f) { p -= 1.0f; }
            if (p < 0.0f)  { p += 1.0f; }
            self->ph_choir[k] = p;

            float q = p + 0.5f;
            if (q >= 1.0f) { q -= 1.0f; }
            const float base = choir_ms[k] * entry_scale * ms2n;
            d_a[k] = base + p * choir_win[k];
            d_b[k] = base + q * choir_win[k];
            w_a[k] = 0.5f - 0.5f * lfo_sin(p + 0.25f);
        }
        const float depth = sm[SM_MOD_DEPTH];

        float wet[MAX_CH];
        float dly_sum = 0.0f;
        for (uint32_t c = 0; c < n_ch; ++c) {
            Chan* ch = &self->ch[c];
            ring_write(&ch->shortline, send[c]);

            float w = 0.0f;
            for (int k = 0; k < n_run; ++k) {
                float v = ring_read(&ch->shortline, d_a[k]) * w_a[k]
                        + ring_read(&ch->shortline, d_b[k]) * (1.0f - w_a[k]);
                /* each voice through its own throat: one bright, one
                   darker. Identical copies sound like an effect. */
                ch->choir_lp[k] = flush(ch->choir_lp[k]
                                      + choir_lp_c[k] * (v - ch->choir_lp[k]));
                ch->choir_hp[k] = flush(ch->choir_hp[k]
                                      + choir_hp_c[k] * (ch->choir_lp[k]
                                                       - ch->choir_hp[k]));
                v = ch->choir_lp[k] - ch->choir_hp[k];

                v *= self->vg[k];       /* 0 while this voice fades in or out */
                if (n_ch == 1u) {
                    w += v;
                } else if ((n_voices & 1) && k == n_voices - 1) {
                    w += 0.7f * v;          /* an odd count: the last one is centred */
                } else if ((uint32_t)(k & 1) == c) {
                    w += v;                 /* even voices left, odd voices right */
                }
            }
            w *= sm[SM_VOICE_GAIN];
            wet[c] = w * sm[SM_DOUBLER] * self->sw[SW_DOUBLER];

            const float mod_ph = self->ph_mod + (c ? 0.25f : 0.0f);
            const float d_mod  = (8.0f + depth * lfo_sin(mod_ph)) * ms2n;
            wet[c] += ring_read(&ch->shortline, d_mod)
                    * sm[SM_MOD] * self->sw[SW_MOD] * 0.6f;

            /* --- delay. Its switch cuts what goes IN, so the tail rings
                   out; cutting the return would chop it. --- */
            const float dly = ring_read(&ch->delay, delay_n);
            ch->dly_lp = flush(ch->dly_lp + fb_lp_c * (dly - ch->dly_lp));
            const float band = ch->dly_lp;
            ch->dly_hp = flush(ch->dly_hp + fb_hp_c * (band - ch->dly_hp));
            ring_write(&ch->delay, flush(send[c] * self->sw[SW_DELAY]
                                         + (band - ch->dly_hp) * fb_amt));

            wet[c] += dly * sm[SM_DELAY];
            dly_sum += dly;
        }
        if (n_ch > 1u) { dly_sum *= 0.5f; }

        /* --- reverb, fed by the send AND by the delay, so the repeats
               are in the room too --- */
        const float rev_in = (send_sum + dly_sum * sm[SM_DELAY])
                           * self->sw[SW_REVERB] * rev_in_g;
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

        for (int k = 0; k < n_voices; ++k) {
            self->ph_choir_drift[k] += choir_drift_hz[k] / rate;
            if (self->ph_choir_drift[k] >= 1.0f) { self->ph_choir_drift[k] -= 1.0f; }
            self->ph_choir_vib[k] += choir_vib_hz[k] / rate;
            if (self->ph_choir_vib[k] >= 1.0f) { self->ph_choir_vib[k] -= 1.0f; }
            self->ph_choir_swell[k] += choir_swell_hz[k] / rate;
            if (self->ph_choir_swell[k] >= 1.0f) { self->ph_choir_swell[k] -= 1.0f; }
        }
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
    if (self->ctl_out[CTL_NOTCHES]) {
        *self->ctl_out[CTL_NOTCHES] = (float)self->n_notch;
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

/* ------------------------------------------------------------------ */
/* The four USER slots, saved with the pedalboard                      */
/*                                                                     */
/* Written out as plain floats rather than as the struct: a struct has  */
/* padding, and padding is not something to write into somebody's       */
/* saved session. The layout is                                        */
/*     [filled, 20 values, 10 switches] x 4                            */
/* and restore() checks the size before believing any of it.           */
/* ------------------------------------------------------------------ */

#define SLOT_FLOATS  (1 + N_PROGRAM_COL + SW_COUNT)
#define STATE_FLOATS (N_USER * SLOT_FLOATS)

static LV2_State_Status
state_save(LV2_Handle instance, LV2_State_Store_Function store,
           LV2_State_Handle handle, uint32_t flags,
           const LV2_Feature* const* features)
{
    Voice* self = (Voice*)instance;
    float  buf[STATE_FLOATS];
    (void)flags; (void)features;

    if (!self || !self->map || !self->urid_slots || !self->urid_chunk) {
        return LV2_STATE_ERR_NO_FEATURE;
    }

    int n = 0;
    for (int u = 0; u < N_USER; ++u) {
        buf[n++] = self->user[u].filled ? 1.0f : 0.0f;
        for (int i = 0; i < N_PROGRAM_COL; ++i) {
            buf[n++] = self->user[u].value[i];
        }
        for (int k = 0; k < (int)SW_COUNT; ++k) {
            buf[n++] = self->user[u].sw[k] ? 1.0f : 0.0f;
        }
    }

    return store(handle, self->urid_slots, buf, sizeof(buf), self->urid_chunk,
                 LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);
}

static LV2_State_Status
state_restore(LV2_Handle instance, LV2_State_Retrieve_Function retrieve,
              LV2_State_Handle handle, uint32_t flags,
              const LV2_Feature* const* features)
{
    Voice* self = (Voice*)instance;
    (void)flags; (void)features;

    if (!self || !self->map || !self->urid_slots) {
        return LV2_STATE_ERR_NO_FEATURE;
    }

    size_t   size = 0;
    uint32_t type = 0;
    uint32_t fl   = 0;
    const void* data = retrieve(handle, self->urid_slots, &size, &type, &fl);
    if (!data || size != sizeof(float) * STATE_FLOATS) {
        return LV2_STATE_ERR_BAD_TYPE;   /* nothing saved, or not ours */
    }

    const float* buf = (const float*)data;
    int n = 0;
    for (int u = 0; u < N_USER; ++u) {
        self->user[u].filled = (buf[n++] > 0.5f) ? 1u : 0u;
        for (int i = 0; i < N_PROGRAM_COL; ++i) {
            const float v = buf[n++];
            self->user[u].value[i] = (v == v) ? v : 0.0f;   /* never a NaN */
        }
        for (int k = 0; k < (int)SW_COUNT; ++k) {
            self->user[u].sw[k] = (buf[n++] > 0.5f) ? 1u : 0u;
        }
    }
    return LV2_STATE_SUCCESS;
}

static const LV2_State_Interface state_interface = {
    state_save,
    state_restore
};

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
    if (uri && !strcmp(uri, LV2_STATE__interface)) {
        return &state_interface;
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

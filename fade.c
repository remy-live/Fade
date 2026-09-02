/* fade.lv2 — crossfade between two audio inputs, driven by a switch.
 *
 * Why it exists: switching an echo or delay off with a normal bypass cuts
 * the sound dead. Feed the dry signal to IN 1 and the delay chain to IN 2,
 * and the switch fades between them over a time you choose, so the tail
 * rings out instead of being chopped.
 *
 * Two independent fade times (1->2 and 2->1), one input gain per input,
 * two controls driving the same state (a toggle and a trigger, so one can
 * go to a footswitch and the other to MIDI), on-device screen feedback,
 * and a web UI.
 *
 * The two controls are tied together through the INTERNAL STATE, not to
 * each other: an LV2 plugin must not write into a control INPUT port,
 * those buffers belong to the host. So we watch CHANGES of the toggle and
 * RISING EDGES of the trigger, and publish the real state on an OUTPUT
 * port that both the screen and the web UI read.
 *
 * MOD Dwarf constraints honoured here:
 *   - libc only, no libm calls, no allocation inside run()
 *   - no unbounded loop, no division by a value that can be zero
 *   - no buffer written without checking it points somewhere
 *   - the HMI struct comes FROM lv2-hmi.h, never retyped from memory
 *   - caps checked before every screen write
 *   - screen rate capped, caches forgotten once per second
 *   - indicator sent AS SOON AS the control is addressed, even at zero
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
#include <string.h>   /* pulls in stddef for size_t: lv2-hmi.h does not include it */

#include "lv2-hmi.h"

#define FADE_URI        "http://remy-live.github.io/lv2/fade"
#define FADE_STEREO_URI "http://remy-live.github.io/lv2/fade#stereo"

/* Two variants share this code. The stereo one is not a convenience: with
   2 audio inputs and 1 output, mod-ui's fill_iotype() classes the mono
   plugin as kPluginIONull (stereo needs >=2 in AND >=2 out, mono needs
   exactly 1 and 1), which lands it in the same drag-and-drop bucket as CV
   plugins. 4 in / 2 out lands in kPluginIOAudioStereo, where it belongs. */
#define MAX_CH 2

/* Build stamp, readable on both ends with:
     grep -ao 'FONDU_BUILD[A-Za-z0-9_]*' fade.so
   The ARCHITECTURE is part of it on purpose: an earlier build used the
   same stamp for 32- and 64-bit binaries, so the check happily passed on
   a binary the device could not load. */
__attribute__((used))
static const volatile char build_tag[] = "FADE_BUILD4_AARCH64_20260901";

#define TIME_MIN    0.0f
#define TIME_MAX 10000.0f

/* Input gains, in decibels. */
#define GAIN_MIN   -60.0f
#define GAIN_MAX    12.0f

/* dB -> linear gain, without libm.
   powf is off limits: the binary must depend on libc alone. So we
   tabulate 10^(dB/20) from -60 to +12 dB in 0.5 dB steps and interpolate.
   The interpolation error is measured in the test bench against real
   libm. A 1 dB step was tried first and gave 0.0144 dB of error; the
   error follows the SQUARE of the step, so halving it brings that down
   to 0.0036 dB.
   At exactly GAIN_MIN we return hard silence rather than 0.001, so an
   input can be muted completely. */
static const float gain_table[145] = {
    0.001000000f, 0.001059254f, 0.001122018f, 0.001188502f, 0.001258925f, 0.001333521f,
    0.001412538f, 0.001496236f, 0.001584893f, 0.001678804f, 0.001778279f, 0.001883649f,
    0.001995262f, 0.002113489f, 0.002238721f, 0.002371374f, 0.002511886f, 0.002660725f,
    0.002818383f, 0.002985383f, 0.003162278f, 0.003349654f, 0.003548134f, 0.003758374f,
    0.003981072f, 0.004216965f, 0.004466836f, 0.004731513f, 0.005011872f, 0.005308844f,
    0.005623413f, 0.005956621f, 0.006309573f, 0.006683439f, 0.007079458f, 0.007498942f,
    0.007943282f, 0.008413951f, 0.008912509f, 0.009440609f, 0.010000000f, 0.010592537f,
    0.011220185f, 0.011885022f, 0.012589254f, 0.013335214f, 0.014125375f, 0.014962357f,
    0.015848932f, 0.016788040f, 0.017782794f, 0.018836491f, 0.019952623f, 0.021134890f,
    0.022387211f, 0.023713737f, 0.025118864f, 0.026607251f, 0.028183829f, 0.029853826f,
    0.031622777f, 0.033496544f, 0.035481339f, 0.037583740f, 0.039810717f, 0.042169650f,
    0.044668359f, 0.047315126f, 0.050118723f, 0.053088444f, 0.056234133f, 0.059566214f,
    0.063095734f, 0.066834392f, 0.070794578f, 0.074989421f, 0.079432823f, 0.084139514f,
    0.089125094f, 0.094406088f, 0.100000000f, 0.105925373f, 0.112201845f, 0.118850223f,
    0.125892541f, 0.133352143f, 0.141253754f, 0.149623566f, 0.158489319f, 0.167880402f,
    0.177827941f, 0.188364909f, 0.199526231f, 0.211348904f, 0.223872114f, 0.237137371f,
    0.251188643f, 0.266072506f, 0.281838293f, 0.298538262f, 0.316227766f, 0.334965439f,
    0.354813389f, 0.375837404f, 0.398107171f, 0.421696503f, 0.446683592f, 0.473151259f,
    0.501187234f, 0.530884444f, 0.562341325f, 0.595662144f, 0.630957344f, 0.668343918f,
    0.707945784f, 0.749894209f, 0.794328235f, 0.841395142f, 0.891250938f, 0.944060876f,
    1.000000000f, 1.059253725f, 1.122018454f, 1.188502227f, 1.258925412f, 1.333521432f,
    1.412537545f, 1.496235656f, 1.584893192f, 1.678804018f, 1.778279410f, 1.883649089f,
    1.995262315f, 2.113489040f, 2.238721139f, 2.371373706f, 2.511886432f, 2.660725060f,
    2.818382931f, 2.985382619f, 3.162277660f, 3.349654392f, 3.548133892f, 3.758374043f,
    3.981071706f,
};

static float gain_linear(float db)
{
    if (!(db >= GAIN_MIN)) {   /* also catches NaN */
        db = GAIN_MIN;
    }
    if (db > GAIN_MAX) {
        db = GAIN_MAX;
    }
    if (db <= GAIN_MIN) {
        return 0.0f;           /* hard mute */
    }

    const float x = (db - GAIN_MIN) * 2.0f; /* 0 .. 144, in 0.5 dB steps */
    int i = (int)x;                         /* lower index */
    if (i < 0) { i = 0; }
    if (i > 143) { i = 143; }               /* never read past the table */
    const float f = x - (float)i;
    return gain_table[i] + (gain_table[i + 1] - gain_table[i]) * f;
}

/* Screen rate: 25 passes per second for the WHOLE screen.
   One send per audio block would be closer to 400. */
#define SCREEN_HZ   25
/* Cache forgetting: the firmware repaints the screen when the page
   changes, so a plugin that only sends deltas leaves stale text behind. */
#define FORGET_HZ    1

/* Audio ports come first, then the controls in the same order for both
   variants. n_audio tells the two apart: 3 for mono, 6 for stereo. */
/* Control ports, numbered from the first one AFTER the audio ports.
   Mono has 3 audio ports, stereo has 6, so the absolute index of a control
   is n_audio + one of these. */
typedef enum {
    CTL_TOGGLE    = 0,
    CTL_TIME_1_2  = 1,
    CTL_TIME_2_1  = 2,
    CTL_PROGRESS  = 3,   /* input: manual crossfade position, 0..100 %.
                            Also where the screen bar gets drawn. */
    CTL_POSITION  = 4,   /* output: fade position, 0 to 1 */
    CTL_GAIN_1    = 5,   /* gain of input 1, in dB */
    CTL_GAIN_2    = 6,   /* gain of input 2, in dB */
    CTL_TRIGGER   = 7,   /* input: every rising edge starts the fade */
    CTL_STATE     = 8,   /* output: the state actually in force */
    CTL_COUNT     = 9
} ControlIndex;

/* Widest port count of the two variants: 6 audio + 9 controls. */
#define PORT_COUNT (6 + CTL_COUNT)

typedef struct {
    /* --- ports --- */
    uint32_t     n_ch;        /* 1 for mono, 2 for stereo */
    uint32_t     n_audio;     /* 3 or 6: where the control ports start */
    const float* in1[MAX_CH];
    const float* in2[MAX_CH];
    float*       out[MAX_CH];
    const float* toggle;
    const float* time_1_2;
    const float* time_2_1;
    const float* progress;
    float*       position_out;
    const float* gain_1;
    const float* gain_2;
    const float* trigger;
    float*       state_out;

    double sample_rate;
    /* Linear gains actually applied. We slide towards them instead of
       jumping, so turning an encoder does not click. */
    float  g1_smooth;
    float  g2_smooth;

    /* State in force: 0 = heading for input 1, 1 = for input 2.
       THIS drives the fade, not a port directly. */
    int    state;
    int    toggle_prev;    /* to spot a CHANGE of the toggle */
    int    trigger_prev;  /* to spot an EDGE of the trigger */

    /* Manual crossfade. Moving PROGRESS takes the fade over by hand; the
       toggle or the trigger takes it back. Without this the automatic
       ramp would drag the position straight back to the current state,
       and turning the control would appear to do nothing at all. */
    int    manual;
    float  progress_prev;
    double pos;          /* 0 = input 1, 1 = input 2.
                            Kept in double: accumulating a 1e-6 step in
                            float drifted by more than 1 % over a ten
                            second fade (measured). */

    /* --- screen --- */
    const LV2_HMI_WidgetControl* hmi;
    LV2_HMI_Addressing           addr[PORT_COUNT];
    uint32_t                     caps[PORT_COUNT];

    uint32_t screen_left;
    uint32_t screen_period;
    uint32_t forget_left;
    uint32_t forget_period;

    /* caches: we only send what changed */
    char  cache_label_enc[12];
    char  cache_value_enc[12];
    char  cache_unit_enc[8];
    int   cache_bar;          /* bar in hundredths, -1 = never sent */
    /* One cache PER footswitch: the toggle and the trigger show the same
       thing, but a shared cache would skip the second one's write. */
    char  cache_value_fs[2][12];
    int   cache_led[2];       /* -1 = never sent */

    float neutral;
} Fade;

/* ------------------------------------------------------------------ */
/* Integer to text without going through the printf family.            */
/* ------------------------------------------------------------------ */
static void write_int(char* buf, size_t size, int v)
{
    char tmp[12];
    size_t n = 0, i = 0;

    if (size == 0) {
        return;
    }
    if (v < 0) {
        v = 0;
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

/* Bounded copy, always terminated.
   Measure first, then copy. The single-loop version
   (`while (src[i] && i + 1 < size)`) is correct, but gcc at -O3 cannot
   see that the end-of-string test stops everything and reports an
   out-of-bounds read on short literals like "1>2". Measuring first makes
   the bound visible to the compiler. */
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

static LV2_Handle
instantiate(const LV2_Descriptor*     descriptor,
            double                    rate,
            const char*               bundle_path,
            const LV2_Feature* const* features)
{
    (void)bundle_path;

    Fade* self = (Fade*)calloc(1, sizeof(Fade));
    if (!self) {
        return NULL;
    }

    self->n_ch    = (descriptor && !strcmp(descriptor->URI, FADE_STEREO_URI)) ? 2u : 1u;
    self->n_audio = self->n_ch * 3u;

    self->sample_rate = (rate > 0.0) ? rate : 48000.0;
    self->pos         = 0.0;
    self->neutral      = 0.0f;

    /* CONTROL ports point at the neutral cell until the host calls
       connect_port. AUDIO ports stay null: a single float cell cannot
       serve as a block buffer, neither for reading nor writing. */
    for (uint32_t c = 0; c < MAX_CH; ++c) {
        self->in1[c] = NULL;
        self->in2[c] = NULL;
        self->out[c] = NULL;
    }
    self->position_out   = NULL;
    self->toggle  = &self->neutral;
    self->time_1_2 = &self->neutral;
    self->time_2_1 = &self->neutral;
    self->progress  = &self->neutral;
    /* The neutral cell holds 0.0f, i.e. 0 dB, which is exactly the gain
       ports' default, so an unconnected gain leaves the sound alone. */
    self->gain_1     = &self->neutral;
    self->gain_2     = &self->neutral;
    self->trigger  = &self->neutral;
    self->state_out = NULL;

    self->screen_period = (uint32_t)(self->sample_rate / SCREEN_HZ);
    if (self->screen_period < 1u) { self->screen_period = 1u; }
    self->forget_period = (uint32_t)(self->sample_rate / FORGET_HZ);
    if (self->forget_period < 1u) { self->forget_period = 1u; }
    self->screen_left = self->screen_period;
    self->forget_left = self->forget_period;

    self->cache_bar = -1;
    self->cache_led[0] = -1;
    self->cache_led[1] = -1;

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
    Fade* self = (Fade*)instance;
    if (!self) {
        return;
    }

    /* Audio first: IN 1 channels, then IN 2 channels, then the outputs.
       Stereo therefore reads in_1_l, in_1_r, in_2_l, in_2_r, out_l, out_r. */
    if (port < self->n_audio) {
        const uint32_t n = self->n_ch;
        if (port < n) {
            self->in1[port] = (const float*)data;
        } else if (port < 2u * n) {
            self->in2[port - n] = (const float*)data;
        } else {
            self->out[port - 2u * n] = (float*)data;
        }
        return;
    }

    const float* const neutral = &self->neutral;
    switch ((ControlIndex)(port - self->n_audio)) {
    case CTL_TOGGLE:   self->toggle   = data ? (const float*)data : neutral; break;
    case CTL_TIME_1_2: self->time_1_2 = data ? (const float*)data : neutral; break;
    case CTL_TIME_2_1: self->time_2_1 = data ? (const float*)data : neutral; break;
    case CTL_PROGRESS: self->progress = data ? (const float*)data : neutral; break;
    case CTL_GAIN_1:   self->gain_1   = data ? (const float*)data : neutral; break;
    case CTL_GAIN_2:   self->gain_2   = data ? (const float*)data : neutral; break;
    case CTL_TRIGGER:  self->trigger  = data ? (const float*)data : neutral; break;
    case CTL_POSITION: self->position_out = (float*)data;                    break;
    case CTL_STATE:    self->state_out    = (float*)data;                    break;
    default: break;
    }
}

static void
forget_caches(Fade* self)
{
    self->cache_label_enc[0] = '\0';
    self->cache_value_enc[0] = '\0';
    self->cache_unit_enc[0] = '\0';
    self->cache_value_fs[0][0] = '\0';
    self->cache_value_fs[1][0] = '\0';
    self->cache_bar = -1;
    self->cache_led[0] = -1;
    self->cache_led[1] = -1;
}

static void
activate(LV2_Handle instance)
{
    Fade* self = (Fade*)instance;
    if (!self) {
        return;
    }
    /* Restart on the input the switch points at, with no fade. */
    self->state           = (*self->toggle > 0.5f) ? 1 : 0;
    self->toggle_prev   = self->state;
    self->trigger_prev = (*self->trigger > 0.5f) ? 1 : 0;
    self->pos = self->state ? 1.0 : 0.0;
    self->manual        = 0;
    self->progress_prev = *self->progress;
    /* On start we take the gains as they are, with no ramp. */
    self->g1_smooth = gain_linear(*self->gain_1);
    self->g2_smooth = gain_linear(*self->gain_2);
    self->screen_left = 1u;
    self->forget_left = self->forget_period;
    forget_caches(self);
}

/* ------------------------------------------------------------------ */
/* Screen                                                              */
/* ------------------------------------------------------------------ */

static void
paint(Fade* self, int force)
{
    const LV2_HMI_WidgetControl* hmi = self->hmi;
    if (!hmi) {
        return;
    }

    const double p        = self->pos;
    const int    to_input_2  = self->state;
    const int    fading = (to_input_2 && p < 1.0) || (!to_input_2 && p > 0.0);
    const int    percent = (int)(p * 100.0 + 0.5);

    /* --- progress encoder: bar + percentage + direction --- */
    LV2_HMI_Addressing a = self->addr[self->n_audio + CTL_PROGRESS];
    if (a) {
        const uint32_t c = self->caps[self->n_audio + CTL_PROGRESS];

        if (c & LV2_HMI_AddressingCapability_Indicator) {
            /* bar compared in hundredths: no send for a change the
               screen cannot show */
            if (force || percent != self->cache_bar) {
                hmi->set_indicator(hmi->handle, a, (float)p);
                self->cache_bar = percent;
            }
        }
        if (c & LV2_HMI_AddressingCapability_Value) {
            char v[12];
            write_int(v, sizeof(v), percent);
            if (force || strcmp(v, self->cache_value_enc)) {
                hmi->set_value(hmi->handle, a, v);
                copy_bounded(self->cache_value_enc, sizeof(self->cache_value_enc), v);
            }
        }
        if (c & LV2_HMI_AddressingCapability_Unit) {
            const char* u = "%";
            if (force || strcmp(u, self->cache_unit_enc)) {
                hmi->set_unit(hmi->handle, a, u);
                copy_bounded(self->cache_unit_enc, sizeof(self->cache_unit_enc), u);
            }
        }
        if (c & LV2_HMI_AddressingCapability_Label) {
            /* Uppercase, ASCII only, 8 characters at most. */
            const char* l = !fading ? "FADE"
                          : (to_input_2  ? "TO IN 2" : "TO IN 1");
            if (force || strcmp(l, self->cache_label_enc)) {
                hmi->set_label(hmi->handle, a, l);
                copy_bounded(self->cache_label_enc, sizeof(self->cache_label_enc), l);
            }
        }
    }

    /* --- footswitches: which input is heard, and the LED ---
       The toggle and the trigger show the same thing: they are two
       handles on a single state. */
    for (int k = 0; k < 2; ++k) {
    const uint32_t idx = self->n_audio + (uint32_t)(k ? CTL_TRIGGER : CTL_TOGGLE);
    a = self->addr[idx];
    if (a) {
        const uint32_t c = self->caps[idx];

        if (c & LV2_HMI_AddressingCapability_Value) {
            const char* v = fading ? (to_input_2 ? "1>2" : "2>1")
                                     : (to_input_2 ? "2"   : "1");
            if (force || strcmp(v, self->cache_value_fs[k])) {
                hmi->set_value(hmi->handle, a, v);
                copy_bounded(self->cache_value_fs[k], sizeof(self->cache_value_fs[k]), v);
            }
        }
        if (c & LV2_HMI_AddressingCapability_LED) {
            /* No mixed colour at low brightness: the Dwarf's RGB LEDs
               turn yellow into green under Brightness_Low. */
            const int couleur = fading ? LV2_HMI_LED_Colour_Yellow
                              : (to_input_2 ? LV2_HMI_LED_Colour_Red
                                         : LV2_HMI_LED_Colour_Green);
            if (force || couleur != self->cache_led[k]) {
                hmi->set_led_with_brightness(hmi->handle, a,
                                             (LV2_HMI_LED_Colour)couleur,
                                             LV2_HMI_LED_Brightness_High);
                self->cache_led[k] = couleur;
            }
        }
    }
    }
}

static void
addressed(LV2_Handle handle, uint32_t index,
          LV2_HMI_Addressing addressing,
          const LV2_HMI_AddressingInfo* info)
{
    Fade* self = (Fade*)handle;
    if (!self || index >= (uint32_t)PORT_COUNT) {
        return;
    }

    self->addr[index] = addressing;
    self->caps[index] = info ? (uint32_t)info->caps : 0u;

    /* SEND THE BAR AS SOON AS THE CONTROL IS ADDRESSED, even at zero:
       until the plugin has sent an indicator, the firmware draws its
       DEFAULT representation of the port instead of a readout. */
    forget_caches(self);
    paint(self, 1);
}

static void
unaddressed(LV2_Handle handle, uint32_t index)
{
    Fade* self = (Fade*)handle;
    if (!self || index >= (uint32_t)PORT_COUNT) {
        return;
    }
    /* No further call must target this addressing. */
    self->addr[index] = NULL;
    self->caps[index] = 0u;
}

/* ------------------------------------------------------------------ */

static void
run(LV2_Handle instance, uint32_t n_samples)
{
    Fade* self = (Fade*)instance;
    if (!self) {
        return;
    }
    /* Every output must point somewhere before we write a whole block. */
    for (uint32_t c = 0; c < self->n_ch; ++c) {
        if (!self->out[c]) {
            return;
        }
    }

    /* Both controls act on the SAME internal state.
       - the toggle: we follow its CHANGES, not its absolute value, or it
         would overwrite on every block whatever the trigger just did;
       - the trigger: a rising edge flips the state. An edge, not a
         change, so a momentary footswitch does not count twice (press
         and release). mod-host resets a trigger port to its default
         after each cycle, so one press gives exactly one pulse. */
    const int toggle_now = (*self->toggle > 0.5f) ? 1 : 0;
    if (toggle_now != self->toggle_prev) {
        self->state = toggle_now;
        self->toggle_prev = toggle_now;
        self->manual = 0;          /* the toggle takes the fade back */
    }

    const int trigger_now = (*self->trigger > 0.5f) ? 1 : 0;
    if (trigger_now && !self->trigger_prev) {
        self->state = !self->state;
        self->manual = 0;          /* so does the trigger */
    }
    self->trigger_prev = trigger_now;

    /* PROGRESS moved? Then the player is crossfading by hand. We follow
       its CHANGES, not its value: the plugin cannot write back into an
       input port, so its value goes stale as soon as the automatic fade
       moves on, and reading it absolutely would fight the ramp. */
    float progress_now = *self->progress;
    if (!(progress_now >= 0.0f)) { progress_now = 0.0f; }   /* also NaN */
    if (progress_now > 100.0f)   { progress_now = 100.0f; }

    if (progress_now > self->progress_prev + 0.01f ||
        progress_now < self->progress_prev - 0.01f) {
        self->manual = 1;
        self->pos    = (double)progress_now * 0.01;
        /* Keep the state consistent, so the next press goes the way the
           player expects rather than back where they just came from. */
        self->state  = (self->pos >= 0.5) ? 1 : 0;
    }
    self->progress_prev = progress_now;

    /* In manual mode the target is wherever the hand left it: no ramp. */
    const double target = self->manual ? self->pos
                                       : (self->state ? 1.0 : 0.0);

    /* Two times: the way out and the way back are set separately. */
    float ms = self->state ? *self->time_1_2 : *self->time_2_1;
    if (!(ms >= TIME_MIN)) {   /* also catches NaN */
        ms = TIME_MIN;
    }
    if (ms > TIME_MAX) {
        ms = TIME_MAX;
    }

    /* Step per sample. Never a division by zero: we test first. */
    double step;
    if (ms <= 0.0f) {
        step = 1.0;   /* immediate switch */
    } else {
        double nb = (double)ms * 0.001 * self->sample_rate;
        if (nb < 1.0) {
            nb = 1.0;
        }
        step = 1.0 / nb;
    }

    /* Input gains. We slide to the requested value across the block
       instead of jumping to it: a gain step makes a click. */
    const float g1_target = gain_linear(*self->gain_1);
    const float g2_target = gain_linear(*self->gain_2);
    float g1 = self->g1_smooth;
    float g2 = self->g2_smooth;
    const float g1_step = (n_samples > 0u) ? (g1_target - g1) / (float)n_samples : 0.0f;
    const float g2_step = (n_samples > 0u) ? (g2_target - g2) / (float)n_samples : 0.0f;

    /* The fade position and the gains are shared by both channels: the
       image must not drift between left and right. So each channel walks
       the same ramp, and we only keep the values reached once. */
    double pos_end = self->pos;

    for (uint32_t c = 0; c < self->n_ch; ++c) {
        const float* const in1 = self->in1[c];
        const float* const in2 = self->in2[c];
        float* const       out = self->out[c];

        double pos = self->pos;
        float  gc1 = g1;
        float  gc2 = g2;

        for (uint32_t i = 0; i < n_samples; ++i) {
            if (pos < target) {
                pos += step;
                if (pos > target) { pos = target; }
            } else if (pos > target) {
                pos -= step;
                if (pos < target) { pos = target; }
            }
            const float g = (float)pos;

            gc1 += g1_step;
            gc2 += g2_step;

            /* Read both inputs BEFORE writing: in place, out may be the same
               buffer as in1 or in2. An unconnected input counts as silence
               and never makes us follow a null pointer. */
            const float a = (in1 ? in1[i] : 0.0f) * gc1;
            const float b = (in2 ? in2[i] : 0.0f) * gc2;
            out[i] = a * (1.0f - g) + b * g;
        }

        pos_end = pos;
    }

    const double pos = pos_end;

    self->pos = pos;
    /* Restart from the target exactly, not from the ramp's accumulation:
       otherwise rounding leaves a residual offset that never settles. */
    self->g1_smooth = g1_target;
    self->g2_smooth = g2_target;

    /* Fade position, read by the web UI. */
    if (self->position_out) {
        *self->position_out = (float)pos;
    }
    /* The real state comes out here: this is the only legitimate way to
       show it on the screen and in the web UI, since an input port must
       not be written back. */
    if (self->state_out) {
        *self->state_out = self->state ? 1.0f : 0.0f;
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

static void
deactivate(LV2_Handle instance)
{
    (void)instance;
}

static void
cleanup(LV2_Handle instance)
{
    free(instance);
}

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
    FADE_URI,
    instantiate,
    connect_port,
    activate,
    run,
    deactivate,
    cleanup,
    extension_data
};

static const LV2_Descriptor descriptor_stereo = {
    FADE_STEREO_URI,
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

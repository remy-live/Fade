/* Test bench for voice.lv2.
 *
 * Two things make this bench worth reading before the plugin.
 *
 * First, it INCLUDES voice.c rather than linking against it. Everything
 * that replaces a libm call in there is static, and the only honest way
 * to know a polynomial is good enough is to measure it against the
 * function it replaces. libm is allowed HERE: this binary never goes near
 * the device.
 *
 * Second, there is a SIMULATED SCREEN. A bench with no HMI feature never
 * runs the display code at all, and that is exactly where the bugs are:
 * strings too long, lowercase on a display that only has capitals, an
 * indicator outside [0,1], a cache that suppresses the write it was meant
 * to save.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "voice.c"

/* -std=c99 does not define PI, and the bench is not going to ask for a
   feature-test macro just for one constant. */
#define PI 3.14159265358979323846

static int echec = 0;

static void verifie(const char* quoi, double vu, double attendu, double tol)
{
    const double d = fabs(vu - attendu);
    printf("  %-46s vu=%10.3f att=%10.3f  %s\n",
           quoi, vu, attendu, (d <= tol) ? "OK" : "ECHEC");
    if (d > tol) { echec = 1; }
}

static void verifie_vrai(const char* quoi, int cond)
{
    printf("  %-46s %s\n", quoi, cond ? "OK" : "ECHEC");
    if (!cond) { echec = 1; }
}

static void verifie_entre(const char* quoi, double vu, double bas, double haut)
{
    const int cond = (vu >= bas && vu <= haut);
    printf("  %-46s vu=%10.3f dans [%.2f, %.2f]  %s\n",
           quoi, vu, bas, haut, cond ? "OK" : "ECHEC");
    if (!cond) { echec = 1; }
}

/* ---------------- simulated screen ---------------- */

#define MAXTXT 64
typedef struct {
    int   n_label, n_value, n_unit, n_indic, n_led, n_popup;
    char  dernier_popup[MAXTXT], dernier_popup_titre[MAXTXT];
    char  dernier_label[MAXTXT];
    char  dernier_value[MAXTXT];
    char  dernier_unit[MAXTXT];
    float derniere_barre;
    int   couleur;
    int   clignote_on, clignote_off;
    int   trop_long, non_ascii, minuscule;
} Ecran;

static Ecran ecran;

static void note_texte(const char* s, size_t maxi)
{
    if (strlen(s) > maxi) {
        ecran.trop_long = 1;
        printf("  *** string too long : \"%s\"\n", s);
    }
    for (size_t i = 0; s[i]; ++i) {
        const unsigned char c = (unsigned char)s[i];
        if (c > 126 || c < 32)      { ecran.non_ascii = 1; }
        if (c >= 'a' && c <= 'z')   { ecran.minuscule = 1; }
    }
}

static void f_led_blink(LV2_HMI_WidgetControl_Handle h, LV2_HMI_Addressing a,
                        LV2_HMI_LED_Colour c, int on, int off)
{
    (void)h; (void)a;
    ecran.n_led++; ecran.couleur = (int)c;
    ecran.clignote_on = on; ecran.clignote_off = off;
    if (on < 0 || on > 5000 || off < 0 || off > 5000) {
        printf("  *** blink outside 0..5000 ms : %d/%d\n", on, off);
        echec = 1;
    }
}

static void f_led_bright(LV2_HMI_WidgetControl_Handle h, LV2_HMI_Addressing a,
                         LV2_HMI_LED_Colour c, int b)
{
    (void)h; (void)a;
    ecran.n_led++; ecran.couleur = (int)c;
    ecran.clignote_on = 0; ecran.clignote_off = 0;
    if (b == LV2_HMI_LED_Brightness_Low) {
        printf("  *** LED at Brightness_Low (colours skew on this device)\n");
        echec = 1;
    }
}

static void f_label(LV2_HMI_WidgetControl_Handle h, LV2_HMI_Addressing a, const char* s)
{ (void)h; (void)a; ecran.n_label++; note_texte(s, 8);
  strncpy(ecran.dernier_label, s, MAXTXT - 1); }

static void f_value(LV2_HMI_WidgetControl_Handle h, LV2_HMI_Addressing a, const char* s)
{ (void)h; (void)a; ecran.n_value++; note_texte(s, 8);
  strncpy(ecran.dernier_value, s, MAXTXT - 1); }

static void f_unit(LV2_HMI_WidgetControl_Handle h, LV2_HMI_Addressing a, const char* s)
{ (void)h; (void)a; ecran.n_unit++; note_texte(s, 7);
  strncpy(ecran.dernier_unit, s, MAXTXT - 1); }

static void f_indic(LV2_HMI_WidgetControl_Handle h, LV2_HMI_Addressing a, const float p)
{ (void)h; (void)a; ecran.n_indic++; ecran.derniere_barre = p;
  if (!(p >= 0.0f && p <= 1.0f)) {
      printf("  *** bar outside [0,1] : %f\n", (double)p); echec = 1; } }

static void f_popup(LV2_HMI_WidgetControl_Handle h, LV2_HMI_Addressing a,
                    LV2_HMI_Popup_Style st, const char* t, const char* m)
{ (void)h; (void)a; (void)st; ecran.n_popup++;
  note_texte(t, 8); note_texte(m, 8);
  strncpy(ecran.dernier_popup_titre, t, MAXTXT - 1);
  strncpy(ecran.dernier_popup, m, MAXTXT - 1); }

static LV2_HMI_WidgetControl widget = {
    (LV2_HMI_WidgetControl_Handle)0x1,
    sizeof(LV2_HMI_WidgetControl),
    f_led_blink, f_led_bright, f_label, f_value, f_unit, f_indic, f_popup
};

static LV2_Feature feat_hmi = { LV2_HMI__WidgetControl, &widget };

/* A URID map, because the USER slots cannot be saved without one. Tiny:
   it hands out one number per string it has not seen before. */
#define MAX_URI 32
static char  uris[MAX_URI][128];
static int   n_uris = 0;

static LV2_URID f_map(LV2_URID_Map_Handle h, const char* uri)
{
    (void)h;
    for (int i = 0; i < n_uris; ++i) {
        if (!strcmp(uris[i], uri)) { return (LV2_URID)(i + 1); }
    }
    if (n_uris >= MAX_URI) { return 0; }
    strncpy(uris[n_uris], uri, sizeof(uris[0]) - 1);
    return (LV2_URID)(++n_uris);
}

static LV2_URID_Map map_feature = { NULL, f_map };
static LV2_Feature feat_map = { LV2_URID__map, &map_feature };

/* --- a host that answers the two requests a save depends on ---------
   mod-host moves a control input when the plugin asks, and runs the
   worker off the audio thread. Both are optional features, so without
   them here the plugin was never exercised the way it runs on a Dwarf -
   which is where "the saves do not work" came from. */
typedef struct Banc Banc;
static Banc* banc_courant = NULL;      /* whose ports to move; set by ouvrir */
static int   refus_requetes = 0;       /* a host that says no, for one test */
static int   n_requetes = 0;

static LV2_ControlInputPort_Change_Status
f_request(LV2_ControlInputPort_Change_Request_Handle h, uint32_t index,
          float value);

static LV2_ControlInputPort_Change_Request portreq_feature = { NULL, f_request };
static LV2_Feature feat_portreq = {
    LV2_CONTROL_INPUT_PORT_CHANGE_REQUEST_URI, &portreq_feature
};

/* The worker, run straight away rather than on another thread: the point
   here is that the work HAPPENS, not that it happens elsewhere. */
static LV2_Worker_Status
f_schedule(LV2_Worker_Schedule_Handle h, uint32_t size, const void* data);

static LV2_Worker_Schedule sched_feature = { NULL, f_schedule };
static LV2_Feature feat_sched = { LV2_WORKER__schedule, &sched_feature };

static const LV2_Feature* features[]     = { &feat_hmi, &feat_map,
                                             &feat_portreq, &feat_sched, NULL };
static const LV2_Feature* features_map[] = { &feat_map, &feat_portreq,
                                             &feat_sched, NULL };
/* and a host that offers neither, to prove the plugin still plays */
static const LV2_Feature* features_nues[] = { &feat_map, NULL };

#define TOUTES_CAPS (LV2_HMI_AddressingCapability_LED       | \
                     LV2_HMI_AddressingCapability_Label     | \
                     LV2_HMI_AddressingCapability_Value     | \
                     LV2_HMI_AddressingCapability_Unit      | \
                     LV2_HMI_AddressingCapability_Indicator)

/* ---------------- the bench ---------------- */

struct Banc {
    const LV2_Descriptor* d;
    LV2_Handle            h;
    uint32_t              n_ch, n_audio, bloc;
    float*                in[MAX_CH];
    float*                out[MAX_CH];
    float                 ctl[CTL_COUNT];
    double                sr, phase;
};

/* The two host services need a bench to point at, and the bench is
   declared below them - so they are wired up here, where both exist. */
static LV2_ControlInputPort_Change_Status
f_request(LV2_ControlInputPort_Change_Request_Handle h, uint32_t index,
          float value)
{
    (void)h;
    ++n_requetes;
    if (refus_requetes) { return LV2_CONTROL_INPUT_PORT_CHANGE_ERR_UNKNOWN; }
    if (!banc_courant) { return LV2_CONTROL_INPUT_PORT_CHANGE_ERR_UNKNOWN; }
    if (index < banc_courant->n_audio) {
        return LV2_CONTROL_INPUT_PORT_CHANGE_ERR_INVALID_INDEX;
    }
    const uint32_t i = index - banc_courant->n_audio;
    if (i >= (uint32_t)CTL_COUNT) {
        return LV2_CONTROL_INPUT_PORT_CHANGE_ERR_INVALID_INDEX;
    }
    /* mod-host writes the port itself: the plugin sees it on a later
       block, exactly as it will on the device */
    banc_courant->ctl[i] = value;
    return LV2_CONTROL_INPUT_PORT_CHANGE_SUCCESS;
}

static LV2_Worker_Status
f_schedule(LV2_Worker_Schedule_Handle h, uint32_t size, const void* data)
{
    (void)h;
    if (!banc_courant || !banc_courant->d) { return LV2_WORKER_ERR_UNKNOWN; }
    const LV2_Worker_Interface* w = (const LV2_Worker_Interface*)
        banc_courant->d->extension_data(LV2_WORKER__interface);
    if (!w || !w->work) { return LV2_WORKER_ERR_NO_SPACE; }
    return w->work(banc_courant->h, NULL, NULL, size, data);
}

/* The slots now reach the disc the moment SAVE is pressed, which is the
   whole point - and means one test's slots would be the next test's
   starting point. Every bench starts from a clean file unless it is
   testing exactly that. */
static int garder_disque = 0;

static void tourner(Banc* b);
static void silence(Banc* b);
/* Changing favourite opens a popup, and while one shows nothing else is
   sent to the screen - a label written after it erases it. Anything that
   reads a label has to let it pass first. */
static void passer_popup(Banc* b);

static void ouvrir_avec(Banc* b, int stereo, double sr, uint32_t bloc,
                        const LV2_Feature* const* feats)
{
    if (!garder_disque) { remove("voice-slots.bin"); }
    memset(b, 0, sizeof(*b));
    banc_courant = b;
    b->d       = lv2_descriptor(stereo ? 1u : 0u);
    b->h       = b->d->instantiate(b->d, sr, ".", feats);
    b->n_ch    = stereo ? 2u : 1u;
    b->n_audio = b->n_ch * 2u;
    b->bloc    = bloc;
    b->sr      = sr;

    for (uint32_t c = 0; c < b->n_ch; ++c) {
        b->in[c]  = (float*)calloc(bloc, sizeof(float));
        b->out[c] = (float*)calloc(bloc, sizeof(float));
        b->d->connect_port(b->h, c, b->in[c]);
        b->d->connect_port(b->h, b->n_ch + c, b->out[c]);
    }
    for (int i = 0; i < (int)CTL_COUNT; ++i) {
        b->ctl[i] = ctl_spec[i].def;
        b->d->connect_port(b->h, b->n_audio + (uint32_t)i, &b->ctl[i]);
    }
    b->d->activate(b->h);

    /* Two seconds of nothing, because the plugin ignores its triggers for
       that long: loading a pedalboard reinstalls the saved value of every
       port, and a restored 1 on SAVE would fire a save nobody asked for.
       Every bench therefore starts AFTER that window - except the one
       test below that exists to prove the window is there. */
    while (((Voice*)b->h)->settle_left) { silence(b); tourner(b); }
}

static void ouvrir(Banc* b, int stereo, double sr, uint32_t bloc, int avec_ecran)
{
    ouvrir_avec(b, stereo, sr, bloc, avec_ecran ? features : features_map);
}

static void fermer(Banc* b)
{
    b->d->deactivate(b->h);
    b->d->cleanup(b->h);
    for (uint32_t c = 0; c < b->n_ch; ++c) { free(b->in[c]); free(b->out[c]); }
}

/* Everything that colours the sound turned off, so a measurement measures
   one thing. The DEFAULTS are not neutral on purpose: a plugin that
   arrives with its compressor and its low cut already working is a plugin
   a singer can use without reading anything. */
static void neutre(Banc* b)
{
    b->ctl[CTL_COMP]    = 0.0f;
    b->ctl[CTL_LOW_CUT] = 0.0f;
    b->ctl[CTL_GATE]    = ctl_spec[CTL_GATE].min;
    b->ctl[CTL_REVERB_MIX] = 0.0f;
    b->ctl[CTL_DELAY_MIX]  = 0.0f;
}

static void tourner(Banc* b)
{
    b->d->run(b->h, b->bloc);
}

static void silence(Banc* b)
{
    for (uint32_t c = 0; c < b->n_ch; ++c) {
        memset(b->in[c], 0, b->bloc * sizeof(float));
    }
}

static void sinus(Banc* b, double freq, double amp)
{
    const double w = 2.0 * PI * freq / b->sr;
    for (uint32_t i = 0; i < b->bloc; ++i) {
        const float v = (float)(amp * sin(b->phase + w * (double)i));
        for (uint32_t c = 0; c < b->n_ch; ++c) { b->in[c][i] = v; }
    }
    b->phase += w * (double)b->bloc;
    while (b->phase > 2.0 * PI) { b->phase -= 2.0 * PI; }
}

static double crete(const float* x, uint32_t n)
{
    double m = 0.0;
    for (uint32_t i = 0; i < n; ++i) {
        const double a = fabs((double)x[i]);
        if (a > m) { m = a; }
    }
    return m;
}

/* Gain at one frequency, measured on power so the window does not have to
   land on a whole number of periods. */
static double gain_db(Banc* b, double freq, double amp, int chauffe, int mesure)
{
    double se = 0.0, ss = 0.0;
    for (int k = 0; k < chauffe + mesure; ++k) {
        sinus(b, freq, amp);
        tourner(b);
        if (k >= chauffe) {
            for (uint32_t i = 0; i < b->bloc; ++i) {
                se += (double)b->in[0][i] * (double)b->in[0][i];
                ss += (double)b->out[0][i] * (double)b->out[0][i];
            }
        }
    }
    if (se <= 0.0 || ss <= 0.0) { return -200.0; }
    return 10.0 * log10(ss / se);
}

/* Gain on noise. Three copies of a SINE, drifting, can cancel at whatever
   frequency the measurement happens to use; three copies of noise cannot,
   because the copies are decorrelated and their power simply adds. This
   is also how loud a preset actually is, which is not a question about
   one frequency. */
static double gain_bruit_db(Banc* b, double amp, int chauffe, int mesure)
{
    double se = 0.0, ss = 0.0;
    for (int k = 0; k < chauffe + mesure; ++k) {
        for (uint32_t i = 0; i < b->bloc; ++i) {
            const float v = (float)(amp * (2.0 * rand() / RAND_MAX - 1.0));
            for (uint32_t c = 0; c < b->n_ch; ++c) { b->in[c][i] = v; }
        }
        tourner(b);
        if (k >= chauffe) {
            for (uint32_t i = 0; i < b->bloc; ++i) {
                se += (double)b->in[0][i] * (double)b->in[0][i];
                ss += (double)b->out[0][i] * (double)b->out[0][i];
            }
        }
    }
    if (se <= 0.0 || ss <= 0.0) { return -200.0; }
    return 10.0 * log10(ss / se);
}

static int tout_fini(const Banc* b)
{
    for (uint32_t c = 0; c < b->n_ch; ++c) {
        for (uint32_t i = 0; i < b->bloc; ++i) {
            if (!isfinite(b->out[c][i]) || fabs((double)b->out[c][i]) > 1.02) {
                return 0;
            }
        }
    }
    return 1;
}

static void chauffer(Banc* b, double ms)
{
    const long n = (long)(ms * 0.001 * b->sr / (double)b->bloc) + 1;
    for (long k = 0; k < n; ++k) { silence(b); tourner(b); }
}

/* Where the loudest thing after the dry hit lands, in milliseconds. */
static double echo_ms(Banc* b, double fenetre_ms)
{
    const long total = (long)(fenetre_ms * 0.001 * b->sr);
    const long sec   = (long)(0.005 * b->sr);   /* skip the dry impulse */
    long   n = 0, pos = 0;
    double meilleur = 0.0;

    silence(b);
    for (uint32_t c = 0; c < b->n_ch; ++c) { b->in[c][0] = 1.0f; }
    while (n < total) {
        tourner(b);
        for (uint32_t i = 0; i < b->bloc; ++i, ++n) {
            if (n < sec) { continue; }
            const double a = fabs((double)b->out[0][i]);
            if (a > meilleur) { meilleur = a; pos = n; }
        }
        silence(b);
    }
    return (double)pos * 1000.0 / b->sr;
}

/* ================================================================== */
/* The approximations, against the functions they replace              */
/* ================================================================== */

static void essai_maths(void)
{
    double pire_db = 0.0;
    for (double db = -80.0; db <= 40.0; db += 0.05) {
        const double vrai = pow(10.0, db / 20.0);
        const double vu   = (double)db_to_lin((float)db);
        const double e    = fabs(20.0 * log10(vu / vrai));
        if (e > pire_db) { pire_db = e; }
    }
    verifie("dB -> gain, worst error over -80..+40 dB", pire_db, 0.0, 0.001);

    double pire_lin = 0.0;
    for (double x = 1.0e-5; x < 20.0; x *= 1.0007) {
        const double e = fabs((double)lin_to_db((float)x) - 20.0 * log10(x));
        if (e > pire_lin) { pire_lin = e; }
    }
    verifie("gain -> dB, worst error over -100..+26 dB", pire_lin, 0.0, 0.002);

    verifie("log2 of a negative is not a number but silence",
            (double)lin_to_db(-1.0f), -602.0, 1.0);
    verifie("dB of zero is silence, not an infinity",
            (double)lin_to_db(0.0f), -602.0, 1.0);
    verifie_vrai("gain of a NaN in dB is zero, not a NaN",
                 db_to_lin((float)NAN) == 0.0f);

    double pire_sin = 0.0;
    for (double p = 0.0; p < 4.0; p += 0.0005) {
        const double e = fabs((double)lfo_sin((float)p) - sin(2.0 * PI * p));
        if (e > pire_sin) { pire_sin = e; }
    }
    verifie("LFO sine, worst error against libm", pire_sin, 0.0, 0.002);

    /* The saturation must never turn round, whatever it is given: the
       rational form it comes from does, above |x| = 3. */
    int monte = 1, borne = 1;
    float precedent = softclip(-40.0f);
    for (double x = -40.0; x <= 40.0; x += 0.001) {
        const float y = softclip((float)x);
        if (y < precedent - 1.0e-6f) { monte = 0; }
        if (!(y >= -1.0f && y <= 1.0f)) { borne = 0; }
        precedent = y;
    }
    verifie_vrai("the saturation never turns back down", monte);
    verifie_vrai("the saturation stays inside [-1,1]", borne);
    verifie("the saturation is transparent for small signals",
            (double)softclip(0.001f), 0.001, 1.0e-7);

    int plafond_transparent = 1, plafond_borne = 1;
    for (double x = -0.85; x <= 0.85; x += 0.001) {
        if (ceiling((float)x) != (float)x) { plafond_transparent = 0; }
    }
    for (double x = -200.0; x <= 200.0; x += 0.01) {
        const float y = ceiling((float)x);
        if (!(y > -1.0f && y < 1.0f)) { plafond_borne = 0; }
    }
    verifie_vrai("the ceiling does nothing below 0.85", plafond_transparent);
    verifie_vrai("the ceiling holds even at 200", plafond_borne);

    /* Filter coefficients: anything a port can hold must give a stable
       filter, which for a one-pole means strictly inside (0,1). */
    int coef_ok = 1;
    for (double f = -100.0; f < 30000.0; f += 7.0) {
        const float c = onepole_coef((float)f, 48000.0f);
        if (!(c >= 0.0f && c < 1.0f)) { coef_ok = 0; }
    }
    if (!(onepole_coef((float)NAN, 48000.0f) >= 0.0f)) { coef_ok = 0; }
    verifie_vrai("every one-pole coefficient is stable", coef_ok);

    int env_ok = 1;
    for (double t = -10.0; t < 5000.0; t += 3.0) {
        const float c = env_coef((float)t, 48000.0f);
        if (!(c > 0.0f && c <= 1.0f)) { env_ok = 0; }
    }
    verifie_vrai("every follower coefficient is stable", env_ok);
}

/* ================================================================== */
/* Level                                                               */
/* ================================================================== */

static void essai_gain(void)
{
    Banc b;
    double pire = 0.0;

    for (double db = -20.0; db <= 40.0; db += 2.0) {
        ouvrir(&b, 0, 48000.0, 128, 0);
        neutre(&b);
        b.ctl[CTL_IN_GAIN] = (float)db;
        /* aim for the same output level whatever the gain, so the
           ceiling is never what is being measured */
        const double amp = 0.1 / pow(10.0, db / 20.0);
        const double vu  = gain_db(&b, 1000.0, amp, 20, 40);
        const double e   = fabs(vu - db);
        if (e > pire) { pire = e; }
        fermer(&b);
    }
    verifie("input gain follows the dial, worst error", pire, 0.0, 0.05);

    ouvrir(&b, 0, 48000.0, 128, 0);
    neutre(&b);
    verifie("with everything neutral the plugin is transparent",
            gain_db(&b, 1000.0, 0.1, 20, 40), 0.0, 0.05);
    b.ctl[CTL_OUTPUT] = -12.0f;
    verifie("output level follows the dial",
            gain_db(&b, 1000.0, 0.1, 20, 40), -12.0, 0.05);
    b.ctl[CTL_OUTPUT] = ctl_spec[CTL_OUTPUT].min;
    for (int k = 0; k < 40; ++k) { sinus(&b, 1000.0, 0.5); tourner(&b); }
    verifie("at its minimum the output is silent, not quiet",
            crete(b.out[0], b.bloc), 0.0, 1.0e-9);
    fermer(&b);
}

static void essai_silence(void)
{
    Banc b;

    ouvrir(&b, 1, 48000.0, 256, 0);
    for (int k = 0; k < 200; ++k) { silence(&b); tourner(&b); }
    verifie("silence in, silence out (defaults)",
            crete(b.out[0], b.bloc) + crete(b.out[1], b.bloc), 0.0, 1.0e-12);
    fermer(&b);

    /* Everything at once, which is also the worst case for the sum of
       four wet effects. */
    ouvrir(&b, 1, 48000.0, 256, 0);
    for (int i = 0; i < (int)CTL_COUNT; ++i) {
        if (is_output_ctl(i)) { continue; }
        b.ctl[i] = ctl_spec[i].max;
    }
    b.ctl[CTL_TAP] = 0.0f;
    for (int k = 0; k < 400; ++k) { silence(&b); tourner(&b); }
    verifie("silence in, silence out (everything at maximum)",
            crete(b.out[0], b.bloc) + crete(b.out[1], b.bloc), 0.0, 1.0e-9);
    fermer(&b);
}

static void essai_plafond(void)
{
    Banc b;
    ouvrir(&b, 0, 48000.0, 128, 0);
    neutre(&b);
    b.ctl[CTL_IN_GAIN] = 40.0f;
    for (int k = 0; k < 60; ++k) { sinus(&b, 300.0, 0.8); tourner(&b); }
    verifie_vrai("a hundredfold overload still leaves the converter alone",
                 crete(b.out[0], b.bloc) < 1.0 && tout_fini(&b));
    fermer(&b);
}

/* ================================================================== */
/* The channel strip                                                   */
/* ================================================================== */

static void essai_coupe_bas(void)
{
    Banc b;
    ouvrir(&b, 0, 48000.0, 256, 0);
    neutre(&b);

    b.ctl[CTL_LOW_CUT] = 200.0f;
    const double g50   = gain_db(&b, 50.0, 0.2, 40, 60);
    const double g1k   = gain_db(&b, 1000.0, 0.2, 40, 60);
    verifie_vrai("low cut at 200 Hz takes out 50 Hz", g50 < -8.0);
    verifie("low cut at 200 Hz leaves 1 kHz alone", g1k, 0.0, 0.4);

    b.ctl[CTL_LOW_CUT] = 0.0f;
    verifie("low cut off leaves 200 Hz alone",
            gain_db(&b, 200.0, 0.2, 40, 60), 0.0, 0.15);
    fermer(&b);

    /* Turning it off used to freeze the filter's state at whatever was in
       it and go on subtracting that for ever. The output survived - there
       is a DC blocker further down - but the gate and the compressor read
       the signal BEFORE it, so a stuck offset held the gate open on
       silence and made the compressor duck a voice that was not there. */
    ouvrir(&b, 0, 48000.0, 256, 0);
    neutre(&b);
    b.ctl[CTL_LOW_CUT] = 400.0f;      /* a corner the 50 Hz tone sits under */
    b.ctl[CTL_GATE]    = -40.0f;
    double ph = 0.0;
    const double w = 2.0 * PI * 50.0 / 48000.0;
    for (int k = 0; k < 200; ++k) {       /* charge the state */
        for (uint32_t j = 0; j < b.bloc; ++j) {
            b.in[0][j] = (float)(0.5 * sin(ph));
            ph += w;
        }
        tourner(&b);
    }
    verifie_vrai("the gate is open while the tone is there",
                 b.ctl[CTL_GATE_OPEN] > 0.5f);
    b.ctl[CTL_LOW_CUT] = 0.0f;            /* off, mid-note */
    for (int k = 0; k < 400; ++k) { silence(&b); tourner(&b); }
    verifie_vrai("and turning LOW CUT off in silence still closes it",
                 b.ctl[CTL_GATE_OPEN] < 0.5f);
    verifie("with nothing left behind it", (double)crete(b.out[0], b.bloc),
            0.0, 1.0e-6);
    fermer(&b);
}

static void essai_bandes(void)
{
    Banc b;
    ouvrir(&b, 0, 48000.0, 256, 0);
    neutre(&b);

    verifie("flat when the three bands are at zero",
            gain_db(&b, 1000.0, 0.1, 30, 60), 0.0, 0.05);

    b.ctl[CTL_BODY] = 12.0f;
    verifie_vrai("BODY lifts 100 Hz", gain_db(&b, 100.0, 0.1, 30, 60) > 8.0);
    verifie_vrai("BODY leaves 5 kHz alone",
                 fabs(gain_db(&b, 5000.0, 0.1, 30, 60)) < 1.0);
    b.ctl[CTL_BODY] = -12.0f;
    verifie_vrai("BODY cuts 100 Hz", gain_db(&b, 100.0, 0.1, 30, 60) < -5.0);
    b.ctl[CTL_BODY] = 0.0f;

    b.ctl[CTL_PRESENCE] = 12.0f;
    verifie_vrai("PRESENCE lifts 2.5 kHz",
                 gain_db(&b, 2500.0, 0.1, 30, 60) > 5.0);
    verifie_vrai("PRESENCE leaves 60 Hz alone",
                 fabs(gain_db(&b, 60.0, 0.1, 30, 60)) < 1.0);
    b.ctl[CTL_PRESENCE] = 0.0f;

    b.ctl[CTL_AIR] = 12.0f;
    verifie_vrai("AIR lifts 12 kHz", gain_db(&b, 12000.0, 0.1, 30, 60) > 8.0);
    verifie_vrai("AIR leaves 200 Hz alone",
                 fabs(gain_db(&b, 200.0, 0.1, 30, 60)) < 1.0);
    fermer(&b);
}

static void essai_porte(void)
{
    Banc b;
    ouvrir(&b, 0, 48000.0, 128, 0);
    neutre(&b);
    b.ctl[CTL_GATE] = -40.0f;

    /* -60 dB of room noise: the gate should shut on it */
    for (int k = 0; k < 800; ++k) { sinus(&b, 400.0, 0.001); tourner(&b); }
    verifie_vrai("the gate shuts on a signal below the threshold",
                 crete(b.out[0], b.bloc) < 1.0e-4);
    verifie("GATE OPEN says so", (double)b.ctl[CTL_GATE_OPEN], 0.0, 0.001);

    /* -14 dB of voice: it should open, and quickly */
    for (int k = 0; k < 20; ++k) { sinus(&b, 400.0, 0.2); tourner(&b); }
    verifie_vrai("the gate opens on a signal above the threshold",
                 crete(b.out[0], b.bloc) > 0.15);
    verifie("GATE OPEN says so too", (double)b.ctl[CTL_GATE_OPEN], 1.0, 0.001);

    b.ctl[CTL_GATE] = ctl_spec[CTL_GATE].min;      /* off */
    for (int k = 0; k < 400; ++k) { sinus(&b, 400.0, 0.001); tourner(&b); }
    verifie_vrai("at its minimum the gate is off, not shut",
                 crete(b.out[0], b.bloc) > 5.0e-4);
    fermer(&b);
}

static void essai_compresseur(void)
{
    Banc b;
    ouvrir(&b, 0, 48000.0, 128, 0);
    neutre(&b);

    b.ctl[CTL_COMP] = 0.0f;
    for (int k = 0; k < 60; ++k) { sinus(&b, 500.0, 0.5); tourner(&b); }
    verifie("no compression, no gain reduction", (double)b.ctl[CTL_GR], 0.0, 0.001);

    b.ctl[CTL_COMP] = 100.0f;
    for (int k = 0; k < 200; ++k) { sinus(&b, 500.0, 0.5); tourner(&b); }
    const double gr_fort = (double)b.ctl[CTL_GR];
    verifie_vrai("a loud signal is compressed", gr_fort < -10.0);
    verifie_vrai("GR stays inside the range it declares",
                 gr_fort >= ctl_spec[CTL_GR].min);
    const double fort = gain_db(&b, 500.0, 0.5, 20, 40);

    for (int k = 0; k < 400; ++k) { sinus(&b, 500.0, 0.01); tourner(&b); }
    const double faible = gain_db(&b, 500.0, 0.01, 40, 40);
    verifie_vrai("a quiet signal is left louder than a loud one",
                 faible > fort + 15.0);
    fermer(&b);
}

static void essai_deesseur(void)
{
    Banc b;
    double sans_s, avec_s, sans_grave, avec_grave;

    ouvrir(&b, 0, 48000.0, 256, 0);
    neutre(&b);
    b.ctl[CTL_DE_ESS] = 0.0f;
    sans_s     = gain_db(&b, 8000.0, 0.3, 40, 60);
    sans_grave = gain_db(&b, 300.0, 0.3, 40, 60);
    b.ctl[CTL_DE_ESS] = 100.0f;
    avec_s     = gain_db(&b, 8000.0, 0.3, 60, 60);
    avec_grave = gain_db(&b, 300.0, 0.3, 60, 60);
    fermer(&b);

    verifie_vrai("the de-esser holds down an 8 kHz tone",
                 avec_s < sans_s - 8.0);
    verifie("and leaves 300 Hz where it was", avec_grave, sans_grave, 0.6);
}

static void essai_saturation(void)
{
    Banc b;
    ouvrir(&b, 0, 48000.0, 256, 0);
    neutre(&b);

    verifie("DRIVE at zero is exactly unity",
            gain_db(&b, 1000.0, 0.3, 20, 40), 0.0, 0.02);

    b.ctl[CTL_DRIVE] = 100.0f;
    for (int k = 0; k < 40; ++k) { sinus(&b, 1000.0, 0.3); tourner(&b); }
    /* A saturated sine is no longer a sine: measure the harmonics by
       what is left after subtracting the best-fit fundamental. */
    double e_tot = 0.0, e_f = 0.0;
    double sc = 0.0, ss = 0.0, norme = 0.0;
    const double w = 2.0 * PI * 1000.0 / b.sr;
    sinus(&b, 1000.0, 0.3);
    tourner(&b);
    for (uint32_t i = 0; i < b.bloc; ++i) {
        const double t = b.phase - w * (double)b.bloc + w * (double)i;
        const double y = b.out[0][i];
        sc += y * cos(t); ss += y * sin(t); norme += 1.0;
        e_tot += y * y;
    }
    e_f = 2.0 * (sc * sc + ss * ss) / norme;
    verifie_vrai("DRIVE at full adds harmonics", e_tot > e_f * 1.0005);
    verifie_vrai("and does not run away", tout_fini(&b));
    fermer(&b);
}

/* ================================================================== */
/* The effects                                                         */
/* ================================================================== */

static void essai_retard(void)
{
    const double temps[] = { 100.0, 200.0, 750.0 };
    const double taux[]  = { 44100.0, 48000.0, 96000.0 };

    for (int t = 0; t < 3; ++t) {
        for (int r = 0; r < 3; ++r) {
            Banc b;
            char quoi[64];
            ouvrir(&b, 0, taux[r], 128, 0);
            neutre(&b);
            b.ctl[CTL_DELAY_TIME]    = (float)temps[t];
            b.ctl[CTL_DELAY_MIX]     = 100.0f;
            b.ctl[CTL_DELAY_REPEATS] = 0.0f;
            chauffer(&b, 2000.0);            /* let the time glide settle */
            snprintf(quoi, sizeof(quoi), "echo lands on time (%.0f ms, %.0f Hz)",
                     temps[t], taux[r]);
            verifie(quoi, echo_ms(&b, temps[t] + 400.0), temps[t], 3.0);
            fermer(&b);
        }
    }
}

static void essai_tap(void)
{
    Banc b;
    ouvrir(&b, 0, 48000.0, 128, 0);
    neutre(&b);
    b.ctl[CTL_DELAY_MIX] = 100.0f;

    const int    blocs  = 187;
    const double attend = (double)(blocs + 1) * 128.0 * 1000.0 / 48000.0;

    b.ctl[CTL_TAP] = 1.0f; tourner(&b); b.ctl[CTL_TAP] = 0.0f;
    for (int k = 0; k < blocs; ++k) { silence(&b); tourner(&b); }
    b.ctl[CTL_TAP] = 1.0f; tourner(&b); b.ctl[CTL_TAP] = 0.0f;
    verifie("two taps set the time", (double)b.ctl[CTL_TIME_OUT], attend, 3.0);

    /* and the echo really moves there */
    chauffer(&b, 2000.0);
    verifie("the delay actually plays the tapped time",
            echo_ms(&b, attend + 400.0), attend, 4.0);

    /* the knob takes it back */
    b.ctl[CTL_DELAY_TIME] = 300.0f;
    silence(&b); tourner(&b);
    verifie("moving the knob takes the time back from the tap",
            (double)b.ctl[CTL_TIME_OUT], 300.0, 0.01);

    /* a gap longer than the line is a fresh start, not a tempo */
    b.ctl[CTL_TAP] = 1.0f; tourner(&b); b.ctl[CTL_TAP] = 0.0f;
    chauffer(&b, 3000.0);
    b.ctl[CTL_TAP] = 1.0f; tourner(&b); b.ctl[CTL_TAP] = 0.0f;
    verifie("a gap longer than the delay line is ignored",
            (double)b.ctl[CTL_TIME_OUT], 300.0, 0.01);
    fermer(&b);
}

static void essai_interrupteur(void)
{
    Banc b;
    ouvrir(&b, 0, 48000.0, 128, 0);
    neutre(&b);
    b.ctl[CTL_DELAY_TIME]    = 200.0f;
    b.ctl[CTL_DELAY_MIX]     = 100.0f;
    b.ctl[CTL_DELAY_REPEATS] = 80.0f;
    chauffer(&b, 2000.0);

    for (int k = 0; k < 200; ++k) { sinus(&b, 700.0, 0.3); tourner(&b); }
    verifie("FX STATE starts where the toggle is",
            (double)b.ctl[CTL_FX_STATE], 1.0, 0.001);

    /* switch off, stop singing: the tail must NOT be chopped */
    b.ctl[CTL_FX] = 0.0f;
    silence(&b); tourner(&b);
    verifie("FX STATE follows the toggle", (double)b.ctl[CTL_FX_STATE], 0.0, 0.001);
    chauffer(&b, 300.0);
    const double juste_apres = crete(b.out[0], b.bloc);
    verifie_vrai("the delay tail rings out after FX is switched off",
                 juste_apres > 1.0e-3);
    chauffer(&b, 20000.0);
    verifie_vrai("and it does die away eventually",
                 crete(b.out[0], b.bloc) < juste_apres * 0.05);

    /* FX 2 is a second SWITCH on the same state: a latching footswitch
       sends a level, so what counts is the switch moving, either way */
    b.ctl[CTL_FX_2] = 0.0f; silence(&b); tourner(&b);
    verifie("moving FX 2 flips the state", (double)b.ctl[CTL_FX_STATE], 1.0, 0.001);
    b.ctl[CTL_FX_2] = 1.0f; silence(&b); tourner(&b);
    verifie("and moving it back flips it again",
            (double)b.ctl[CTL_FX_STATE], 0.0, 0.001);
    /* held where it is, it must not keep flipping */
    for (int k = 0; k < 20; ++k) { silence(&b); tourner(&b); }
    verifie("holding FX 2 still does not keep flipping",
            (double)b.ctl[CTL_FX_STATE], 0.0, 0.001);
    /* and the other switch still works from where it is */
    b.ctl[CTL_FX] = 1.0f; silence(&b); tourner(&b);
    verifie("FX itself still sets the state",
            (double)b.ctl[CTL_FX_STATE], 1.0, 0.001);
    fermer(&b);
}

static void essai_interrupteur_sans_clic(void)
{
    Banc b;
    ouvrir(&b, 0, 48000.0, 64, 0);
    neutre(&b);
    b.ctl[CTL_DELAY_TIME]    = 150.0f;
    b.ctl[CTL_DELAY_MIX]     = 100.0f;
    b.ctl[CTL_DELAY_REPEATS] = 0.0f;
    b.ctl[CTL_REVERB_MIX]    = 100.0f;
    chauffer(&b, 2000.0);

    /* the biggest step between two samples while nothing is changing */
    double pas_calme = 0.0, precedent = 0.0;
    for (int k = 0; k < 200; ++k) {
        sinus(&b, 500.0, 0.3);
        tourner(&b);
        for (uint32_t i = 0; i < b.bloc; ++i) {
            const double d = fabs((double)b.out[0][i] - precedent);
            if (k > 100 && d > pas_calme) { pas_calme = d; }
            precedent = b.out[0][i];
        }
    }

    /* and while the switch is being thrown */
    double pas_bascule = 0.0;
    b.ctl[CTL_FX] = 0.0f;
    for (int k = 0; k < 100; ++k) {
        sinus(&b, 500.0, 0.3);
        tourner(&b);
        for (uint32_t i = 0; i < b.bloc; ++i) {
            const double d = fabs((double)b.out[0][i] - precedent);
            if (d > pas_bascule) { pas_bascule = d; }
            precedent = b.out[0][i];
        }
    }
    verifie_vrai("switching the FX off does not click",
                 pas_bascule < pas_calme * 1.5);
    fermer(&b);
}

static void essai_reverb(void)
{
    Banc b;

    /* short tail against long tail */
    double reste[2];
    for (int k = 0; k < 2; ++k) {
        ouvrir(&b, 0, 48000.0, 128, 0);
        neutre(&b);
        b.ctl[CTL_REVERB]     = k ? 100.0f : 0.0f;
        b.ctl[CTL_REVERB_MIX] = 100.0f;
        for (int j = 0; j < 40; ++j) { sinus(&b, 600.0, 0.3); tourner(&b); }
        chauffer(&b, 1500.0);
        reste[k] = crete(b.out[0], b.bloc);
        fermer(&b);
    }
    verifie_vrai("a long tail outlasts a short one", reste[1] > reste[0] * 4.0);
    verifie_vrai("a short tail has gone after a second and a half",
                 reste[0] < 1.0e-3);

    /* and it must not run away, ever */
    ouvrir(&b, 1, 48000.0, 256, 0);
    b.ctl[CTL_REVERB]     = 100.0f;
    b.ctl[CTL_REVERB_MIX] = 100.0f;
    b.ctl[CTL_DELAY_MIX]  = 100.0f;
    b.ctl[CTL_DELAY_REPEATS] = 95.0f;
    b.ctl[CTL_IN_GAIN]    = 20.0f;
    int fini = 1;
    for (int j = 0; j < 400; ++j) {
        sinus(&b, 220.0, 0.5);
        tourner(&b);
        if (!tout_fini(&b)) { fini = 0; }
    }
    double apres_5s = 0.0;
    for (int j = 0; j < 4700; ++j) {          /* 25 seconds of silence */
        silence(&b);
        tourner(&b);
        if (!tout_fini(&b)) { fini = 0; }
        if (j == 940) { apres_5s = crete(b.out[0], b.bloc); }
    }
    verifie_vrai("reverb and delay at maximum stay finite and inside the rails",
                 fini);
    /* 95 % of feedback on a 400 ms line takes the best part of a minute to
       die: what matters is that it IS dying, not that it has finished. */
    verifie_vrai("and are decaying, not sustaining",
                 crete(b.out[0], b.bloc) < apres_5s * 0.5 && apres_5s > 0.0);
    fermer(&b);
}

/* The copies, measured by DIFFERENCE: two runs fed the same noise, one
   with the doubler and one without. Subtracting them leaves exactly what
   the doubler added, which is a question no single-frequency measurement
   can answer - three drifting copies of a sine can cancel at whatever
   frequency the bench happens to pick. */
static void essai_doubleur(void)
{
    const uint32_t N = 128;
    const int      nb = 700, saute = 200;
    float* bruit = (float*)malloc((size_t)nb * N * sizeof(float));
    float* sans  = (float*)malloc((size_t)nb * N * sizeof(float));
    float* avec  = (float*)malloc((size_t)nb * N * sizeof(float));
    float* coupe = (float*)malloc((size_t)nb * N * sizeof(float));

    srand(7);
    for (size_t i = 0; i < (size_t)nb * N; ++i) {
        bruit[i] = (float)(0.2 * (2.0 * rand() / RAND_MAX - 1.0));
    }

    for (int cas = 0; cas < 3; ++cas) {
        Banc b;
        ouvrir(&b, 0, 48000.0, N, 0);
        neutre(&b);
        b.ctl[CTL_DOUBLER]    = cas ? 100.0f : 0.0f;
        b.ctl[CTL_DOUBLER_ON] = (cas == 2) ? 0.0f : 1.0f;
        float* dst = cas == 0 ? sans : (cas == 1 ? avec : coupe);
        for (int k = 0; k < nb; ++k) {
            memcpy(b.in[0], bruit + (size_t)k * N, N * sizeof(float));
            tourner(&b);
            memcpy(dst + (size_t)k * N, b.out[0], N * sizeof(float));
        }
        fermer(&b);
    }

    double e_sec = 0.0, e_copies = 0.0, e_avec = 0.0, ecart_coupe = 0.0;
    for (size_t i = (size_t)saute * N; i < (size_t)nb * N; ++i) {
        const double d = (double)avec[i] - (double)sans[i];
        e_sec    += (double)sans[i] * (double)sans[i];
        e_avec   += (double)avec[i] * (double)avec[i];
        e_copies += d * d;
        const double c = fabs((double)coupe[i] - (double)sans[i]);
        if (c > ecart_coupe) { ecart_coupe = c; }
    }
    const double copies_db = 10.0 * log10(e_copies / e_sec);
    const double total_db  = 10.0 * log10(e_avec / e_sec);

    verifie_entre("the three copies sit just under the lead voice",
                  copies_db, -10.0, -4.0);
    verifie_entre("so the doubler is not a volume pedal", total_db, 0.3, 2.5);
    verifie("its switch takes it back out completely", ecart_coupe, 0.0, 0.0);

    free(bruit); free(sans); free(avec); free(coupe);

    /* The copies must MOVE. A doubler with fixed delays is a comb filter,
       and it is the drift that stops it being one. */
    Banc b;
    ouvrir(&b, 0, 48000.0, 128, 0);
    neutre(&b);
    b.ctl[CTL_DOUBLER] = 100.0f;
    double bas = 1.0e9, haut = 0.0;
    for (int k = 0; k < 3000; ++k) {
        sinus(&b, 800.0, 0.2);
        tourner(&b);
        if (k > 200) {
            const double c = crete(b.out[0], b.bloc);
            if (c < bas)  { bas = c; }
            if (c > haut) { haut = c; }
        }
    }
    verifie_vrai("and they drift instead of standing still", haut > bas * 1.05);
    fermer(&b);
}

/* ================================================================== */
/* Stereo                                                              */
/* ================================================================== */

static void essai_stereo(void)
{
    Banc b;
    ouvrir(&b, 1, 48000.0, 128, 0);
    verifie_vrai("the stereo variant exists and has 4 audio ports",
                 b.d != NULL && b.n_audio == 4);
    neutre(&b);
    b.ctl[CTL_COMP]     = 80.0f;
    b.ctl[CTL_DE_ESS]   = 80.0f;
    b.ctl[CTL_GATE]     = -50.0f;
    b.ctl[CTL_DRIVE]    = 50.0f;
    b.ctl[CTL_BODY]     = 6.0f;
    b.ctl[CTL_LOW_CUT]  = 120.0f;

    double ecart = 0.0;
    for (int k = 0; k < 200; ++k) {
        sinus(&b, 700.0, 0.3);
        tourner(&b);
        for (uint32_t i = 0; i < b.bloc; ++i) {
            const double d = fabs((double)b.out[0][i] - (double)b.out[1][i]);
            if (d > ecart) { ecart = d; }
        }
    }
    verifie("the channel strip does not move the image", ecart, 0.0, 0.0);

    /* the wet effects, on the other hand, are where the width comes from */
    b.ctl[CTL_DOUBLER] = 100.0f;
    b.ctl[CTL_MOD]     = 100.0f;
    ecart = 0.0;
    for (int k = 0; k < 200; ++k) {
        sinus(&b, 700.0, 0.3);
        tourner(&b);
        for (uint32_t i = 0; i < b.bloc; ++i) {
            const double d = fabs((double)b.out[0][i] - (double)b.out[1][i]);
            if (d > ecart) { ecart = d; }
        }
    }
    verifie_vrai("the doubler and the chorus do open it out", ecart > 0.01);
    fermer(&b);
}

/* ================================================================== */
/* Robustness                                                          */
/* ================================================================== */

static void essai_sur_place(void)
{
    Banc a, b;
    ouvrir(&a, 0, 48000.0, 128, 0);
    ouvrir(&b, 0, 48000.0, 128, 0);
    b.d->connect_port(b.h, 1, b.in[0]);        /* out on top of in */

    double ecart = 0.0;
    for (int k = 0; k < 100; ++k) {
        for (uint32_t i = 0; i < a.bloc; ++i) {
            const float v = (float)(0.3 * sin(0.07 * (double)(k * 128 + i)));
            a.in[0][i] = v;
            b.in[0][i] = v;
        }
        a.d->run(a.h, a.bloc);
        b.d->run(b.h, b.bloc);
        for (uint32_t i = 0; i < a.bloc; ++i) {
            const double d = fabs((double)a.out[0][i] - (double)b.in[0][i]);
            if (d > ecart) { ecart = d; }
        }
    }
    verifie("working in place gives the same samples", ecart, 0.0, 0.0);
    fermer(&a);
    free(b.out[0]);
    b.d->deactivate(b.h); b.d->cleanup(b.h); free(b.in[0]);
}

static void essai_ports_absents(void)
{
    const LV2_Descriptor* d = lv2_descriptor(0);
    LV2_Handle h = d->instantiate(d, 48000.0, ".", NULL);
    float in[64], out[64];

    /* no port connected at all: run() must notice and do nothing */
    d->activate(h);
    d->run(h, 64);
    verifie_vrai("run() with nothing connected does not crash", 1);

    /* audio connected, every control left alone: the defaults are what
       the plugin reads, and they must be the ones in the table */
    for (int i = 0; i < 64; ++i) { in[i] = 0.2f; out[i] = -1.0f; }
    d->connect_port(h, 0, in);
    d->connect_port(h, 1, out);
    d->activate(h);
    for (int k = 0; k < 200; ++k) { d->run(h, 64); }
    verifie_vrai("unconnected controls fall back on their own defaults",
                 fabs((double)out[0]) > 0.0 && isfinite(out[0]));

    /* an input that points nowhere counts as silence */
    d->connect_port(h, 0, NULL);
    d->activate(h);
    for (int k = 0; k < 400; ++k) { d->run(h, 64); }
    verifie("an unconnected input is silence, not a crash",
            crete(out, 64), 0.0, 1.0e-6);
    d->cleanup(h);
}

static void essai_valeurs_impossibles(void)
{
    Banc b;
    ouvrir(&b, 1, 48000.0, 128, 0);
    for (int i = 0; i < (int)CTL_COUNT; ++i) {
        if (is_output_ctl(i)) { continue; }
        b.ctl[i] = (float)NAN;
    }
    int fini = 1;
    for (int k = 0; k < 100; ++k) {
        sinus(&b, 500.0, 0.4);
        tourner(&b);
        if (!tout_fini(&b)) { fini = 0; }
    }
    verifie_vrai("a NaN on every control does not reach the audio", fini);

    for (int i = 0; i < (int)CTL_COUNT; ++i) {
        if (is_output_ctl(i)) { continue; }
        b.ctl[i] = (i % 2) ? (float)INFINITY : -(float)INFINITY;
    }
    for (int k = 0; k < 100; ++k) {
        sinus(&b, 500.0, 0.4);
        tourner(&b);
        if (!tout_fini(&b)) { fini = 0; }
    }
    verifie_vrai("an infinity on every control does not either", fini);
    fermer(&b);
}

static void essai_aleatoire(void)
{
    const double taux[]    = { 44100.0, 48000.0, 96000.0 };
    const uint32_t blocs[] = { 1, 7, 64, 1024 };
    int fini = 1;

    srand(1);
    for (int r = 0; r < 3; ++r) {
        for (int q = 0; q < 4; ++q) {
            Banc b;
            ouvrir(&b, (q % 2), taux[r], blocs[q], 0);
            for (int k = 0; k < 300; ++k) {
                if ((k % 20) == 0) {
                    for (int i = 0; i < (int)CTL_COUNT; ++i) {
        if (is_output_ctl(i)) { continue; }
                        const float lo = ctl_spec[i].min, hi = ctl_spec[i].max;
                        b.ctl[i] = lo + (hi - lo) * ((float)rand() / (float)RAND_MAX);
                    }
                }
                for (uint32_t c = 0; c < b.n_ch; ++c) {
                    for (uint32_t i = 0; i < b.bloc; ++i) {
                        b.in[c][i] = (float)(2.0 * rand() / RAND_MAX - 1.0) * 0.5f;
                    }
                }
                tourner(&b);
                if (!tout_fini(&b)) { fini = 0; }
            }
            /* a zero-length block is legal and must change nothing */
            b.d->run(b.h, 0);
            fermer(&b);
        }
    }
    verifie_vrai("random controls, rates and block sizes stay in the rails",
                 fini);
}

static void essai_activate_deux_fois(void)
{
    Banc b;
    ouvrir(&b, 0, 48000.0, 128, 0);
    neutre(&b);
    b.ctl[CTL_DELAY_MIX] = 100.0f;
    b.ctl[CTL_DELAY_REPEATS] = 90.0f;
    for (int k = 0; k < 200; ++k) { sinus(&b, 400.0, 0.4); tourner(&b); }
    b.d->activate(b.h);              /* the host may do this at any time */
    silence(&b); tourner(&b);
    verifie("activate() empties the lines instead of leaving a tail",
            crete(b.out[0], b.bloc), 0.0, 1.0e-9);
    fermer(&b);
}

/* ================================================================== */
/* The screen                                                          */
/* ================================================================== */

/* The preset list is grouped by family and gets new entries, so a test
   that hard-coded "program 3" broke every time the list was reordered.
   Ask for a program by the name on the screen instead. */
static int programme_nomme(const char* nom)
{
    for (int i = 1; i < N_PROGRAM; ++i) {
        if (!strcmp(program_name[i], nom)) { return i; }
    }
    return -1;
}

static const LV2_HMI_PluginNotification* notif(const LV2_Descriptor* d)
{
    return (const LV2_HMI_PluginNotification*)
           d->extension_data(LV2_HMI__PluginNotification);
}

static void adresser(Banc* b, int ctl, uint32_t caps, void* jeton)
{
    LV2_HMI_AddressingInfo info;
    memset(&info, 0, sizeof(info));
    info.caps  = (LV2_HMI_AddressingCapabilities)caps;
    info.label = "TEST";
    info.min   = ctl_spec[ctl].min;
    info.max   = ctl_spec[ctl].max;
    info.steps = 201;
    notif(b->d)->addressed(b->h, b->n_audio + (uint32_t)ctl,
                           (LV2_HMI_Addressing)jeton, &info);
}

static void essai_ecran_immediat(void)
{
    Banc b;
    memset(&ecran, 0, sizeof(ecran));
    ouvrir(&b, 0, 48000.0, 128, 1);
    neutre(&b);
    adresser(&b, CTL_OUTPUT, TOUTES_CAPS, (void*)0xA1);
    verifie_vrai("the bar is sent the moment the control is addressed",
                 ecran.n_indic > 0);
    verifie_vrai("and so is the label", ecran.n_label > 0);
    verifie_vrai("the label is the one this slot owns",
                 !strcmp(ecran.dernier_label, "OUT"));
    fermer(&b);
}

static void essai_ecran_cadence(void)
{
    Banc b;
    memset(&ecran, 0, sizeof(ecran));
    ouvrir(&b, 0, 48000.0, 64, 1);
    neutre(&b);
    adresser(&b, CTL_OUTPUT, TOUTES_CAPS, (void*)0xA1);

    const int depart = ecran.n_indic;
    for (int k = 0; k < 750; ++k) {          /* one second at 64 samples */
        sinus(&b, 300.0, 0.05 + 0.2 * fabs(sin(0.03 * k)));
        tourner(&b);
    }
    const int envois = ecran.n_indic - depart;
    verifie_entre("bar sends over one second of moving level",
                  (double)envois, 5.0, 32.0);
    verifie_vrai("no string longer than the screen can show", !ecran.trop_long);
    verifie_vrai("nothing but printable ASCII", !ecran.non_ascii);
    verifie_vrai("nothing lowercase", !ecran.minuscule);
    verifie_vrai("the meter reads out in dB", !strcmp(ecran.dernier_unit, "DB"));
    fermer(&b);
}

static void essai_ecran_tap(void)
{
    Banc b;
    memset(&ecran, 0, sizeof(ecran));
    ouvrir(&b, 0, 48000.0, 128, 1);
    neutre(&b);
    b.ctl[CTL_DELAY_TIME] = 500.0f;
    silence(&b); tourner(&b);

    adresser(&b, CTL_TAP, TOUTES_CAPS, (void*)0xB1);
    verifie_vrai("the tap switch reads out in BPM",
                 !strcmp(ecran.dernier_unit, "BPM"));
    verifie_vrai("500 ms is shown as 120 BPM",
                 !strcmp(ecran.dernier_value, "120"));
    verifie("the LED blinks the tempo back",
            (double)(ecran.clignote_on + ecran.clignote_off), 500.0, 1.0);

    b.ctl[CTL_DELAY_TIME] = 250.0f;
    for (int k = 0; k < 40; ++k) { silence(&b); tourner(&b); }
    verifie_vrai("and follows the time when it changes",
                 !strcmp(ecran.dernier_value, "240"));
    fermer(&b);
}

static void essai_ecran_interrupteur(void)
{
    Banc b;
    memset(&ecran, 0, sizeof(ecran));
    ouvrir(&b, 0, 48000.0, 128, 1);
    neutre(&b);
    adresser(&b, CTL_FX, TOUTES_CAPS, (void*)0xC1);
    verifie_vrai("the FX switch shows ON", !strcmp(ecran.dernier_value, "ON"));
    verifie("and lights green", (double)ecran.couleur,
            (double)LV2_HMI_LED_Colour_Green, 0.001);

    b.ctl[CTL_FX] = 0.0f;
    for (int k = 0; k < 40; ++k) { silence(&b); tourner(&b); }
    verifie_vrai("switched off it shows OFF", !strcmp(ecran.dernier_value, "OFF"));
    verifie("and the LED goes out", (double)ecran.couleur,
            (double)LV2_HMI_LED_Colour_Off, 0.001);
    fermer(&b);

    /* and each per-effect switch names itself on the footswitch */
    memset(&ecran, 0, sizeof(ecran));
    ouvrir(&b, 0, 48000.0, 128, 1);
    neutre(&b);
    adresser(&b, CTL_DELAY_ON, TOUTES_CAPS, (void*)0xC2);
    verifie_vrai("an effect switch shows its own name",
                 !strcmp(ecran.dernier_label, "DELAY"));
    verifie_vrai("and its state", !strcmp(ecran.dernier_value, "ON"));
    b.ctl[CTL_DELAY_ON] = 0.0f;
    for (int k = 0; k < 40; ++k) { silence(&b); tourner(&b); }
    verifie_vrai("which follows the switch", !strcmp(ecran.dernier_value, "OFF"));
    verifie("with the LED to match", (double)ecran.couleur,
            (double)LV2_HMI_LED_Colour_Off, 0.001);
    fermer(&b);
}

static void essai_ecran_compresseur(void)
{
    Banc b;
    memset(&ecran, 0, sizeof(ecran));
    ouvrir(&b, 0, 48000.0, 128, 1);
    neutre(&b);
    b.ctl[CTL_COMP] = 100.0f;
    adresser(&b, CTL_COMP, TOUTES_CAPS, (void*)0xD1);
    for (int k = 0; k < 200; ++k) { sinus(&b, 500.0, 0.5); tourner(&b); }

    verifie_vrai("the compressor knob shows the reduction it is applying",
                 ecran.dernier_value[0] == '-');
    verifie_vrai("with a bar to match", ecran.derniere_barre > 0.05f);
    verifie_vrai("under the right label", !strcmp(ecran.dernier_label, "COMP GR"));
    fermer(&b);
}

static void essai_ecran_desassignation(void)
{
    Banc b;
    memset(&ecran, 0, sizeof(ecran));
    ouvrir(&b, 0, 48000.0, 128, 1);
    neutre(&b);
    adresser(&b, CTL_OUTPUT, TOUTES_CAPS, (void*)0xA1);
    notif(b.d)->unaddressed(b.h, b.n_audio + CTL_OUTPUT);

    const int avant = ecran.n_indic + ecran.n_label + ecran.n_value;
    for (int k = 0; k < 400; ++k) { sinus(&b, 300.0, 0.3); tourner(&b); }
    verifie_vrai("nothing is sent after unaddressed()",
                 ecran.n_indic + ecran.n_label + ecran.n_value == avant);

    /* a host that addresses a port that does not exist must be ignored */
    adresser(&b, CTL_OUTPUT, TOUTES_CAPS, (void*)0xA1);
    notif(b.d)->addressed(b.h, 9999u, (LV2_HMI_Addressing)0xEE, NULL);
    notif(b.d)->unaddressed(b.h, 9999u);
    for (int k = 0; k < 40; ++k) { sinus(&b, 300.0, 0.3); tourner(&b); }
    verifie_vrai("an out-of-range index is ignored rather than written", 1);
    fermer(&b);
}

static void essai_sans_ecran(void)
{
    Banc b;
    ouvrir(&b, 0, 48000.0, 128, 0);
    neutre(&b);
    /* a host with no HMI feature may still call the notification */
    notif(b.d)->addressed(b.h, b.n_audio + CTL_OUTPUT,
                          (LV2_HMI_Addressing)0xA1, NULL);
    for (int k = 0; k < 100; ++k) { sinus(&b, 300.0, 0.3); tourner(&b); }
    verifie_vrai("no screen, no trouble", tout_fini(&b));
    fermer(&b);
}

/* ================================================================== */
/* One switch per effect                                               */
/* ================================================================== */

static void essai_interrupteurs_effets(void)
{
    Banc b;
    ouvrir(&b, 0, 48000.0, 128, 0);
    neutre(&b);

    /* --- gate --- */
    b.ctl[CTL_GATE] = -30.0f;
    for (int k = 0; k < 800; ++k) { sinus(&b, 400.0, 0.003); tourner(&b); }
    verifie_vrai("GATE ON: the gate is doing its job", crete(b.out[0], b.bloc) < 3.0e-4);
    b.ctl[CTL_GATE_ON] = 0.0f;
    for (int k = 0; k < 200; ++k) { sinus(&b, 400.0, 0.003); tourner(&b); }
    verifie("GATE OFF: the signal comes straight through",
            crete(b.out[0], b.bloc), 0.003, 3.0e-4);
    b.ctl[CTL_GATE] = ctl_spec[CTL_GATE].min;
    b.ctl[CTL_GATE_ON] = 1.0f;

    /* --- compressor --- */
    b.ctl[CTL_COMP] = 100.0f;
    for (int k = 0; k < 300; ++k) { sinus(&b, 500.0, 0.5); tourner(&b); }
    verifie_vrai("COMP ON: it is compressing", (double)b.ctl[CTL_GR] < -5.0);
    b.ctl[CTL_COMP_ON] = 0.0f;
    for (int k = 0; k < 300; ++k) { sinus(&b, 500.0, 0.5); tourner(&b); }
    verifie("COMP OFF: no reduction left", (double)b.ctl[CTL_GR], 0.0, 0.001);
    verifie("COMP OFF: and no makeup either",
            gain_db(&b, 500.0, 0.3, 60, 60), 0.0, 0.1);
    b.ctl[CTL_COMP] = 0.0f;
    b.ctl[CTL_COMP_ON] = 1.0f;

    /* --- de-esser --- */
    b.ctl[CTL_DE_ESS] = 100.0f;
    verifie_vrai("DE-ESS ON: an 8 kHz tone is held down",
                 gain_db(&b, 8000.0, 0.3, 80, 60) < -8.0);
    b.ctl[CTL_DE_ESS_ON] = 0.0f;
    verifie("DE-ESS OFF: it is not", gain_db(&b, 8000.0, 0.3, 80, 60), 0.0, 0.1);
    b.ctl[CTL_DE_ESS] = 0.0f;
    b.ctl[CTL_DE_ESS_ON] = 1.0f;

    /* --- drive. Turning it on changes the colour and NOT the level:
           the stage measures itself either side of the saturator and
           corrects the difference, which is why a preset with the drive
           at ninety does not arrive six decibels louder than the one
           before it. --- */
    b.ctl[CTL_DRIVE] = 100.0f;
    chauffer(&b, 1200.0);           /* the level match takes 400 ms */
    /* Measured on the FUNDAMENTAL, which is why a decibel is allowed:
       squaring a sine off moves energy into it - four over pi of the
       peak, for a perfect square - while the stage matches the average
       of the whole waveform. Before the level match this read +6 dB. */
    verifie("DRIVE ON: the colour changes, the level does not",
            gain_db(&b, 800.0, 0.02, 200, 60), 0.0, 1.2);
    b.ctl[CTL_DRIVE_ON] = 0.0f;
    verifie("DRIVE OFF: unity again", gain_db(&b, 800.0, 0.02, 200, 60), 0.0, 0.1);
    b.ctl[CTL_DRIVE] = 0.0f;
    b.ctl[CTL_DRIVE_ON] = 1.0f;

    /* --- chorus --- */
    srand(3);
    const double sec = gain_bruit_db(&b, 0.2, 40, 300);
    b.ctl[CTL_MOD] = 100.0f;
    chauffer(&b, 200.0);
    verifie_vrai("MOD ON: the chorus adds something",
                 gain_bruit_db(&b, 0.2, 40, 300) > sec + 0.2);
    b.ctl[CTL_MOD_ON] = 0.0f;
    chauffer(&b, 200.0);
    verifie("MOD OFF: back where it was",
            gain_bruit_db(&b, 0.2, 40, 300), sec, 0.2);
    b.ctl[CTL_MOD] = 0.0f;
    b.ctl[CTL_MOD_ON] = 1.0f;
    fermer(&b);

    /* --- delay: its switch stops the SEND, so nothing new goes in --- */
    ouvrir(&b, 0, 48000.0, 128, 0);
    neutre(&b);
    b.ctl[CTL_DELAY_TIME]    = 200.0f;
    b.ctl[CTL_DELAY_MIX]     = 100.0f;
    b.ctl[CTL_DELAY_REPEATS] = 0.0f;
    b.ctl[CTL_DELAY_ON]      = 0.0f;
    chauffer(&b, 2000.0);
    silence(&b);
    b.in[0][0] = 1.0f;
    tourner(&b);
    /* The first 100 ms belong to the dry impulse and to the DC blocker
       ringing behind it - an impulse through a 20 Hz high pass leaves a
       tail of its own, some microvolts of it, for a good while. The echo,
       if the switch let one through, would land at 200 ms at full
       level. */
    chauffer(&b, 100.0);
    double reste = 0.0;
    for (int k = 0; k < 150; ++k) {
        silence(&b);
        tourner(&b);
        const double c = crete(b.out[0], b.bloc);
        if (c > reste) { reste = c; }
    }
    verifie("DELAY OFF: nothing new goes into the line", reste, 0.0, 1.0e-5);
    b.ctl[CTL_DELAY_ON] = 1.0f;
    chauffer(&b, 100.0);
    verifie("DELAY ON: the echo is back", echo_ms(&b, 600.0), 200.0, 3.0);

    /* and switching it off lets what is already in there ring out */
    b.ctl[CTL_DELAY_REPEATS] = 80.0f;
    for (int k = 0; k < 400; ++k) { sinus(&b, 600.0, 0.3); tourner(&b); }
    b.ctl[CTL_DELAY_ON] = 0.0f;
    chauffer(&b, 400.0);
    verifie_vrai("DELAY OFF: the tail rings out rather than being chopped",
                 crete(b.out[0], b.bloc) > 1.0e-3);
    fermer(&b);

    /* --- reverb, same rule --- */
    ouvrir(&b, 0, 48000.0, 128, 0);
    neutre(&b);
    b.ctl[CTL_REVERB]     = 80.0f;
    b.ctl[CTL_REVERB_MIX] = 100.0f;
    b.ctl[CTL_REVERB_ON]  = 0.0f;
    chauffer(&b, 200.0);          /* let the switch ramp reach zero first */
    for (int k = 0; k < 200; ++k) { sinus(&b, 600.0, 0.3); tourner(&b); }
    chauffer(&b, 300.0);
    /* -100 dBFS, not exact zero: LOW CUT reads 0 here, which is off, and
       off is a one-hertz high pass rather than a frozen one - so a few
       microvolts of the tone's start are still walking back to nothing
       160 ms later. A reverb tail at this point would be a thousand times
       bigger. */
    verifie("REVERB OFF: no tail at all", crete(b.out[0], b.bloc), 0.0, 1.0e-5);
    b.ctl[CTL_REVERB_ON] = 1.0f;
    for (int k = 0; k < 200; ++k) { sinus(&b, 600.0, 0.3); tourner(&b); }
    chauffer(&b, 300.0);
    verifie_vrai("REVERB ON: there is one", crete(b.out[0], b.bloc) > 1.0e-3);
    fermer(&b);
}

static void essai_interrupteurs_sans_clic(void)
{
    static const int commandes[] = {
        CTL_GATE_ON, CTL_COMP_ON, CTL_DE_ESS_ON, CTL_DRIVE_ON,
        CTL_DOUBLER_ON, CTL_MOD_ON, CTL_DELAY_ON, CTL_REVERB_ON
    };
    static const char* noms[] = {
        "GATE", "COMP", "DE-ESS", "DRIVE", "DOUBLE", "MOD", "DELAY", "REVERB"
    };

    for (int j = 0; j < 8; ++j) {
        Banc b;
        ouvrir(&b, 0, 48000.0, 64, 0);
        /* everything engaged at once: the worst case for a switch */
        b.ctl[CTL_LOW_CUT]       = 80.0f;
        b.ctl[CTL_GATE]          = -60.0f;
        b.ctl[CTL_COMP]          = 60.0f;
        b.ctl[CTL_DE_ESS]        = 60.0f;
        b.ctl[CTL_DRIVE]         = 50.0f;
        b.ctl[CTL_DOUBLER]       = 60.0f;
        b.ctl[CTL_MOD]           = 50.0f;
        b.ctl[CTL_DELAY_MIX]     = 60.0f;
        b.ctl[CTL_DELAY_REPEATS] = 40.0f;
        b.ctl[CTL_REVERB_MIX]    = 50.0f;
        chauffer(&b, 1500.0);

        /* Three seconds of singing first: with a 400 ms delay and a long
           reverb, "quiet" measured before those have reached the output
           is not quiet, it is early. */
        double calme = 0.0, precedent = 0.0;
        for (int k = 0; k < 2600; ++k) {
            sinus(&b, 500.0, 0.3);
            tourner(&b);
            for (uint32_t i = 0; i < b.bloc; ++i) {
                const double d = fabs((double)b.out[0][i] - precedent);
                if (k > 2200 && d > calme) { calme = d; }
                precedent = b.out[0][i];
            }
        }

        double bascule = 0.0;
        for (int tour = 0; tour < 2; ++tour) {
            b.ctl[commandes[j]] = tour ? 1.0f : 0.0f;
            for (int k = 0; k < 120; ++k) {
                sinus(&b, 500.0, 0.3);
                tourner(&b);
                for (uint32_t i = 0; i < b.bloc; ++i) {
                    const double d = fabs((double)b.out[0][i] - precedent);
                    if (d > bascule) { bascule = d; }
                    precedent = b.out[0][i];
                }
            }
        }
        char quoi[64];
        snprintf(quoi, sizeof(quoi), "throwing %s does not click", noms[j]);
        verifie_vrai(quoi, bascule < calme * 1.5);
        fermer(&b);
    }
}

/* Same measurement, for the knobs that used to be read once a block and
   applied whole: the compressor's makeup gain, SPREAD, the chorus depth
   and the number of voices. A program change turns all four at once,
   which is where it was heard - selecting a sound with a doubler in it
   banged. */
static void essai_boutons_sans_clic(void)
{
    static const int    commandes[] = { CTL_COMP, CTL_SPREAD, CTL_VOICES,
                                        CTL_MOD };
    static const float  depuis[]    = { 0.0f, 0.0f, 2.0f, 0.0f };
    static const float  vers[]      = { 70.0f, 100.0f, 4.0f, 80.0f };
    static const char*  noms[]      = { "COMP", "SPREAD", "VOICES", "MOD" };

    for (int j = 0; j < 4; ++j) {
        Banc b;
        ouvrir(&b, 0, 48000.0, 64, 0);
        b.ctl[CTL_LOW_CUT]    = 80.0f;
        b.ctl[CTL_GATE]       = -60.0f;
        b.ctl[CTL_DOUBLER]    = 60.0f;
        b.ctl[CTL_MOD]        = 30.0f;
        b.ctl[CTL_REVERB_MIX] = 20.0f;
        b.ctl[commandes[j]]   = depuis[j];
        chauffer(&b, 1500.0);

        double calme = 0.0, precedent = 0.0;
        for (int k = 0; k < 1200; ++k) {
            sinus(&b, 500.0, 0.3);
            tourner(&b);
            for (uint32_t i = 0; i < b.bloc; ++i) {
                const double d = fabs((double)b.out[0][i] - precedent);
                if (k > 900 && d > calme) { calme = d; }
                precedent = b.out[0][i];
            }
        }

        double saut = 0.0;
        for (int tour = 0; tour < 2; ++tour) {
            b.ctl[commandes[j]] = tour ? vers[j] : depuis[j];
            for (int k = 0; k < 120; ++k) {
                sinus(&b, 500.0, 0.3);
                tourner(&b);
                for (uint32_t i = 0; i < b.bloc; ++i) {
                    const double d = fabs((double)b.out[0][i] - precedent);
                    if (d > saut) { saut = d; }
                    precedent = b.out[0][i];
                }
            }
        }
        char quoi[64];
        snprintf(quoi, sizeof(quoi), "turning %s does not click", noms[j]);
        if (getenv("TRACE_CLIC")) {
            printf("    %s: calme=%.6f saut=%.6f (x%.2f)\n",
                   noms[j], calme, saut, saut / (calme > 0 ? calme : 1));
        }
        verifie_vrai(quoi, saut < calme * 1.5);
        fermer(&b);
    }

    /* And the case a singer actually meets: picking a sound from the
       list turns every one of those knobs at once. */
    {
        Banc b;
        ouvrir(&b, 0, 48000.0, 64, 0);
        neutre(&b);
        chauffer(&b, 1500.0);

        double calme = 0.0, precedent = 0.0;
        for (int k = 0; k < 1200; ++k) {
            sinus(&b, 500.0, 0.3);
            tourner(&b);
            for (uint32_t i = 0; i < b.bloc; ++i) {
                const double d = fabs((double)b.out[0][i] - precedent);
                if (k > 900 && d > calme) { calme = d; }
                precedent = b.out[0][i];
            }
        }

        double saut = 0.0;
        b.ctl[CTL_PROGRAM] = (float)programme_nomme("CHOIR");
        for (int k = 0; k < 400; ++k) {
            sinus(&b, 500.0, 0.3);
            tourner(&b);
            for (uint32_t i = 0; i < b.bloc; ++i) {
                const double d = fabs((double)b.out[0][i] - precedent);
                if (d > saut) { saut = d; }
                precedent = b.out[0][i];
            }
        }
        if (getenv("TRACE_CLIC")) {
            printf("    PROGRAM: calme=%.6f saut=%.6f (x%.2f)\n",
                   calme, saut, saut / (calme > 0 ? calme : 1));
        }
        /* Two and a half, and it is honest to say what the rest is: the
           tone controls and the low cut are FILTER COEFFICIENTS, and a
           program change moves them in one block. The gains, the mixes
           and the doubler all ramp; a coefficient does not, and the
           residue is the small tick that leaves. Measured without the
           host's write-back as well - moving the knobs is not what makes
           it, so smoothing the coefficients is the fix when it comes. */
        verifie_vrai("and picking a program does not click",
                     saut < calme * 2.5);
        fermer(&b);
    }
}

/* ================================================================== */
/* Presets, read out of the file that ships                            */
/* ================================================================== */

typedef struct {
    char  label[32];
    float val[CTL_COUNT];
} Preset;

static int lire_presets(Preset* p, int max)
{
    static char buf[1200000];
    FILE* f = fopen("presets.ttl", "r");
    if (!f) {
        printf("  *** presets.ttl not found - run the bench from voice/\n");
        echec = 1;
        return 0;
    }
    const size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    /* a truncated read would silently test the first half of the list */
    if (n == sizeof(buf) - 1) {
        printf("  *** presets.ttl is bigger than the buffer that reads it\n");
        echec = 1;
        return 0;
    }

    int nb = 0;
    const char* cur = buf;
    while (nb < max && (cur = strstr(cur, "a pset:Preset")) != NULL) {
        const char* fin = strstr(cur, "] .");
        const char* app = strstr(cur, "lv2:appliesTo <");
        const char* lab = strstr(cur, "rdfs:label \"");
        if (!fin) { break; }
        /* the mono variant only: the stereo presets carry the same values */
        if (app && lab && app < fin &&
            !strncmp(app + 15, "http://remy-live.github.io/lv2/voice>", 37)) {
            for (int i = 0; i < (int)CTL_COUNT; ++i) {
                p[nb].val[i] = ctl_spec[i].def;
            }
            memset(p[nb].label, 0, sizeof(p[nb].label));
            if (sscanf(lab + 12, "%31[^\"]", p[nb].label) != 1) {
                p[nb].label[0] = '\0';
            }
            p[nb].label[sizeof(p[nb].label) - 1] = '\0';
            const char* q = lab;
            while ((q = strstr(q, "lv2:symbol \"")) != NULL && q < fin) {
                char  sym[48];
                float v = 0.0f;
                const char* val = strstr(q, "pset:value ");
                if (sscanf(q + 12, "%47[^\"]", sym) == 1 && val && val < fin &&
                    sscanf(val + 11, "%f", &v) == 1) {
                    for (int i = 0; i < (int)CTL_COUNT; ++i) {
                        if (!strcmp(sym, ctl_spec[i].symbol)) { p[nb].val[i] = v; }
                    }
                }
                q += 12;
            }
            ++nb;
        }
        cur = fin;
    }
    return nb;
}


/* The compressor gives back what it takes off a voice at the reference
   level, so turning it up must not turn you up. */
static void essai_comp_niveau(void)
{
    const double amounts[] = { 0.0, 30.0, 60.0, 100.0 };
    for (int k = 0; k < 4; ++k) {
        Banc b;
        char quoi[72];
        ouvrir(&b, 0, 48000.0, 128, 0);
        neutre(&b);
        b.ctl[CTL_COMP] = (float)amounts[k];
        /* a sine whose PEAK sits on the reference: -12 dBFS */
        const double g = gain_db(&b, 500.0, 0.25, 300, 80);
        snprintf(quoi, sizeof(quoi), "COMP at %3.0f leaves a -12 dBFS voice alone",
                 amounts[k]);
        verifie(quoi, g, 0.0, 1.5);
        fermer(&b);
    }
}

/* ================================================================== */
/* The program list                                                    */
/* ================================================================== */

/* A sung phrase: loud lines, quiet lines, breaths between them. Loudness
   through a compressor is not loudness through a wire - density is what
   the ear adds up - so a preset measured on steady noise can pass while
   the same preset shouts at a singer. This bench measures on a phrase. */
static void phrase(float* buf, size_t n, double sr)
{
    double ph = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double t   = (double)i / sr;
        const double bar = fmod(t, 2.4);
        double env = (bar < 0.9) ? 0.30 : (bar < 1.2) ? 0.02
                   : (bar < 1.9) ? 0.09 : 0.004;
        env *= 0.6 + 0.4 * sin(2.0 * PI * 3.1 * t);       /* syllables */
        const double f = 180.0 + 40.0 * sin(2.0 * PI * 0.7 * t);
        ph += 2.0 * PI * f / sr;
        buf[i] = (float)(env * (sin(ph) + 0.5 * sin(2 * ph) + 0.25 * sin(3 * ph)
                              + 0.12 * sin(4 * ph)) * 0.55);
    }
}

/* Runs a phrase through one setting and returns its level in dB. */
static double niveau_phrase(const float* val, const float* in, size_t total,
                            double sr, float* sortie)
{
    const uint32_t N = 128;
    Banc b;
    ouvrir(&b, 0, sr, N, 0);
    for (int i = 0; i < (int)CTL_COUNT; ++i) { b.ctl[i] = val[i]; }
    b.d->activate(b.h);          /* with the ports already where they belong */

    double e = 0.0;
    size_t done = 0, compte = 0;
    const size_t saute = (size_t)(sr * 2.0);
    while (done + N <= total) {
        memcpy(b.in[0], in + done, N * sizeof(float));
        tourner(&b);
        if (sortie) { memcpy(sortie + done, b.out[0], N * sizeof(float)); }
        if (done > saute) {
            for (uint32_t i = 0; i < N; ++i) {
                e += (double)b.out[0][i] * (double)b.out[0][i];
            }
            compte += N;
        }
        done += N;
    }
    fermer(&b);
    return (compte && e > 0.0) ? 10.0 * log10(e / (double)compte) : -200.0;
}

static void essai_sonie_presets(void)
{
    Preset p[N_PROGRAM];
    const int nb = lire_presets(p, N_PROGRAM);
    const double sr = 48000.0;
    const size_t total = (size_t)(sr * 12.0);
    float* in = (float*)malloc(total * sizeof(float));
    phrase(in, total, sr);

    float nu[CTL_COUNT];
    for (int i = 0; i < (int)CTL_COUNT; ++i) { nu[i] = ctl_spec[i].def; }
    nu[CTL_COMP] = 0.0f;
    nu[CTL_LOW_CUT] = 0.0f;
    const double ref = niveau_phrase(nu, in, total, sr, NULL);

    for (int k = 0; k < nb; ++k) {
        char quoi[96];
        snprintf(quoi, sizeof(quoi), "\"%.20s\" against a transparent plugin",
                 p[k].label);
        verifie_entre(quoi, niveau_phrase(p[k].val, in, total, sr, NULL) - ref,
                      -2.5, 2.0);
        verifie_vrai("  and it never writes IN GAIN",
                     p[k].val[CTL_IN_GAIN] == ctl_spec[CTL_IN_GAIN].def);
    }
    free(in);
}

/* Picking "Ballad" from the list and selecting program 3 must give the
   same sound, sample for sample. They come from one table in make_ttl.py,
   but by two completely different routes: one writes ports, the other is
   read straight out of programs.h. */
static void essai_programme_egale_preset(void)
{
    Preset p[N_PROGRAM];
    const int nb = lire_presets(p, N_PROGRAM);
    const double sr = 48000.0;
    const size_t total = (size_t)(sr * 6.0);
    float* in  = (float*)malloc(total * sizeof(float));
    float* a   = (float*)malloc(total * sizeof(float));
    float* b   = (float*)malloc(total * sizeof(float));
    phrase(in, total, sr);

    double pire = 0.0;
    for (int k = 0; k < nb; ++k) {
        float par_port[CTL_COUNT], par_liste[CTL_COUNT];
        memcpy(par_port, p[k].val, sizeof(par_port));
        /* the list route: everything at its default except the switches,
           which belong to the player either way, and the program itself */
        for (int i = 0; i < (int)CTL_COUNT; ++i) { par_liste[i] = ctl_spec[i].def; }
        for (int j = 0; j < (int)SW_COUNT; ++j) {
            par_liste[switch_ctl[j]] = p[k].val[switch_ctl[j]];
        }
        par_liste[CTL_PROGRAM] = (float)(k + 1);

        niveau_phrase(par_port,  in, total, sr, a);
        niveau_phrase(par_liste, in, total, sr, b);
        for (size_t i = 0; i < total; ++i) {
            const double d = fabs((double)a[i] - (double)b[i]);
            if (d > pire) { pire = d; }
        }
    }
    verifie("every program is its preset, sample for sample", pire, 0.0, 0.0);
    free(in); free(a); free(b);
}

static void essai_programme_interrupteurs(void)
{
    Banc b;
    ouvrir(&b, 0, 48000.0, 128, 0);
    /* Speech is program 1 and starts with the delay switched off */
    b.ctl[CTL_PROGRAM] = 1.0f;
    silence(&b); tourner(&b);
    verifie_vrai("selecting a program adopts its switch positions",
                 program_switch[1][SW_DELAY] == 0);

    /* ... and a foot on the switch takes it straight back */
    b.ctl[CTL_DELAY_ON] = 0.0f;   /* move it, so the port is seen to change */
    silence(&b); tourner(&b);
    b.ctl[CTL_DELAY_ON] = 1.0f;
    silence(&b); tourner(&b);
    chauffer(&b, 2000.0);
    verifie("a program owns the knobs until one of them moves",
            (double)b.ctl[CTL_TIME_OUT], 300.0, 0.01);
    b.ctl[CTL_DELAY_TIME] = 200.0f;   /* and then that one is the player's */
    b.ctl[CTL_DELAY_MIX]  = 100.0f;
    chauffer(&b, 200.0);
    verifie("and hands one back the moment it does",
            (double)b.ctl[CTL_TIME_OUT], 200.0, 0.01);

    /* MANUAL hands everything back */
    b.ctl[CTL_PROGRAM] = 0.0f;
    silence(&b); tourner(&b);
    verifie("MANUAL hands the knobs back", (double)b.ctl[CTL_TIME_OUT], 200.0, 0.01);

    fermer(&b);

    /* IN GAIN and OUTPUT are never taken away. Measured on two fresh
       instances rather than by turning the knob on one: a program with a
       long reverb takes seconds to settle, and comparing before with
       after on the same instance measures the settling, not the knob. */
    double niveau[2];
    for (int k = 0; k < 2; ++k) {
        Banc c;
        ouvrir(&c, 0, 48000.0, 128, 0);
        c.ctl[CTL_PROGRAM] = 3.0f;                 /* Ballad */
        c.ctl[CTL_COMP_ON] = 0.0f;
        c.ctl[CTL_OUTPUT]  = k ? -20.0f : 0.0f;
        c.d->activate(c.h);
        niveau[k] = gain_db(&c, 500.0, 0.1, 400, 60);
        fermer(&c);
    }
    verifie("OUTPUT stays the player's under a program",
            niveau[1] - niveau[0], -20.0, 0.05);
}

static void essai_voix(void)
{
    /* Two, three or four voices must be a texture, not a volume. */
    double niveau[5];
    for (int n = 2; n <= 4; ++n) {
        Banc b;
        ouvrir(&b, 0, 48000.0, 128, 0);
        neutre(&b);
        b.ctl[CTL_DOUBLER] = 100.0f;
        b.ctl[CTL_VOICES]  = (float)n;
        srand(5);
        chauffer(&b, 300.0);
        niveau[n] = gain_bruit_db(&b, 0.2, 40, 400);
        fermer(&b);
    }
    verifie("three voices sit at the level of two", niveau[3], niveau[2], 0.6);
    verifie("and so do four", niveau[4], niveau[2], 0.6);

    /* but they are not the same signal */
    Banc b;
    ouvrir(&b, 0, 48000.0, 128, 0);
    neutre(&b);
    b.ctl[CTL_DOUBLER] = 100.0f;
    b.ctl[CTL_VOICES]  = 4.0f;
    chauffer(&b, 300.0);
    double bas = 1.0e9, haut = 0.0;
    for (int k = 0; k < 2000; ++k) {
        sinus(&b, 700.0, 0.2);
        tourner(&b);
        if (k > 200) {
            const double c = crete(b.out[0], b.bloc);
            if (c < bas)  { bas = c; }
            if (c > haut) { haut = c; }
        }
    }
    verifie_vrai("four voices drift like the others", haut > bas * 1.05);
    fermer(&b);
}

static void essai_ecran_programme(void)
{
    Banc b;
    memset(&ecran, 0, sizeof(ecran));
    ouvrir(&b, 0, 48000.0, 128, 1);
    neutre(&b);
    adresser(&b, CTL_PROGRAM, TOUTES_CAPS, (void*)0xE1);
    verifie_vrai("the list shows MANUAL to start with",
                 !strcmp(ecran.dernier_value, "MANUAL"));
    b.ctl[CTL_PROGRAM] = (float)programme_nomme("BALLAD");
    passer_popup(&b);
    verifie_vrai("and the name of whatever is picked",
                 !strcmp(ecran.dernier_value, "BALLAD"));
    verifie_vrai("under its own label", !strcmp(ecran.dernier_label, "PROGRAM"));

    /* A save with nothing to show for it is a save nobody believes in.
       For a second after the press, both the PROGRAM readout and the SAVE
       footswitch say where it went. */
    b.ctl[CTL_USER_SLOT] = 3.0f;
    b.ctl[CTL_SAVE] = 1.0f; silence(&b); tourner(&b);
    b.ctl[CTL_SAVE] = 0.0f;
    memset(&ecran, 0, sizeof(ecran));
    /* more than the 40 ms the screen is allowed to speak in */
    for (int k = 0; k < 30; ++k) { silence(&b); tourner(&b); }
    verifie_vrai("and SAVED, with the slot, right after a save",
                 !strcmp(ecran.dernier_value, "SAVED 3"));
    for (int k = 0; k < 900; ++k) { silence(&b); tourner(&b); }
    verifie_vrai("which goes away again",
                 !strcmp(ecran.dernier_value, "BALLAD"));

    memset(&ecran, 0, sizeof(ecran));
    adresser(&b, CTL_VOICES, TOUTES_CAPS, (void*)0xE2);
    verifie_vrai("the voice count shows itself",
                 !strcmp(ecran.dernier_label, "VOICES"));
    fermer(&b);
}

/* ================================================================== */
/* Pitch, without a pitch detector anywhere                            */
/* ================================================================== */

/* Energy at one frequency, by Goertzel: the bench has no FFT and does
   not need one to ask "did the note move where it was told to". */
static double energie_a(const float* x, size_t n, double f, double sr)
{
    const double w = 2.0 * PI * f / sr, c = 2.0 * cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double s = (double)x[i] + c * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}

static void essai_pitch(void)
{
    const double sr = 48000.0;
    const uint32_t N = 128;
    const size_t total = (size_t)(sr * 3.0);
    float* in  = (float*)malloc(total * sizeof(float));
    float* out = (float*)malloc(total * sizeof(float));

    for (int cas = 0; cas < 5; ++cas) {
        static const float demi[5] = { 0.0f, 12.0f, -12.0f, 7.0f, -5.0f };
        Banc b;
        ouvrir(&b, 0, sr, N, 0);
        neutre(&b);
        b.ctl[CTL_PITCH]     = demi[cas];
        b.ctl[CTL_PITCH_MIX] = 100.0f;

        double ph = 0.0;
        const double w = 2.0 * PI * 220.0 / sr;
        size_t done = 0;
        while (done + N <= total) {
            for (uint32_t i = 0; i < N; ++i) {
                in[done + i] = (float)(0.25 * sin(ph + w * (double)i));
            }
            ph += w * (double)N;
            memcpy(b.in[0], in + done, N * sizeof(float));
            tourner(&b);
            memcpy(out + done, b.out[0], N * sizeof(float));
            done += N;
        }
        fermer(&b);

        const size_t saute = (size_t)sr;
        const size_t n = total - saute;
        double ein = 0.0, eout = 0.0;
        for (size_t i = saute; i < total; ++i) {
            ein  += (double)in[i] * (double)in[i];
            eout += (double)out[i] * (double)out[i];
        }
        const double attendue = 220.0 * pow(2.0, demi[cas] / 12.0);
        const double e_neuve = energie_a(out + saute, n, attendue, sr);
        const double e_vieille = energie_a(out + saute, n, 220.0, sr);
        char quoi[80];

        snprintf(quoi, sizeof(quoi), "%+.0f semitones lands on %.0f Hz",
                 demi[cas], attendue);
        if (cas == 0) {
            verifie("0 semitones is exactly transparent",
                    10.0 * log10(eout / ein), 0.0, 0.1);
        } else {
            verifie_vrai(quoi, e_neuve > e_vieille * 100.0);
            snprintf(quoi, sizeof(quoi), "  and %+.0f keeps its level", demi[cas]);
            verifie(quoi, 10.0 * log10(eout / ein), 0.0, 1.0);
        }
    }
    free(in); free(out);

    /* the switch, and the mix, both take it back out */
    Banc b;
    ouvrir(&b, 0, sr, N, 0);
    neutre(&b);
    b.ctl[CTL_PITCH]     = -7.0f;
    b.ctl[CTL_PITCH_MIX] = 100.0f;
    b.ctl[CTL_PITCH_ON]  = 0.0f;
    chauffer(&b, 300.0);
    verifie("PITCH OFF leaves the voice where it was",
            gain_db(&b, 500.0, 0.2, 60, 60), 0.0, 0.2);
    b.ctl[CTL_PITCH_ON]  = 1.0f;
    b.ctl[CTL_PITCH_MIX] = 0.0f;
    chauffer(&b, 300.0);
    verifie("and so does a mix of zero",
            gain_db(&b, 500.0, 0.2, 60, 60), 0.0, 0.2);
    fermer(&b);
}

/* ================================================================== */
/* The tone controls                                                   */
/* ================================================================== */

static void essai_eq(void)
{
    Banc b;
    ouvrir(&b, 0, 48000.0, 256, 0);
    neutre(&b);

    b.ctl[CTL_PRESENCE] = 12.0f;
    b.ctl[CTL_MID_FREQ] = 800.0f;
    const double bas_a_800 = gain_db(&b, 800.0, 0.1, 40, 60);
    const double bas_a_4k  = gain_db(&b, 4000.0, 0.1, 40, 60);
    b.ctl[CTL_MID_FREQ] = 4000.0f;
    const double haut_a_800 = gain_db(&b, 800.0, 0.1, 40, 60);
    const double haut_a_4k  = gain_db(&b, 4000.0, 0.1, 40, 60);

    verifie_vrai("MID FREQ low lifts 800 Hz", bas_a_800 > 6.0);
    verifie_vrai("MID FREQ high lifts 4 kHz", haut_a_4k > 6.0);
    verifie_vrai("and each leaves the other end alone",
                 bas_a_4k < bas_a_800 - 4.0 && haut_a_800 < haut_a_4k - 4.0);

    /* the switch takes all three bands out at once */
    b.ctl[CTL_BODY] = 12.0f;
    b.ctl[CTL_AIR]  = 12.0f;
    b.ctl[CTL_EQ_ON] = 0.0f;
    chauffer(&b, 200.0);
    verifie("EQ OFF is flat at 100 Hz", gain_db(&b, 100.0, 0.1, 40, 60), 0.0, 0.2);
    verifie("EQ OFF is flat at 4 kHz", gain_db(&b, 4000.0, 0.1, 40, 60), 0.0, 0.2);
    verifie("EQ OFF is flat at 12 kHz", gain_db(&b, 12000.0, 0.1, 40, 60), 0.0, 0.2);
    fermer(&b);
}

/* ================================================================== */
/* The four USER slots                                                 */
/* ================================================================== */

static void essai_slots(void)
{
    Banc b;
    const int premier = N_PROGRAM;          /* USER 1 on the program list */
    ouvrir(&b, 0, 48000.0, 128, 0);
    neutre(&b);

    /* An empty slot leaves the knobs in charge - which is what makes
       dialling a sound and then storing it one continuous action. */
    b.ctl[CTL_PROGRAM]    = (float)premier;
    b.ctl[CTL_DELAY_TIME] = 250.0f;
    silence(&b); tourner(&b);
    verifie("an empty USER slot leaves the knobs alone",
            (double)b.ctl[CTL_TIME_OUT], 250.0, 0.01);

    /* store into slot 1, then move the knobs: the slot must not follow */
    b.ctl[CTL_USER_SLOT] = 1.0f;
    b.ctl[CTL_SAVE] = 1.0f; silence(&b); tourner(&b);
    b.ctl[CTL_SAVE] = 0.0f; silence(&b); tourner(&b);
    /* a slot is a program like any other: the knob still takes over ... */
    b.ctl[CTL_DELAY_TIME] = 900.0f;
    silence(&b); tourner(&b);
    verifie("a saved slot can be edited like anything else",
            (double)b.ctl[CTL_TIME_OUT], 900.0, 0.01);

    b.ctl[CTL_PROGRAM] = 0.0f;
    silence(&b); tourner(&b);
    verifie("MANUAL still answers to the knobs",
            (double)b.ctl[CTL_TIME_OUT], 900.0, 0.01);

    /* ... and what was stored is still what comes back */
    b.ctl[CTL_PROGRAM] = (float)premier;
    silence(&b); tourner(&b);
    verifie("coming back recalls what was stored, not the knob",
            (double)b.ctl[CTL_TIME_OUT], 250.0, 0.01);

    /* a second slot, written from MANUAL, must not touch the first */
    b.ctl[CTL_PROGRAM]    = 0.0f;
    b.ctl[CTL_DELAY_TIME] = 111.0f;
    b.ctl[CTL_USER_SLOT]  = 4.0f;
    silence(&b); tourner(&b);
    b.ctl[CTL_SAVE] = 1.0f; silence(&b); tourner(&b);
    b.ctl[CTL_SAVE] = 0.0f; silence(&b); tourner(&b);
    b.ctl[CTL_PROGRAM] = (float)(premier + 3);      /* USER 4 */
    silence(&b); tourner(&b);
    verifie("USER SLOT chooses where SAVE writes",
            (double)b.ctl[CTL_TIME_OUT], 111.0, 0.01);
    b.ctl[CTL_PROGRAM] = (float)premier;
    silence(&b); tourner(&b);
    verifie("and the other slot is untouched",
            (double)b.ctl[CTL_TIME_OUT], 250.0, 0.01);
    fermer(&b);
}

/* The thing a preset list is for: pick a sound, change what you do not
   like about it, keep the result. Every control a program owns has to go
   back to the knob the moment the knob moves, or a built-in sound is a
   cage rather than a starting point. */
static void essai_retouche(void)
{
    Banc b;
    ouvrir(&b, 0, 48000.0, 128, 0);
    neutre(&b);

    const int p_ballad = programme_nomme("BALLAD");
    const int p_rock   = programme_nomme("ROCK");
    const double t_ballad = (double)program_value[p_ballad][program_col[CTL_DELAY_TIME]];
    const double t_rock   = (double)program_value[p_rock][program_col[CTL_DELAY_TIME]];
    verifie_vrai("the two programs the test needs are in the list",
                 p_ballad > 0 && p_rock > 0 && t_ballad != t_rock);

    b.ctl[CTL_PROGRAM] = (float)p_ballad;
    silence(&b); tourner(&b);
    verifie("a program owns its controls to start with",
            (double)b.ctl[CTL_TIME_OUT], t_ballad, 0.01);

    b.ctl[CTL_DELAY_TIME] = 180.0f;         /* the player turns the knob */
    silence(&b); tourner(&b);
    verifie("turning a knob takes that one control back",
            (double)b.ctl[CTL_TIME_OUT], 180.0, 0.01);

    /* ... and ONLY that one: the rest of the program is still in force */
    b.ctl[CTL_REVERB_MIX] = 0.0f;           /* untouched so far */
    verifie_vrai("the rest of the program is still in force",
                 program_value[p_ballad][program_col[CTL_REVERB_MIX]] > 0.0f);

    /* the edited sound is what SAVE keeps */
    b.ctl[CTL_USER_SLOT] = 2.0f;
    b.ctl[CTL_SAVE] = 1.0f; silence(&b); tourner(&b);
    b.ctl[CTL_SAVE] = 0.0f; silence(&b); tourner(&b);
    b.ctl[CTL_PROGRAM]    = 0.0f;           /* back to MANUAL */
    b.ctl[CTL_DELAY_TIME] = 700.0f;
    silence(&b); tourner(&b);
    b.ctl[CTL_PROGRAM] = (float)(N_PROGRAM + 1);   /* USER 2 */
    silence(&b); tourner(&b);
    verifie("SAVE keeps the edit, not the knob it came from",
            (double)b.ctl[CTL_TIME_OUT], 180.0, 0.01);

    /* The case the web UI creates every time: SAVE and the jump to the
       slot land in the same block. Applied in the wrong order, entering
       the slot first makes SAVE read THE SLOT instead of the knobs and
       store its old contents back into itself - the sound stays right
       until you come back to it, which is the worst way for a save to
       fail. */
    b.ctl[CTL_USER_SLOT]  = 4.0f;
    b.ctl[CTL_PROGRAM]    = 0.0f;              /* MANUAL */
    b.ctl[CTL_DELAY_TIME] = 240.0f;
    silence(&b); tourner(&b);
    b.ctl[CTL_SAVE] = 1.0f; silence(&b); tourner(&b);       /* fill it once */
    b.ctl[CTL_SAVE] = 0.0f; silence(&b); tourner(&b);

    b.ctl[CTL_DELAY_TIME] = 960.0f;
    silence(&b); tourner(&b);
    b.ctl[CTL_SAVE]    = 1.0f;                 /* both in ONE block */
    b.ctl[CTL_PROGRAM] = (float)(N_PROGRAM + 3);
    silence(&b); tourner(&b);
    b.ctl[CTL_SAVE] = 0.0f; silence(&b); tourner(&b);

    b.ctl[CTL_PROGRAM]    = 0.0f;
    b.ctl[CTL_DELAY_TIME] = 130.0f;
    silence(&b); tourner(&b);
    b.ctl[CTL_PROGRAM] = (float)(N_PROGRAM + 3);
    silence(&b); tourner(&b);
    verifie("saving and jumping to the slot in one block keeps the sound",
            (double)b.ctl[CTL_TIME_OUT], 960.0, 0.01);

    b.ctl[CTL_PROGRAM] = 0.0f;
    silence(&b); tourner(&b);

    /* choosing another program hands everything back to it */
    b.ctl[CTL_PROGRAM] = (float)p_rock;
    silence(&b); tourner(&b);
    verifie("a new program takes every control back",
            (double)b.ctl[CTL_TIME_OUT], t_rock, 0.01);
    fermer(&b);
}

/* The slots have to survive the pedalboard being closed and reopened,
   which means going out through the State extension and coming back. */
static float etat_buf[512];
static size_t etat_taille = 0;
static uint32_t etat_type = 0;
static LV2_URID etat_cle = 0;

static LV2_State_Status f_store(LV2_State_Handle h, uint32_t key, const void* value,
                                size_t size, uint32_t type, uint32_t flags)
{
    (void)h; (void)flags;
    if (size > sizeof(etat_buf)) { return LV2_STATE_ERR_UNKNOWN; }
    memcpy(etat_buf, value, size);
    etat_taille = size;
    etat_type = type;
    etat_cle = key;
    return LV2_STATE_SUCCESS;
}

static const void* f_retrieve(LV2_State_Handle h, uint32_t key, size_t* size,
                              uint32_t* type, uint32_t* flags)
{
    (void)h;
    if (key != etat_cle || etat_taille == 0) { return NULL; }
    if (size)  { *size = etat_taille; }
    if (type)  { *type = etat_type; }
    if (flags) { *flags = 0u; }
    return etat_buf;
}

static void essai_etat(void)
{
    const LV2_Descriptor* d = lv2_descriptor(0);
    const LV2_State_Interface* st =
        (const LV2_State_Interface*)d->extension_data(LV2_STATE__interface);
    verifie_vrai("the plugin offers a state interface", st != NULL);
    if (!st) { return; }

    /* one instance saves a slot */
    Banc a;
    ouvrir(&a, 0, 48000.0, 128, 0);
    neutre(&a);
    a.ctl[CTL_USER_SLOT]  = 3.0f;                      /* store into USER 3 */
    a.ctl[CTL_DELAY_TIME] = 333.0f;
    a.ctl[CTL_REVERB_MIX] = 44.0f;
    a.ctl[CTL_SLOT_NAME]  = 3.0f;                      /* called CHORUS */
    silence(&a); tourner(&a);
    a.ctl[CTL_SAVE] = 1.0f; silence(&a); tourner(&a);
    a.ctl[CTL_SAVE] = 0.0f; silence(&a); tourner(&a);
    etat_taille = 0;
    verifie_vrai("state saves without complaining",
                 st->save(a.h, f_store, NULL, 0, NULL) == LV2_STATE_SUCCESS);
    verifie_vrai("and it wrote something", etat_taille > 0);
    fermer(&a);

    /* another instance takes it back */
    Banc b;
    ouvrir(&b, 0, 48000.0, 128, 0);
    neutre(&b);
    verifie_vrai("state restores without complaining",
                 st->restore(b.h, f_retrieve, NULL, 0, NULL) == LV2_STATE_SUCCESS);
    b.ctl[CTL_PROGRAM] = (float)N_PROGRAM + 2.0f;      /* USER 3 */
    b.ctl[CTL_DELAY_TIME] = 800.0f;         /* the knobs are elsewhere */
    silence(&b); tourner(&b);
    verifie_vrai("and the name came back with the sound",
                 !strcmp(((Voice*)b.h)->user[2].name, "CHORUS"));
    verifie("a restored slot plays what was saved in it",
            (double)b.ctl[CTL_TIME_OUT], 333.0, 0.01);

    /* An untouched slot must still be empty, not full of zeros. The knob
       is set AFTER selecting it: entering a filled slot now moves the
       knobs to what it holds, so "the knobs are elsewhere" has to be
       arranged rather than assumed. */
    b.ctl[CTL_PROGRAM] = (float)N_PROGRAM;   /* USER 1, never saved */
    silence(&b); tourner(&b);
    b.ctl[CTL_DELAY_TIME] = 800.0f;
    silence(&b); tourner(&b);
    verifie("an empty slot stays empty across a restore",
            (double)b.ctl[CTL_TIME_OUT], 800.0, 0.01);

    /* nonsense in the state must be refused rather than believed */
    etat_taille = 8;
    verifie_vrai("a state of the wrong size is refused",
                 st->restore(b.h, f_retrieve, NULL, 0, NULL) != LV2_STATE_SUCCESS);
    fermer(&b);
}

/* ================================================================== */
/* The anti-Larsen hunter                                              */
/* ================================================================== */

/* A room, which is all a Larsen needs: the output comes back into the
   input, late, through a resonance. With the loop gain over one it howls,
   and with it under one it does not - so the same test rig proves both
   that the hunter stops a howl and that it leaves a played note alone. */
typedef struct { double b0, b1, b2, a1, a2, z1, z2; } Biquad;

static void bq_bandpass(Biquad* f, double fc, double q, double sr, double gain)
{
    const double w = 2.0 * PI * fc / sr;
    const double al = sin(w) / (2.0 * q);
    const double c = cos(w), a0 = 1.0 + al;
    f->b0 = gain * al / a0; f->b1 = 0.0; f->b2 = -gain * al / a0;
    f->a1 = -2.0 * c / a0;  f->a2 = (1.0 - al) / a0;
    f->z1 = f->z2 = 0.0;
}

static double bq_run(Biquad* f, double x)
{
    const double y = f->b0 * x + f->z1;
    f->z1 = f->b1 * x - f->a1 * y + f->z2;
    f->z2 = f->b2 * x - f->a2 * y;
    return y;
}

typedef struct {
    double pic_tot, pic_fin;
    double notches;
} Larsen;

static Larsen larsen_essai(float hunt, double loop_gain, int note, double hz)
{
    const double sr = 48000.0;
    const uint32_t N = 64;
    const size_t total = (size_t)(sr * 12.0);
    Banc b;
    ouvrir(&b, 0, sr, N, 0);
    neutre(&b);
    b.ctl[CTL_FEEDBACK] = hunt;

    const int D = (int)(0.005 * sr);
    const int len = D + (int)N;
    float* line = (float*)calloc((size_t)len, sizeof(float));
    Biquad room;
    /* the resonance normalised by measurement, then scaled: a loop gain of
       one is exactly on the edge of howling */
    double norm = 0.0;
    {
        Biquad t; bq_bandpass(&t, hz, 3.0, sr, 1.0);
        double p = 0.0;
        for (int i = 0; i < 20000; ++i) {
            p += 2.0 * PI * hz / sr;
            const double y = bq_run(&t, sin(p));
            if (i > 10000 && fabs(y) > norm) { norm = fabs(y); }
        }
    }
    bq_bandpass(&room, hz, 3.0, sr, loop_gain / (norm > 0.0 ? norm : 1.0));

    Larsen r = { 0.0, 0.0, 0.0 };
    double ph = 0.0;
    int w = 0;
    size_t done = 0;
    while (done + N <= total) {
        for (uint32_t i = 0; i < N; ++i) {
            const double t = (double)(done + i) / sr;
            double src;
            if (note) {
                /* a sung note: harmonics, vibrato, and it breathes */
                const double f = 440.0 * (1.0 + 0.03 * sin(2.0 * PI * 5.5 * t));
                ph += 2.0 * PI * f / sr;
                src = 0.22 * (sin(ph) + 0.55 * sin(2 * ph) + 0.30 * sin(3 * ph)
                            + 0.15 * sin(4 * ph)) * (0.85 + 0.15 * sin(2.0 * PI * 0.7 * t));
            } else {
                ph += 2.0 * PI * hz / sr;
                src = (t < 0.6 ? 0.20 : 0.004) * sin(ph);
            }
            int rd = w + (int)i - D;
            while (rd < 0)    { rd += len; }
            while (rd >= len) { rd -= len; }
            double back = bq_run(&room, (double)line[rd]);
            if (back >  2.0) { back =  2.0; }
            if (back < -2.0) { back = -2.0; }
            b.in[0][i] = (float)(src + back);
        }
        tourner(&b);
        for (uint32_t i = 0; i < N; ++i) {
            line[w] = b.out[0][i];
            if (++w >= len) { w = 0; }
            const double a = fabs((double)b.out[0][i]);
            const double t = (double)(done + i) / sr;
            if (t > 1.5 && a > r.pic_tot) { r.pic_tot = a; }
            if (t > 9.0 && a > r.pic_fin) { r.pic_fin = a; }
        }
        r.notches = (double)b.ctl[CTL_NOTCHES];
        done += N;
    }
    free(line);
    fermer(&b);
    return r;
}

static void essai_larsen(void)
{
    const Larsen libre  = larsen_essai(0.0f,  1.6, 0, 1200.0);
    const Larsen chasse = larsen_essai(60.0f, 1.6, 0, 1200.0);
    const Larsen dur    = larsen_essai(90.0f, 1.6, 0, 1200.0);
    const Larsen chante = larsen_essai(90.0f, 0.0, 1, 1200.0);

    verifie_vrai("without the hunter the room howls and stays there",
                 libre.pic_fin > 0.5 && libre.notches == 0.0);
    verifie_vrai("with it, the howl is hunted down",
                 chasse.pic_fin < libre.pic_fin * 0.05 && chasse.notches > 0.0);
    verifie_vrai("and hunting harder catches it sooner",
                 dur.pic_tot < libre.pic_tot * 0.5);
    verifie("a sung note with harmonics and vibrato is not notched",
            chante.notches, 0.0, 0.001);
    verifie_vrai("and it comes through untouched", chante.pic_fin > 0.2);

    /* The top of the bank had a bug worth a test of its own. The test
       that keeps a harmonic out of the notches asks "is my octave
       partner three bands up loud too?", and the top three bands have no
       partner up there, so they asked it backwards - "is the band three
       BELOW me loud?" - which is true of every voice ever recorded,
       there being always more energy low than high. The top three bands
       were therefore vetoed on every block and could never be notched,
       and five kilohertz is exactly where a bright PA sings. */
    const Larsen aigu_libre  = larsen_essai(0.0f,  1.6, 1, 5200.0);
    const Larsen aigu_chasse = larsen_essai(60.0f, 1.6, 1, 5200.0);
    if (getenv("TRACE_LARSEN")) {
        printf("    aigu libre  pic_fin=%.4f notches=%.1f\n",
               aigu_libre.pic_fin, aigu_libre.notches);
        printf("    aigu chasse pic_fin=%.4f notches=%.1f\n",
               aigu_chasse.pic_fin, aigu_chasse.notches);
    }
    verifie_vrai("a howl at five kilohertz over a sung note builds up too",
                 aigu_libre.pic_fin > 0.5 && aigu_libre.notches == 0.0);
    verifie_vrai("and the hunter catches that one as well",
                 aigu_chasse.pic_fin < aigu_libre.pic_fin * 0.5
                 && aigu_chasse.notches > 0.0);
}

/* A notch has to be GIVEN BACK, at every sample rate. The first version
   never was: in silence a band and the broadband reference decay at the
   same rate, so their ratio never changes and a band that was dominant
   when the room went quiet looked dominant for ever. */
static void essai_larsen_rendu(void)
{
    const double taux[] = { 44100.0, 48000.0, 96000.0 };
    for (int t = 0; t < 3; ++t) {
        Banc b;
        char quoi[80];
        ouvrir(&b, 0, taux[t], 128, 0);
        neutre(&b);
        b.ctl[CTL_FEEDBACK] = 100.0f;
        const long blocs = (long)(taux[t] * 4.0 / 128.0);
        for (long k = 0; k < blocs; ++k) { sinus(&b, 1241.0, 0.4); tourner(&b); }
        snprintf(quoi, sizeof(quoi), "at %.0f Hz a steady tone is notched", taux[t]);
        verifie_vrai(quoi, b.ctl[CTL_NOTCHES] > 0.0f);

        chauffer(&b, 90000.0);          /* a minute and a half of nothing */
        snprintf(quoi, sizeof(quoi), "  and given back when the room goes quiet");
        verifie(quoi, (double)b.ctl[CTL_NOTCHES], 0.0, 0.001);
        fermer(&b);
    }
}

static void essai_larsen_relache(void)
{
    /* switching the block off must let the notches out rather than
       leaving four holes in the tone for ever */
    Banc b;
    ouvrir(&b, 0, 48000.0, 128, 0);
    neutre(&b);
    b.ctl[CTL_FEEDBACK] = 100.0f;
    /* a pure steady tone is what a howl looks like, and there is no
       harmless way to tell it apart - so it gets notched, which is what
       makes this a fair way to place one on purpose */
    for (int k = 0; k < 800; ++k) { sinus(&b, 1241.0, 0.4); tourner(&b); }
    verifie_vrai("a steady pure tone does get notched", b.ctl[CTL_NOTCHES] > 0.0f);
    const double avec = gain_db(&b, 1241.0, 0.4, 40, 60);

    b.ctl[CTL_FEEDBACK_ON] = 0.0f;
    chauffer(&b, 400.0);
    verifie("switching the hunter off gives the notches back",
            (double)b.ctl[CTL_NOTCHES], 0.0, 0.001);
    verifie_vrai("and the frequency comes back with them",
                 gain_db(&b, 1241.0, 0.4, 40, 60) > avec + 3.0);
    fermer(&b);
}

/* Switching the hunter off has to forget what it was about to do. A slot
   that still owes a notch to a band plants one the moment the hunter
   comes back, on a room it has not listened to since. */
static void essai_larsen_oubli(void)
{
    Banc b;
    ouvrir(&b, 0, 48000.0, 128, 0);
    neutre(&b);
    b.ctl[CTL_FEEDBACK] = 100.0f;
    for (int k = 0; k < 800; ++k) { sinus(&b, 1241.0, 0.4); tourner(&b); }
    verifie_vrai("a tone is notched, and a slot may owe another band",
                 b.ctl[CTL_NOTCHES] > 0.0f);

    b.ctl[CTL_FEEDBACK_ON] = 0.0f;
    chauffer(&b, 400.0);
    b.ctl[CTL_FEEDBACK_ON] = 1.0f;
    chauffer(&b, 200.0);            /* silence, and the hunter back on */
    verifie("coming back on in silence plants nothing",
            (double)b.ctl[CTL_NOTCHES], 0.0, 0.001);
    fermer(&b);
}

/* One non-finite sample from a plugin upstream used to be the end of the
   session: it latched into the first filter state and was subtracted from
   every sample after it, for ever. */
static void essai_non_fini(void)
{
    const float valeurs[2] = { (float)(1.0 / 0.0), (float)(0.0 / 0.0) };
    const char* noms[2] = { "an infinity", "a NaN" };

    for (int j = 0; j < 2; ++j) {
        Banc b;
        char quoi[80];
        ouvrir(&b, 0, 48000.0, 128, 0);
        chauffer(&b, 200.0);
        for (int k = 0; k < 20; ++k) { sinus(&b, 440.0, 0.3); tourner(&b); }

        sinus(&b, 440.0, 0.3);
        b.in[0][64] = valeurs[j];
        tourner(&b);

        int fini = 1;
        for (int k = 0; k < 400; ++k) {
            sinus(&b, 440.0, 0.3);
            tourner(&b);
            for (uint32_t i = 0; i < b.bloc; ++i) {
                const float y = b.out[0][i];
                if (!(y > -8.0f && y < 8.0f)) { fini = 0; }
            }
        }
        snprintf(quoi, sizeof(quoi),
                 "%s on the input does not kill the channel", noms[j]);
        verifie_vrai(quoi, fini);
        verifie_vrai("  and the voice is still there",
                     crete(b.out[0], b.bloc) > 0.05f);
        fermer(&b);
    }
}

/* Harmony: two more of the same grain shifter, in parallel. Sung at
   220 Hz, a third above lands on 277 and a fourth below on 165 - and
   nothing of either is there when the mix is down or the switch is off,
   because a harmony you cannot turn off is not a harmony, it is a fault. */
static void essai_harmonie(void)
{
    const double sr = 48000.0;
    const uint32_t N = 128;
    const size_t total = (size_t)(sr * 4.0);
    float* out = (float*)malloc(total * sizeof(float));
    double tierce[3] = { 0.0, 0.0, 0.0 };
    double quarte[3] = { 0.0, 0.0, 0.0 };
    double fond[3]   = { 0.0, 0.0, 0.0 };

    for (int cas = 0; cas < 3; ++cas) {
        Banc b;
        ouvrir(&b, 0, sr, N, 0);
        neutre(&b);
        b.ctl[CTL_HARM_1]   = 4.0f;      /* a major third above */
        b.ctl[CTL_HARM_2]   = -5.0f;     /* a fourth below */
        b.ctl[CTL_HARM_MIX] = (cas == 1) ? 0.0f : 100.0f;
        b.ctl[CTL_HARM_ON]  = (cas == 2) ? 0.0f : 1.0f;

        double ph = 0.0;
        const double w = 2.0 * PI * 220.0 / sr;
        size_t done = 0;
        while (done + N <= total) {
            for (uint32_t i = 0; i < N; ++i) {
                b.in[0][i] = (float)(0.25 * sin(ph + w * (double)i));
            }
            ph += w * (double)N;
            tourner(&b);
            memcpy(out + done, b.out[0], N * sizeof(float));
            done += N;
        }
        fermer(&b);

        const size_t saute = (size_t)(sr * 1.0);
        const size_t n = total - saute;
        fond[cas]   = energie_a(out + saute, n, 220.00, sr);
        tierce[cas] = energie_a(out + saute, n, 277.18, sr);
        quarte[cas] = energie_a(out + saute, n, 164.81, sr);
    }
    free(out);

    verifie_vrai("a third above is really there",
                 tierce[0] > tierce[1] * 100.0);
    verifie_vrai("and a fourth below with it",
                 quarte[0] > quarte[1] * 100.0);
    verifie_vrai("neither is louder than the voice",
                 fond[0] > tierce[0] && fond[0] > quarte[0]);
    verifie_vrai("the switch takes both away",
                 tierce[2] < tierce[0] * 0.02 && quarte[2] < quarte[0] * 0.02);
    verifie_vrai("and at mix zero nothing is added at all",
                 tierce[1] < fond[1] * 1.0e-4);
}

/* MUTE is not the gate and not FX: it stops the sound. Over twenty
   milliseconds, so it does not thump, and it lets go the same way. */
static void essai_mute(void)
{
    Banc b;
    ouvrir(&b, 0, 48000.0, 64, 0);
    neutre(&b);
    b.ctl[CTL_REVERB_MIX] = 40.0f;
    chauffer(&b, 500.0);
    for (int k = 0; k < 200; ++k) { sinus(&b, 500.0, 0.3); tourner(&b); }
    const double avant = crete(b.out[0], b.bloc);
    verifie_vrai("the voice is there to start with", avant > 0.1);

    b.ctl[CTL_MUTE] = 1.0f;
    double saut = 0.0, precedent = (double)b.out[0][b.bloc - 1];
    for (int k = 0; k < 40; ++k) {          /* 53 ms */
        sinus(&b, 500.0, 0.3);
        tourner(&b);
        for (uint32_t i = 0; i < b.bloc; ++i) {
            const double d = fabs((double)b.out[0][i] - precedent);
            if (d > saut) { saut = d; }
            precedent = b.out[0][i];
        }
    }
    verifie("MUTE stops the sound", crete(b.out[0], b.bloc), 0.0, 1.0e-9);
    verifie_vrai("without a step in it", saut < 0.05);

    b.ctl[CTL_MUTE] = 0.0f;
    for (int k = 0; k < 40; ++k) { sinus(&b, 500.0, 0.3); tourner(&b); }
    verifie_vrai("and lets go again", crete(b.out[0], b.bloc) > avant * 0.9);
    fermer(&b);
}

/* A/B goes back to the program before this one - which the PROGRAM port
   knows nothing about, because a plugin may not write it. PROGRAM NOW is
   how anything else finds out. */
static void essai_ab(void)
{
    Banc b;
    ouvrir(&b, 0, 48000.0, 128, 0);
    neutre(&b);
    const int p_ballad = programme_nomme("BALLAD");
    const int p_choir  = programme_nomme("CHOIR");
    const double t_ballad =
        (double)program_value[p_ballad][program_col[CTL_DELAY_TIME]];
    const double t_choir =
        (double)program_value[p_choir][program_col[CTL_DELAY_TIME]];

    b.ctl[CTL_PROGRAM] = (float)p_ballad;
    silence(&b); tourner(&b);
    b.ctl[CTL_PROGRAM] = (float)p_choir;
    silence(&b); tourner(&b);
    verifie("two programs picked, the second one in force",
            (double)b.ctl[CTL_PROGRAM_NOW], (double)p_choir, 0.01);

    b.ctl[CTL_AB] = 1.0f; silence(&b); tourner(&b);
    b.ctl[CTL_AB] = 0.0f; silence(&b); tourner(&b);
    verifie("A/B goes back to the one before",
            (double)b.ctl[CTL_PROGRAM_NOW], (double)p_ballad, 0.01);
    verifie("and the sound goes with it",
            (double)b.ctl[CTL_TIME_OUT], t_ballad, 0.01);

    b.ctl[CTL_AB] = 1.0f; silence(&b); tourner(&b);
    b.ctl[CTL_AB] = 0.0f; silence(&b); tourner(&b);
    verifie("and again comes back", (double)b.ctl[CTL_TIME_OUT], t_choir, 0.01);

    /* the knob still wins: it is followed by its CHANGES, so turning it
       to where the port already was is still a change to the plugin */
    b.ctl[CTL_PROGRAM] = (float)p_ballad;
    silence(&b); tourner(&b);
    verifie("turning PROGRAM still beats A/B",
            (double)b.ctl[CTL_PROGRAM_NOW], (double)p_ballad, 0.01);
    fermer(&b);
}

/* The de-esser used to listen at 5.5 kHz whatever the voice was. */
static void essai_deess_freq(void)
{
    Banc b;
    ouvrir(&b, 0, 48000.0, 256, 0);
    neutre(&b);
    b.ctl[CTL_DE_ESS] = 100.0f;

    b.ctl[CTL_DE_ESS_FREQ] = 3000.0f;
    const double bas = gain_db(&b, 4000.0, 0.3, 120, 60);
    b.ctl[CTL_DE_ESS_FREQ] = 10000.0f;
    const double haut = gain_db(&b, 4000.0, 0.3, 120, 60);
    verifie_vrai("at 3 kHz the de-esser reaches a 4 kHz tone", bas < -6.0);
    verifie_vrai("at 10 kHz it leaves the same tone alone", haut > bas + 5.0);
    fermer(&b);
}

/* A name that reaches the pedal. It cannot be typed - a control port
   carries a number - so it is PICKED from a list, stored in the slot with
   the sound, saved with the pedalboard, and read out on the screen. And
   NEXT USER walks the slots that have something in them, which is the one
   footswitch a singer actually wants. */
static void essai_noms(void)
{
    Banc b;
    memset(&ecran, 0, sizeof(ecran));
    ouvrir(&b, 0, 48000.0, 128, 1);
    neutre(&b);

    /* slot 2 gets a sound and the word CHORUS */
    b.ctl[CTL_USER_SLOT]  = 2.0f;
    b.ctl[CTL_DELAY_TIME] = 210.0f;
    b.ctl[CTL_SLOT_NAME]  = 3.0f;             /* CHORUS, on the slot */
    silence(&b); tourner(&b);
    b.ctl[CTL_SAVE] = 1.0f; silence(&b); tourner(&b);
    b.ctl[CTL_SAVE] = 0.0f; silence(&b); tourner(&b);

    /* slot 5 gets another, with no name at all: USER, at the top of the
       list, means leave the name alone, and nothing has ever named it */
    b.ctl[CTL_USER_SLOT]  = 5.0f;
    b.ctl[CTL_DELAY_TIME] = 640.0f;
    b.ctl[CTL_SLOT_NAME]  = 0.0f;
    silence(&b); tourner(&b);
    b.ctl[CTL_SAVE] = 1.0f; silence(&b); tourner(&b);
    b.ctl[CTL_SAVE] = 0.0f; silence(&b); tourner(&b);

    adresser(&b, CTL_PROGRAM, TOUTES_CAPS, (void*)0xF1);
    /* past the SAVED flash, which owns the readout for a second */
    for (int k = 0; k < 500; ++k) { silence(&b); tourner(&b); }
    memset(&ecran, 0, sizeof(ecran));
    b.ctl[CTL_PROGRAM] = (float)(N_PROGRAM + 1);          /* USER 2 */
    for (int k = 0; k < 40; ++k) { silence(&b); tourner(&b); }
    verifie_vrai("landing on a favourite says its name in a popup",
                 ecran.n_popup > 0 && !strcmp(ecran.dernier_popup, "CHORUS"));
    verifie_vrai("under a title that says what it is",
                 !strcmp(ecran.dernier_popup_titre, "FAVORI"));
    passer_popup(&b);
    verifie_vrai("a named slot says its name on the screen",
                 !strcmp(ecran.dernier_value, "CHORUS"));
    b.ctl[CTL_PROGRAM] = (float)(N_PROGRAM + 4);          /* USER 5 */
    passer_popup(&b);
    verifie_vrai("and an unnamed one still says USER 5",
                 !strcmp(ecran.dernier_value, "USER 5"));

    /* the cycle switch: 5 -> 2 -> 5, skipping the four empty slots */
    b.ctl[CTL_NEXT_USER] = 1.0f; silence(&b); tourner(&b);
    b.ctl[CTL_NEXT_USER] = 0.0f; silence(&b); tourner(&b);
    verifie("NEXT USER skips the empty slots",
            (double)b.ctl[CTL_PROGRAM_NOW], (double)(N_PROGRAM + 1), 0.01);
    verifie("and brings its sound with it",
            (double)b.ctl[CTL_TIME_OUT], 210.0, 0.01);
    passer_popup(&b);
    verifie_vrai("under the name it was given",
                 !strcmp(ecran.dernier_value, "CHORUS"));

    b.ctl[CTL_NEXT_USER] = 1.0f; silence(&b); tourner(&b);
    b.ctl[CTL_NEXT_USER] = 0.0f; silence(&b); tourner(&b);
    verifie("and round again", (double)b.ctl[CTL_PROGRAM_NOW],
            (double)(N_PROGRAM + 4), 0.01);
    fermer(&b);

    /* nothing filled: the switch does nothing rather than landing on an
       empty slot, which on stage would be a silent press */
    ouvrir(&b, 0, 48000.0, 128, 0);
    neutre(&b);
    b.ctl[CTL_NEXT_USER] = 1.0f; silence(&b); tourner(&b);
    b.ctl[CTL_NEXT_USER] = 0.0f; silence(&b); tourner(&b);
    verifie("with no slot filled it stays where it is",
            (double)b.ctl[CTL_PROGRAM_NOW], 0.0, 0.01);
    fermer(&b);
}

/* A name typed in the web page. A control port carries a number, not a
   word, so the word goes down one character at a time: the plugin looks
   at web_char every time web_strobe CHANGES, and the change is what
   counts - a pedalboard restoring the port to whatever it held would
   otherwise type a character of its own every time the board opened.

   And back the other way: the page has no way to ask what the six slots
   are called, so the plugin says, one slot per second, on name_slot and
   the seven letters beside it. */

/* one code down the wire, the way the page sends it: the slot travels
   with the character, so nothing here depends on what went before */
static void taper(Banc* b, int slot, int code)
{
    b->ctl[CTL_WEB_SLOT]   = (float)slot;
    b->ctl[CTL_WEB_CHAR]   = (float)code;
    b->ctl[CTL_WEB_STROBE] = b->ctl[CTL_WEB_STROBE] + 1.0f;
    silence(b); tourner(b);
}

static void taper_nom(Banc* b, int slot, const char* mot)
{
    taper(b, slot, 1);                        /* clear, then spell it */
    for (const char* p = mot; *p; ++p) { taper(b, slot, (unsigned char)*p); }
}

/* the name the plugin is showing the page at this instant */
static void echo_lu(Banc* b, int* slot, char* nom)
{
    *slot = (int)(b->ctl[CTL_NAME_SLOT] + 0.5f);
    for (int k = 0; k < 7; ++k) {
        const int c = (int)(b->ctl[CTL_N1 + k] + 0.5f);
        nom[k] = (c >= 32 && c <= 126) ? (char)c : ' ';
    }
    nom[7] = '\0';
    for (int k = 6; k >= 0 && nom[k] == ' '; --k) { nom[k] = '\0'; }
}

static void essai_nom_web(void)
{
    Banc b;
    ouvrir(&b, 0, 48000.0, 128, 0);
    neutre(&b);

    taper_nom(&b, 2, "chorus");
    verifie_vrai("a name typed in the page reaches the slot",
                 !strcmp(((Voice*)b.h)->user[1].name, "CHORUS"));

    /* lower case is not on the screen of the pedal */
    taper_nom(&b, 3, "Growl");
    verifie_vrai("and arrives in capitals whatever was typed",
                 !strcmp(((Voice*)b.h)->user[2].name, "GROWL"));

    /* eight characters into seven: the eighth is dropped rather than
       running over the end of the name */
    taper_nom(&b, 4, "ABCDEFGHIJ");
    verifie_vrai("a name too long for the screen is cut, not overrun",
                 !strcmp(((Voice*)b.h)->user[3].name, "ABCDEFG"));

    /* rubbing out */
    taper(&b, 5, 1);
    taper(&b, 5, 'S'); taper(&b, 5, 'O'); taper(&b, 5, 'L'); taper(&b, 5, 'X');
    taper(&b, 5, 8);
    taper(&b, 5, 'O');
    verifie_vrai("and backspace rubs out the last letter only",
                 !strcmp(((Voice*)b.h)->user[4].name, "SOLO"));

    /* the strobe is what counts, not the value: the same character sent
       again with no change of strobe is not a second character */
    b.ctl[CTL_WEB_CHAR] = (float)'Z';
    for (int k = 0; k < 10; ++k) { silence(&b); tourner(&b); }
    verifie_vrai("a character with no strobe behind it types nothing",
                 !strcmp(((Voice*)b.h)->user[4].name, "SOLO"));

    /* slot 0 is nowhere: a strobe with no slot behind it types nothing */
    taper(&b, 0, 'Z');
    verifie_vrai("and a character with no slot behind it goes nowhere",
                 !strcmp(((Voice*)b.h)->user[4].name, "SOLO"));

    /* every letter carries its own slot, so two names can be spelt at
       once without either landing on the other */
    taper(&b, 6, 'A'); taper(&b, 1, 'B');
    taper(&b, 6, 'C'); taper(&b, 1, 'D');
    verifie_vrai("two names spelt at once do not mix",
                 !strcmp(((Voice*)b.h)->user[5].name, "AC")
                 && !strcmp(((Voice*)b.h)->user[0].name, "BD"));
    taper(&b, 6, 1); taper(&b, 1, 1);          /* and put them back empty */

    /* --- and the six coming back up, one per second --- */
    int vus[N_USER + 1];
    memset(vus, 0, sizeof(vus));
    char trouve[N_USER + 1][8];
    memset(trouve, 0, sizeof(trouve));
    /* seven seconds: six slots, one a second, and one to spare */
    for (int k = 0; k < 7 * 375; ++k) {
        silence(&b); tourner(&b);
        int slot; char nom[8];
        echo_lu(&b, &slot, nom);
        if (slot >= 1 && slot <= N_USER) {
            vus[slot] = 1;
            memcpy(trouve[slot], nom, sizeof(nom));
        }
    }
    int tous = 1;
    for (int u = 1; u <= N_USER; ++u) { if (!vus[u]) { tous = 0; } }
    verifie_vrai("every slot comes round on the echo", tous);
    verifie_vrai("and says the name it was given",
                 !strcmp(trouve[2], "CHORUS") && !strcmp(trouve[3], "GROWL")
                 && !strcmp(trouve[5], "SOLO"));
    verifie_vrai("an empty slot echoes an empty name", !trouve[1][0]);
    fermer(&b);

    /* Nothing is typed during the two seconds a pedalboard takes to put
       its ports back, or opening a board would rename a slot. Opened by
       hand, WITHOUT the wait ouvrir() does. */
    remove("voice-slots.bin");
    banc_courant = &b;
    memset(&b, 0, sizeof(b));
    b.d = lv2_descriptor(0u);
    b.h = b.d->instantiate(b.d, 48000.0, ".", features_map);
    b.n_ch = 1u; b.n_audio = 2u; b.bloc = 128u; b.sr = 48000.0;
    for (uint32_t c = 0; c < b.n_ch; ++c) {
        b.in[c]  = (float*)calloc(b.bloc, sizeof(float));
        b.out[c] = (float*)calloc(b.bloc, sizeof(float));
        b.d->connect_port(b.h, c, b.in[c]);
        b.d->connect_port(b.h, b.n_ch + c, b.out[c]);
    }
    for (int i = 0; i < (int)CTL_COUNT; ++i) {
        b.ctl[i] = ctl_spec[i].def;
        b.d->connect_port(b.h, b.n_audio + (uint32_t)i, &b.ctl[i]);
    }
    b.d->activate(b.h);
    b.ctl[CTL_WEB_SLOT]   = 1.0f;
    b.ctl[CTL_WEB_CHAR]   = (float)'X';
    b.ctl[CTL_WEB_STROBE] = 4.0f;
    silence(&b); tourner(&b);
    b.ctl[CTL_WEB_CHAR]   = (float)'Y';
    b.ctl[CTL_WEB_STROBE] = 5.0f;
    silence(&b); tourner(&b);
    verifie_vrai("a board being restored types nothing at all",
                 !((Voice*)b.h)->user[0].name[0]);

    /* and two seconds later the same three codes do type */
    while (((Voice*)b.h)->settle_left) { silence(&b); tourner(&b); }
    taper_nom(&b, 1, "OK");
    verifie_vrai("two seconds later the page can type again",
                 !strcmp(((Voice*)b.h)->user[0].name, "OK"));
    fermer(&b);

    /* A name is a thing the player typed: it goes to the disc on its own,
       rather than waiting for the pedalboard to be saved - which on stage
       may be never. Half a second after the last letter, so a seven
       letter word is one write of the file and not seven. */
    remove("voice-slots.bin");
    ouvrir(&b, 0, 48000.0, 128, 0);
    neutre(&b);
    taper_nom(&b, 3, "GROWL");
    verifie_vrai("nothing on the disc while the letters are still coming",
                 fopen("voice-slots.bin", "rb") == NULL);
    /* six tenths of a second, and no more typing */
    for (int k = 0; k < 230; ++k) { silence(&b); tourner(&b); }
    FILE* fw = fopen("voice-slots.bin", "rb");
    verifie_vrai("and the file written once the typing stops", fw != NULL);
    if (fw) { fclose(fw); }
    fermer(&b);

    garder_disque = 1;
    ouvrir(&b, 0, 48000.0, 128, 0);
    garder_disque = 0;
    verifie_vrai("and the name is still there in a fresh instance",
                 !strcmp(((Voice*)b.h)->user[2].name, "GROWL"));
    fermer(&b);

    /* and a pedalboard closed before that half second is up does not
       lose the name: the way out writes what the clock had not */
    remove("voice-slots.bin");
    ouvrir(&b, 0, 48000.0, 128, 0);
    neutre(&b);
    taper_nom(&b, 6, "LAST");
    fermer(&b);                            /* straight away, mid-countdown */
    garder_disque = 1;
    ouvrir(&b, 0, 48000.0, 128, 0);
    garder_disque = 0;
    verifie_vrai("a name typed and closed on at once is not lost",
                 !strcmp(((Voice*)b.h)->user[5].name, "LAST"));
    fermer(&b);
}

/* The bug a singer found on stage: type CHORUS into a favourite, press
   SAVE to keep the sound, and the favourite comes back called VERSE.

   Two buffers held a name, and SAVE read the wrong one. The WORD list
   and the pedal's editor shared one; the boxes in the web page wrote the
   slot directly. So a word picked once - or restored with a pedalboard,
   or merely passed over while browsing the favourites on the pedal - sat
   in that shared buffer for the rest of the session and stamped itself
   on every slot saved afterwards.

   Saving a sound is not renaming it. SAVE no longer touches the name at
   all, and the WORD list names the slot the moment it is turned. */
static void essai_nom_pas_ecrase(void)
{
    Banc b;
    ouvrir(&b, 0, 48000.0, 128, 0);
    neutre(&b);

    /* the word list, turned once, on the slot USER SLOT points at */
    b.ctl[CTL_USER_SLOT] = 2.0f;
    b.ctl[CTL_SLOT_NAME] = 2.0f;                       /* VERSE */
    silence(&b); tourner(&b);
    verifie_vrai("a word from the list names the slot as it is turned",
                 !strcmp(((Voice*)b.h)->user[1].name, "VERSE"));

    /* the player then types a real name into the same slot */
    taper_nom(&b, 2, "CHORUS");
    verifie_vrai("and typing over it wins, being the later word",
                 !strcmp(((Voice*)b.h)->user[1].name, "CHORUS"));

    /* ...and keeps the sound. THIS is the press that used to rename it */
    b.ctl[CTL_DELAY_TIME] = 480.0f;
    silence(&b); tourner(&b);
    b.ctl[CTL_SAVE] = 1.0f; silence(&b); tourner(&b);
    b.ctl[CTL_SAVE] = 0.0f; silence(&b); tourner(&b);
    verifie_vrai("SAVE keeps the sound",
                 ((Voice*)b.h)->user[1].value[program_col[CTL_DELAY_TIME]]
                 > 479.0f);
    verifie_vrai("and does not rename the slot behind the player's back",
                 !strcmp(((Voice*)b.h)->user[1].name, "CHORUS"));

    /* the same, the other way round: a slot named from the page, then a
       word picked for ANOTHER slot, then a save on the first */
    taper_nom(&b, 4, "SOLO");
    b.ctl[CTL_USER_SLOT] = 6.0f;
    b.ctl[CTL_SLOT_NAME] = 1.0f;                       /* INTRO, on slot 6 */
    silence(&b); tourner(&b);
    b.ctl[CTL_USER_SLOT] = 4.0f;
    silence(&b); tourner(&b);
    b.ctl[CTL_SAVE] = 1.0f; silence(&b); tourner(&b);
    b.ctl[CTL_SAVE] = 0.0f; silence(&b); tourner(&b);
    verifie_vrai("a word picked for one slot stays on that slot",
                 !strcmp(((Voice*)b.h)->user[5].name, "INTRO"));
    verifie_vrai("and does not follow the next save somewhere else",
                 !strcmp(((Voice*)b.h)->user[3].name, "SOLO"));

    /* browsing the favourites on the pedal fills the editor's buffer with
       whatever it passes over. That used to leak into the next save too. */
    b.ctl[CTL_PROGRAM] = (float)(N_PROGRAM + 5);       /* USER 6, INTRO */
    for (int k = 0; k < 40; ++k) { silence(&b); tourner(&b); }
    b.ctl[CTL_USER_SLOT] = 3.0f;
    silence(&b); tourner(&b);
    b.ctl[CTL_SAVE] = 1.0f; silence(&b); tourner(&b);
    b.ctl[CTL_SAVE] = 0.0f; silence(&b); tourner(&b);
    verifie_vrai("and a favourite looked at does not name the next one saved",
                 !((Voice*)b.h)->user[2].name[0]);
    fermer(&b);

    /* A word reaches the disc on its own, without a save: a name the
       player has to remember to keep is a name they lose. */
    b.ctl[CTL_USER_SLOT] = 1.0f;
    ouvrir(&b, 0, 48000.0, 128, 0);
    neutre(&b);
    b.ctl[CTL_USER_SLOT] = 1.0f;
    b.ctl[CTL_SLOT_NAME] = 5.0f;                       /* SOLO */
    silence(&b); tourner(&b);
    fermer(&b);
    garder_disque = 1;
    ouvrir(&b, 0, 48000.0, 128, 0);
    garder_disque = 0;
    verifie_vrai("a word picked is on the disc without a save",
                 !strcmp(((Voice*)b.h)->user[0].name, "SOLO"));
    fermer(&b);

    /* And nothing during the two seconds a pedalboard takes to put its
       ports back, or every opening would rename a slot. Opened by hand,
       WITHOUT the wait ouvrir() does. */
    remove("voice-slots.bin");
    banc_courant = &b;
    memset(&b, 0, sizeof(b));
    b.d = lv2_descriptor(0u);
    b.h = b.d->instantiate(b.d, 48000.0, ".", features_map);
    b.n_ch = 1u; b.n_audio = 2u; b.bloc = 128u; b.sr = 48000.0;
    for (uint32_t c = 0; c < b.n_ch; ++c) {
        b.in[c]  = (float*)calloc(b.bloc, sizeof(float));
        b.out[c] = (float*)calloc(b.bloc, sizeof(float));
        b.d->connect_port(b.h, c, b.in[c]);
        b.d->connect_port(b.h, b.n_ch + c, b.out[c]);
    }
    for (int i = 0; i < (int)CTL_COUNT; ++i) {
        b.ctl[i] = ctl_spec[i].def;
        b.d->connect_port(b.h, b.n_audio + (uint32_t)i, &b.ctl[i]);
    }
    b.d->activate(b.h);
    /* the pedalboard had WORD stored on VERSE, as a saved port value */
    b.ctl[CTL_USER_SLOT] = 2.0f;
    b.ctl[CTL_SLOT_NAME] = 2.0f;
    for (int k = 0; k < 20; ++k) { silence(&b); tourner(&b); }
    verifie_vrai("a board being restored names nothing",
                 !((Voice*)b.h)->user[1].name[0]);
    while (((Voice*)b.h)->settle_left) { silence(&b); tourner(&b); }
    b.ctl[CTL_SLOT_NAME] = 3.0f;                       /* CHORUS */
    silence(&b); tourner(&b);
    verifie_vrai("two seconds later the list names again",
                 !strcmp(((Voice*)b.h)->user[1].name, "CHORUS"));
    fermer(&b);
}

/* The two things a MOD host does that nothing here used to ask for, and
   between them the whole of "the saves do not work".

   One: a plugin may not write its own control inputs, but it may ASK -
   mod-host implements the kx change-request feature. That is how picking
   a sound moves the encoders and the web page instead of leaving them on
   the sound before it, which is what made a save look like it had done
   nothing.

   Two: the LV2 state travels with a pedalboard, and reaches the disc when
   the BOARD is saved. Press SAVE, walk away, and it was gone. The slots
   are written to a file beside the bundle the moment SAVE is pressed. */
static void essai_hote(void)
{
    Banc b;
    n_requetes = 0;
    ouvrir(&b, 0, 48000.0, 128, 0);
    neutre(&b);
    const int p_choir = programme_nomme("CHOIR");
    const float attendu = program_value[p_choir][program_col[CTL_DOUBLER]];

    b.ctl[CTL_DOUBLER] = 0.0f;
    silence(&b); tourner(&b);
    b.ctl[CTL_PROGRAM] = (float)p_choir;
    silence(&b); tourner(&b);
    verifie("picking a sound moves the knob to what it holds",
            (double)b.ctl[CTL_DOUBLER], (double)attendu, 0.01);
    verifie_vrai("which took asking the host", n_requetes > 0);

    /* and the knob still wins afterwards */
    b.ctl[CTL_DOUBLER] = 7.0f;
    silence(&b); tourner(&b);
    silence(&b); tourner(&b);
    verifie("a knob turned after that is still the player's",
            (double)b.ctl[CTL_DOUBLER], 7.0, 0.01);
    fermer(&b);

    /* a host that declines every request: the sound must be right anyway */
    refus_requetes = 1;
    ouvrir(&b, 0, 48000.0, 128, 0);
    neutre(&b);
    b.ctl[CTL_DELAY_TIME] = 111.0f;
    b.ctl[CTL_PROGRAM] = (float)programme_nomme("BALLAD");
    silence(&b); tourner(&b);
    verifie("a host that refuses to move the knobs still gets the sound",
            (double)b.ctl[CTL_TIME_OUT],
            (double)program_value[programme_nomme("BALLAD")]
                                 [program_col[CTL_DELAY_TIME]], 0.01);
    fermer(&b);
    refus_requetes = 0;

    /* a host with neither feature - an ordinary LV2 host - is the case
       this plugin shipped with, and must keep working */
    ouvrir_avec(&b, 0, 48000.0, 128, features_nues);
    neutre(&b);
    b.ctl[CTL_PROGRAM] = (float)p_choir;
    silence(&b); tourner(&b);
    verifie("with no such feature at all the sound is still right",
            (double)b.ctl[CTL_TIME_OUT],
            (double)program_value[p_choir][program_col[CTL_DELAY_TIME]], 0.01);
    fermer(&b);

    /* SAVE reaches the disc without the pedalboard being saved */
    remove("voice-slots.bin");
    ouvrir(&b, 0, 48000.0, 128, 0);
    neutre(&b);
    b.ctl[CTL_USER_SLOT]  = 4.0f;
    b.ctl[CTL_SLOT_NAME]  = 5.0f;               /* SOLO */
    b.ctl[CTL_DELAY_TIME] = 275.0f;
    silence(&b); tourner(&b);
    b.ctl[CTL_SAVE] = 1.0f; silence(&b); tourner(&b);
    b.ctl[CTL_SAVE] = 0.0f; silence(&b); tourner(&b);
    fermer(&b);                                  /* no state save anywhere */

    FILE* f = fopen("voice-slots.bin", "rb");
    verifie_vrai("SAVE writes the slots to a file there and then", f != NULL);
    if (f) { fclose(f); }

    garder_disque = 1;                           /* a new instance, same disc */
    ouvrir(&b, 0, 48000.0, 128, 0);
    neutre(&b);
    garder_disque = 0;
    b.ctl[CTL_PROGRAM] = (float)(N_PROGRAM + 3); /* USER 4 */
    silence(&b); tourner(&b);
    verifie("and a fresh instance plays them",
            (double)b.ctl[CTL_TIME_OUT], 275.0, 0.01);
    verifie_vrai("with the name they were given",
                 !strcmp(((Voice*)b.h)->user[3].name, "SOLO"));
    fermer(&b);
    remove("voice-slots.bin");
}

/* The pedal page: three encoders, one page, the loop a singer needs
   between two songs. An encoder is read in DETENTS - the plugin counts
   clicks, never a position - which is what lets one knob change meaning
   without any value jumping when it does. */

/* One click of a knob, the size the Dwarf sends by default (201 steps
   over the course). Two clicks closer than FLICK_BLOCKS are a flick and
   count five, so a plain click is spaced out - a test that flicks by
   accident measures the wrong thing. */
static void passer_popup(Banc* b)
{
    Voice* v = (Voice*)b->h;
    /* A program written to the port is only seen when a block runs, and
       the popup goes out a screen pass later: wait for it to appear, */
    for (int k = 0; k < 200 && !v->popup_want && !v->popup_freeze; ++k) {
        silence(b); tourner(b);
    }
    /* then for the two seconds during which it owns the screen, */
    while (v->popup_want || v->popup_freeze) { silence(b); tourner(b); }
    /* then long enough for the screen to be painted again. */
    for (int k = 0; k < 40; ++k) { silence(b); tourner(b); }
}

static void cran_espace(Banc* b, int ctl, int sens, int vite)
{
    float v = b->ctl[ctl] + (float)sens * 0.005f;
    if (v < 0.0f) { v = 0.0f; }
    if (v > 1.0f) { v = 1.0f; }
    b->ctl[ctl] = v;
    silence(b); tourner(b);
    if (!vite) {
        for (uint32_t k = 0; k < FLICK_BLOCKS + 2u; ++k) { silence(b); tourner(b); }
    }
}

static void cran(Banc* b, int ctl, int sens) { cran_espace(b, ctl, sens, 0); }

static void essai_page(void)
{
    Banc b;
    ouvrir(&b, 0, 48000.0, 128, 0);
    neutre(&b);
    Voice* v = (Voice*)b.h;

    /* --- encoder 1 walks the favourites, and the sound follows ------- */
    cran(&b, CTL_ENC_SLOT, +1);
    verifie("one click of encoder 1 lands on the first USER slot",
            (double)b.ctl[CTL_PROGRAM_NOW], (double)N_PROGRAM, 0.01);
    cran(&b, CTL_ENC_SLOT, +1);
    verifie("the next click on the second",
            (double)b.ctl[CTL_PROGRAM_NOW], (double)(N_PROGRAM + 1), 0.01);
    cran(&b, CTL_ENC_SLOT, -1);
    verifie("and back again the other way",
            (double)b.ctl[CTL_PROGRAM_NOW], (double)N_PROGRAM, 0.01);
    cran(&b, CTL_ENC_SLOT, -1);
    verifie("the list wraps rather than stopping",
            (double)b.ctl[CTL_PROGRAM_NOW], (double)(N_PROGRAM + N_USER - 1), 0.01);
    cran(&b, CTL_ENC_SLOT, +1);

    /* --- encoder 2 walks the parameters ----------------------------- */
    verifie("the page starts on NAME", (double)b.ctl[CTL_PARAM_NOW], 0.0, 0.01);
    cran(&b, CTL_ENC_PARAM, +1);
    verifie("one click moves to the first parameter",
            (double)b.ctl[CTL_PARAM_NOW], 1.0, 0.01);
    cran(&b, CTL_ENC_PARAM, -1);
    verifie("and the list wraps back to NAME",
            (double)b.ctl[CTL_PARAM_NOW], 0.0, 0.01);

    /* --- encoder 3 changes the parameter, and writes it back --------- */
    int p_reverb = -1;
    for (int i = 0; i < N_PROGRAM_COL; ++i) {
        if (param_spec[i].ctl == CTL_REVERB_MIX) { p_reverb = i + 1; }
    }
    verifie_vrai("REVERB MIX is on the list", p_reverb > 0);
    for (int k = 0; k < 400 && (int)(b.ctl[CTL_PARAM_NOW] + 0.5f) != p_reverb; ++k) {
        cran(&b, CTL_ENC_PARAM, +1);
    }
    verifie("encoder 2 reaches REVERB MIX",
            (double)b.ctl[CTL_PARAM_NOW], (double)p_reverb, 0.01);
    const float avant = b.ctl[CTL_REVERB_MIX];
    cran(&b, CTL_ENC_VALUE, +1);
    verifie_vrai("one click of encoder 3 moves that parameter",
                 b.ctl[CTL_REVERB_MIX] > avant);
    verifie("and by exactly one step of it",
            (double)b.ctl[CTL_REVERB_MIX] - (double)avant,
            (double)param_spec[p_reverb - 1].step, 0.001);

    /* changing the effect the page points at moves nothing: a detent
       reader has no position to inherit */
    const float garde = b.ctl[CTL_REVERB_MIX];
    for (int k = 0; k < 5; ++k) { cran(&b, CTL_ENC_PARAM, +1); }
    verifie("walking away from a parameter leaves it where it was",
            (double)b.ctl[CTL_REVERB_MIX], (double)garde, 0.001);

    /* --- the name editor -------------------------------------------- */
    for (int k = 0; k < 400 && (int)(b.ctl[CTL_PARAM_NOW] + 0.5f) != 0; ++k) {
        cran(&b, CTL_ENC_PARAM, +1);
    }
    verifie_vrai("the page is on NAME again", v->page_mode == 0);
    cran(&b, CTL_ENC_VALUE, +1);
    verifie_vrai("encoder 3 opens the name editor on NAME", v->page_mode == 1);

    /* S, O, L, O */
    static const char* mot = "SOLO";
    for (int i = 0; mot[i]; ++i) {
        int cible = 0;
        for (int k = 0; k < N_ALPHA; ++k) { if (alpha[k] == mot[i]) cible = k; }
        for (int k = 0; k < 400 && v->page_letter != cible; ++k) {
            cran(&b, CTL_ENC_PARAM, +1);
        }
        if (mot[i + 1]) { cran(&b, CTL_ENC_SLOT, +1); }
    }
    if (strcmp(v->page_buf, "SOLO")) {
        printf("    (buf='%s' mode=%d pos=%d param=%d slot=%d)\n",
               v->page_buf, v->page_mode, v->page_pos, v->page_param,
               v->program - N_PROGRAM);
    }
    verifie_vrai("the letters are typed", !strcmp(v->page_buf, "SOLO"));

    cran(&b, CTL_ENC_VALUE, +1);            /* right: keep it */
    verifie_vrai("turning encoder 3 right keeps the name", v->page_mode == 0);
    const int slot_ici = v->program - N_PROGRAM;
    verifie_vrai("and the slot has it",
                 slot_ici >= 0 && !strcmp(v->user[slot_ici].name, "SOLO"));

    /* --- and turning it left puts the old one back ------------------- */
    cran(&b, CTL_ENC_VALUE, +1);            /* open again */
    verifie_vrai("the editor opens on the name that is there",
                 !strcmp(v->page_buf, "SOLO"));
    int cible_a = 0;
    for (int k = 0; k < N_ALPHA; ++k) { if (alpha[k] == 'A') cible_a = k; }
    for (int k = 0; k < 400 && v->page_letter != cible_a; ++k) {
        cran(&b, CTL_ENC_PARAM, +1);
    }
    verifie_vrai("a letter is changed", strcmp(v->page_buf, "SOLO") != 0);
    cran(&b, CTL_ENC_VALUE, -1);            /* left: no */
    verifie_vrai("turning it left leaves the editor", v->page_mode == 0);
    verifie_vrai("and puts the name back",
                 !strcmp(v->user[slot_ici].name, "SOLO"));

    /* --- a mode nobody touches gives up ----------------------------- */
    cran(&b, CTL_ENC_VALUE, +1);
    verifie_vrai("the editor is open once more", v->page_mode == 1);
    for (uint32_t k = 0; k < PAGE_GIVE_UP + 10u; ++k) { silence(&b); tourner(&b); }
    verifie_vrai("and lets go on its own after a while", v->page_mode == 0);
    verifie_vrai("without changing the name",
                 !strcmp(v->user[slot_ici].name, "SOLO"));
    fermer(&b);
}

/* A snapshot recall moves a knob further than a hand can, and must move
   nothing at all - otherwise loading a pedalboard rewrites the sound. */
static void essai_page_saut(void)
{
    Banc b;
    ouvrir(&b, 0, 48000.0, 128, 0);
    neutre(&b);
    const float avant = b.ctl[CTL_REVERB_MIX];
    for (int k = 0; k < 40 && (int)(b.ctl[CTL_PARAM_NOW] + 0.5f) == 0; ++k) {
        cran(&b, CTL_ENC_PARAM, +1);
    }

    b.ctl[CTL_ENC_VALUE] = 0.95f;      /* not a hand: half the course */
    silence(&b); tourner(&b);
    verifie("a jump bigger than a hand moves nothing",
            (double)b.ctl[CTL_REVERB_MIX], (double)avant, 0.001);

    /* and a flick - two clicks in a row - counts five steps */
    while ((int)(b.ctl[CTL_PARAM_NOW] + 0.5f)
           != (int)(b.ctl[CTL_PARAM_NOW] + 0.5f)) { }
    fermer(&b);
}

/* Loading a pedalboard reinstalls the saved value of every port. A
   trigger restored to 1 reads as a press: a save fired at every opening,
   over whatever slot happened to be selected. Nothing counts as a press
   for the first two seconds. */
static void essai_reveil(void)
{
    Banc b;
    /* opened by hand, WITHOUT the wait ouvrir() does */
    banc_courant = &b;
    memset(&b, 0, sizeof(b));
    b.d = lv2_descriptor(0u);
    b.h = b.d->instantiate(b.d, 48000.0, ".", features_map);
    b.n_ch = 1u; b.n_audio = 2u; b.bloc = 128u; b.sr = 48000.0;
    for (uint32_t c = 0; c < b.n_ch; ++c) {
        b.in[c]  = (float*)calloc(b.bloc, sizeof(float));
        b.out[c] = (float*)calloc(b.bloc, sizeof(float));
        b.d->connect_port(b.h, c, b.in[c]);
        b.d->connect_port(b.h, b.n_ch + c, b.out[c]);
    }
    for (int i = 0; i < (int)CTL_COUNT; ++i) {
        b.ctl[i] = ctl_spec[i].def;
        b.d->connect_port(b.h, b.n_audio + (uint32_t)i, &b.ctl[i]);
    }
    /* the pedalboard had SAVE stored high, as a saved port value */
    b.ctl[CTL_SAVE] = 1.0f;
    b.ctl[CTL_USER_SLOT] = 2.0f;
    b.d->activate(b.h);
    for (int k = 0; k < 20; ++k) { silence(&b); tourner(&b); }
    verifie_vrai("a trigger restored high at load is not a press",
                 ((Voice*)b.h)->user[1].filled == 0u);

    /* and once awake, the same press works */
    while (((Voice*)b.h)->settle_left) { silence(&b); tourner(&b); }
    b.ctl[CTL_SAVE] = 0.0f; silence(&b); tourner(&b);
    b.ctl[CTL_SAVE] = 1.0f; silence(&b); tourner(&b);
    verifie_vrai("two seconds later it is a press again",
                 ((Voice*)b.h)->user[1].filled == 1u);
    fermer(&b);
}

/* ================================================================== */
/* The choir                                                           */
/* ================================================================== */

static void essai_choeur(void)
{
    const double sr = 48000.0;
    const uint32_t N = 128;
    const size_t total = (size_t)(sr * 8.0);
    float* out = (float*)malloc(total * sizeof(float));
    double cote[2] = { 0.0, 0.0 };
    double centre[2] = { 0.0, 0.0 };

    for (int cas = 0; cas < 2; ++cas) {
        Banc b;
        ouvrir(&b, 0, sr, N, 0);
        neutre(&b);
        b.ctl[CTL_DOUBLER] = 100.0f;
        b.ctl[CTL_VOICES]  = 4.0f;
        b.ctl[CTL_SPREAD]  = cas ? 100.0f : 0.0f;

        double ph = 0.0;
        const double w = 2.0 * PI * 220.0 / sr;
        size_t done = 0;
        while (done + N <= total) {
            for (uint32_t i = 0; i < N; ++i) {
                b.in[0][i] = (float)(0.25 * sin(ph + w * (double)i));
            }
            ph += w * (double)N;
            tourner(&b);
            memcpy(out + done, b.out[0], N * sizeof(float));
            done += N;
        }
        fermer(&b);

        const size_t saute = (size_t)(sr * 2.0);
        const size_t n = total - saute;
        centre[cas] = energie_a(out + saute, n, 220.0, sr);
        /* what a detuned voice puts either side of the lead */
        cote[cas] = energie_a(out + saute, n, 216.0, sr)
                  + energie_a(out + saute, n, 224.0, sr);
    }
    free(out);

    verifie_vrai("SPREAD really does move the voices apart",
                 cote[1] > cote[0] * 10.0);
    verifie_vrai("and the lead is still the loudest thing there",
                 centre[1] > cote[1] * 4.0);
}

/* The vibrato of each voice swells and relaxes. A vibrato of fixed depth
   is the giveaway of a machine, so the depth itself rides a very slow
   sine - a third of a minute for the first voice. Sung at 220 Hz, voice
   one sits seven cents low and wobbles at 4.7 Hz, which puts a pair of
   sidebands 4.7 Hz either side of ITS carrier; how strong they are IS
   the depth. One window is not enough - the slow drift walks the carrier
   through the bins and the grains turn over every fourteen seconds, so a
   single reading is noise. Three whole swell cycles are measured in
   three-second windows and sorted into the ones near the peak and the
   ones near the trough; the peak group has to come out clearly louder.
   If it does not, the depth is constant and this comment sits over dead
   code. */
static void essai_choeur_souffle(void)
{
    const double sr = 48000.0;
    const uint32_t N = 128;
    const double duree = 75.0;
    const size_t total = (size_t)(sr * duree);
    float* out = (float*)malloc(total * sizeof(float));
    Banc b;

    ouvrir(&b, 0, sr, N, 0);
    neutre(&b);
    b.ctl[CTL_DOUBLER] = 100.0f;
    b.ctl[CTL_VOICES]  = 2.0f;
    b.ctl[CTL_SPREAD]  = 50.0f;

    double ph = 0.0;
    const double w = 2.0 * PI * 220.0 / sr;
    size_t done = 0;
    while (done + N <= total) {
        for (uint32_t i = 0; i < N; ++i) {
            b.in[0][i] = (float)(0.25 * sin(ph + w * (double)i));
        }
        ph += w * (double)N;
        tourner(&b);
        memcpy(out + done, b.out[0], N * sizeof(float));
        done += N;
    }
    fermer(&b);

    double haut = 0.0, bas = 0.0;
    int n_haut = 0, n_bas = 0;
    for (double t = 2.0; t + 3.0 < duree; t += 0.5) {
        const size_t deb = (size_t)(sr * t);
        const size_t n   = (size_t)(sr * 3.0);
        double e = 0.0;
        for (int j = -4; j <= 4; ++j) {
            const double d = 0.25 * (double)j;
            e += energie_a(out + deb, n, 214.41 + d, sr)
               + energie_a(out + deb, n, 223.81 + d, sr);
        }
        const double s = 0.775 + 0.225 * sin(2.0 * PI * 0.041 * (t + 1.5));
        if (s > 0.93)      { haut += e; ++n_haut; }
        else if (s < 0.63) { bas  += e; ++n_bas;  }
    }
    free(out);
    verifie_entre("the vibrato swells and relaxes instead of running flat",
                  (haut / (double)n_haut) / (bas / (double)n_bas),
                  1.35, 8.0);
}

/* ==================================================================
   A demo you can listen to.

   Everything else in this file measures; a doubler is judged by ear.
   "./test_voice --demo" writes voice-demo.wav: one sung phrase, played
   first dry and then through the sounds where the choir does the work,
   each announced by its own little gap. The source is not a recording -
   it is a glottal pulse train through three formants, with vibrato, a
   little jitter and a breath - but it is close enough to a voice for the
   question being asked, which is whether four copies sound like four
   people.
   ================================================================== */

typedef struct { double b1, b2, y1, y2, c, r; } Reso;

static void reso_set(Reso* f, double hz, double bw, double sr)
{
    f->y1 = f->y2 = 0.0;
    f->r  = exp(-PI * bw / sr);
    f->c  = 2.0 * f->r * cos(2.0 * PI * hz / sr);
    f->b1 = f->c;
    f->b2 = -f->r * f->r;
}

static double reso(Reso* f, double x)
{
    const double y = x + f->b1 * f->y1 + f->b2 * f->y2;
    f->y2 = f->y1;
    f->y1 = y;
    return y;
}

/* A phrase in A minor, sung on "ah": eight notes, legato, with the
   vibrato arriving a moment after each note rather than with it. */
static void chant(float* buf, size_t n, double sr)
{
    static const double notes[8] = { 220.00, 246.94, 261.63, 329.63,
                                     293.66, 261.63, 246.94, 220.00 };
    const double duree = 0.62;
    Reso f1, f2, f3;
    reso_set(&f1,  700.0,  90.0, sr);
    reso_set(&f2, 1220.0, 110.0, sr);
    reso_set(&f3, 2600.0, 160.0, sr);

    double ph = 0.0, f_liss = notes[0];
    unsigned int graine = 12345u;
    for (size_t i = 0; i < n; ++i) {
        const double t   = (double)i / sr;
        const size_t k   = (size_t)(t / duree);
        const double dans = t - (double)k * duree;
        if (k >= 8) { buf[i] = 0.0f; continue; }

        /* the note, reached rather than jumped to */
        const double vib = (dans > 0.18)
                         ? 0.02 * sin(2.0 * PI * 5.2 * (t - 0.18))
                                * (dans > 0.34 ? 1.0 : (dans - 0.18) / 0.16)
                         : 0.0;
        graine = graine * 1103515245u + 12345u;
        const double bruit = (double)((graine >> 9) & 0xFFFFu) / 32768.0 - 1.0;
        const double cible = notes[k] * (1.0 + vib + 0.0012 * bruit);
        f_liss += (cible - f_liss) * 0.0016;
        ph += 2.0 * PI * f_liss / sr;
        if (ph > 2.0 * PI) { ph -= 2.0 * PI; }

        /* a glottal pulse: harmonics falling away, not a sine */
        double src = 0.0;
        for (int h = 1; h <= 24; ++h) {
            if (f_liss * (double)h > sr * 0.45) { break; }
            src += sin(ph * (double)h) / pow((double)h, 1.15);
        }
        src *= 0.25;
        src += 0.012 * bruit;                      /* breath */

        /* attack, sustain, release, and a little push mid-note */
        const double env = (dans < 0.035) ? dans / 0.035
                         : (dans > duree - 0.10) ? (duree - dans) / 0.10
                         : 0.85 + 0.15 * sin(2.0 * PI * 1.7 * dans);

        const double v = reso(&f1, src) * 1.0
                       + reso(&f2, src) * 0.45
                       + reso(&f3, src) * 0.18;
        buf[i] = (float)(0.0045 * env * v);
    }
}

static void ecrire_wav(const char* nom, const float* x, size_t n, double sr)
{
    FILE* f = fopen(nom, "wb");
    if (!f) { printf("  *** cannot write %s\n", nom); return; }
    const unsigned int taille = (unsigned int)(n * 2u);
    const unsigned int debit  = (unsigned int)sr * 2u;
    unsigned char e[44] = {
        'R','I','F','F', 0,0,0,0, 'W','A','V','E', 'f','m','t',' ',
        16,0,0,0, 1,0, 1,0, 0,0,0,0, 0,0,0,0, 2,0, 16,0,
        'd','a','t','a', 0,0,0,0 };
    const unsigned int riff = taille + 36u, sr_i = (unsigned int)sr;
    memcpy(e + 4,  &riff,   4);
    memcpy(e + 24, &sr_i,   4);
    memcpy(e + 28, &debit,  4);
    memcpy(e + 40, &taille, 4);
    fwrite(e, 1, 44, f);
    for (size_t i = 0; i < n; ++i) {
        double v = (double)x[i];
        if (v >  0.999) { v =  0.999; }
        if (v < -0.999) { v = -0.999; }
        const short s = (short)(v * 32767.0);
        fwrite(&s, 2, 1, f);
    }
    fclose(f);
    printf("%s: %.1f s\n", nom, (double)n / sr);
}

static void demo(void)
{
    static const char* const suite[] = { "MANUAL", "TIGHT", "CHOIR",
                                         "WIDECHOR", "THIRD", "TRIO",
                                         "OCTAVES" };
    const int nb = (int)(sizeof(suite) / sizeof(suite[0]));
    const double sr = 48000.0;
    const uint32_t N = 128;
    const size_t un   = (size_t)(sr * 5.0);       /* the phrase */
    const size_t trou = (size_t)(sr * 0.8);       /* the gap after it */
    const size_t total = (size_t)nb * (un + trou);

    float* in  = (float*)calloc(un, sizeof(float));
    float* out = (float*)calloc(total, sizeof(float));
    chant(in, un, sr);

    size_t ou = 0;
    for (int k = 0; k < nb; ++k) {
        Banc b;
        ouvrir(&b, 0, sr, N, 0);
        const int p = strcmp(suite[k], "MANUAL") ? programme_nomme(suite[k]) : 0;
        if (p < 0) {
            printf("  *** no program called %s\n", suite[k]);
            fermer(&b);
            ou += un + trou;
            continue;
        }
        b.ctl[CTL_PROGRAM] = (float)p;
        printf("  %-9s program %2d\n", suite[k], p);

        size_t done = 0;
        while (done + N <= un) {
            memcpy(b.in[0], in + done, N * sizeof(float));
            tourner(&b);
            memcpy(out + ou + done, b.out[0], N * sizeof(float));
            done += N;
        }
        /* let the tails ring into the gap rather than cutting them */
        size_t reste = 0;
        while (reste + N <= trou) {
            memset(b.in[0], 0, N * sizeof(float));
            tourner(&b);
            memcpy(out + ou + un + reste, b.out[0], N * sizeof(float));
            reste += N;
        }
        fermer(&b);
        ou += un + trou;
    }
    ecrire_wav("voice-demo.wav", out, total, sr);
    free(in);
    free(out);
}

/* ================================================================== */

int main(int argc, char** argv)
{
    if (argc > 1 && !strcmp(argv[1], "--demo")) { demo(); return 0; }

    printf("Maths, against the libm this build is not allowed to link:\n");
    essai_maths();
    printf("Level:\n");                    essai_gain();
    printf("Silence:\n");                  essai_silence();
    printf("Ceiling:\n");                  essai_plafond();
    printf("Low cut:\n");                  essai_coupe_bas();
    printf("Tone:\n");                     essai_bandes();
    printf("Gate:\n");                     essai_porte();
    printf("Compressor:\n");               essai_compresseur();
    printf("De-esser:\n");                 essai_deesseur();
    printf("Drive:\n");                    essai_saturation();
    printf("Delay:\n");                    essai_retard();
    printf("Tap tempo:\n");                essai_tap();
    printf("FX switch:\n");                essai_interrupteur();
                                           essai_interrupteur_sans_clic();
    printf("One switch per effect:\n");    essai_interrupteurs_effets();
                                           essai_interrupteurs_sans_clic();
    printf("Compressor level:\n");         essai_comp_niveau();
                                           essai_boutons_sans_clic();
    printf("Preset loudness, on a sung phrase:\n"); essai_sonie_presets();
    printf("The program list:\n");          essai_programme_egale_preset();
                                           essai_programme_interrupteurs();
    printf("Doubled voices:\n");            essai_voix();
                                           essai_choeur();
                                           essai_choeur_souffle();
    printf("Anti-Larsen:\n");               essai_larsen();
                                           essai_larsen_relache();
                                           essai_larsen_rendu();
                                           essai_larsen_oubli();
    printf("Rubbish on the input:\n");      essai_non_fini();
    printf("Pitch:\n");                     essai_pitch();
    printf("Harmony:\n");                   essai_harmonie();
    printf("Mute:\n");                      essai_mute();
    printf("A/B:\n");                       essai_ab();
    printf("Names, and the cycle switch:\n"); essai_noms();
    printf("A name typed in the page:\n"); essai_nom_web();
    printf("A name nothing else may overwrite:\n"); essai_nom_pas_ecrase();
    printf("De-esser frequency:\n");        essai_deess_freq();
    printf("Tone controls:\n");             essai_eq();
    printf("Waking up:\n");                 essai_reveil();
    printf("The pedal page:\n");            essai_page();
                                           essai_page_saut();
    printf("The host's part:\n");           essai_hote();
    printf("USER slots:\n");                essai_slots();
                                           essai_retouche();
                                           essai_etat();
    printf("Screen - the list:\n");         essai_ecran_programme();
    printf("Reverb:\n");                   essai_reverb();
    printf("Doubler:\n");                  essai_doubleur();
    printf("Stereo variant:\n");           essai_stereo();
    printf("In place:\n");                 essai_sur_place();
    printf("Missing ports:\n");            essai_ports_absents();
    printf("Impossible values:\n");        essai_valeurs_impossibles();
    printf("Random:\n");                   essai_aleatoire();
    printf("Restart:\n");                  essai_activate_deux_fois();
    printf("Screen - immediate:\n");       essai_ecran_immediat();
    printf("Screen - rate and strings:\n"); essai_ecran_cadence();
    printf("Screen - tap tempo:\n");       essai_ecran_tap();
    printf("Screen - FX switch:\n");       essai_ecran_interrupteur();
    printf("Screen - compressor:\n");      essai_ecran_compresseur();
    printf("Screen - unaddressing:\n");    essai_ecran_desassignation();
    printf("Without a screen:\n");         essai_sans_ecran();

    printf("\n%s\n", echec ? "*** SOME CHECKS FAILED ***" : "All checks pass.");
    return echec;
}

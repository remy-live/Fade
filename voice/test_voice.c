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
{ (void)h; (void)a; (void)st; (void)t; (void)m; ecran.n_popup++; }

static LV2_HMI_WidgetControl widget = {
    (LV2_HMI_WidgetControl_Handle)0x1,
    sizeof(LV2_HMI_WidgetControl),
    f_led_blink, f_led_bright, f_label, f_value, f_unit, f_indic, f_popup
};

static LV2_Feature feat_hmi = { LV2_HMI__WidgetControl, &widget };
static const LV2_Feature* features[] = { &feat_hmi, NULL };

#define TOUTES_CAPS (LV2_HMI_AddressingCapability_LED       | \
                     LV2_HMI_AddressingCapability_Label     | \
                     LV2_HMI_AddressingCapability_Value     | \
                     LV2_HMI_AddressingCapability_Unit      | \
                     LV2_HMI_AddressingCapability_Indicator)

/* ---------------- the bench ---------------- */

typedef struct {
    const LV2_Descriptor* d;
    LV2_Handle            h;
    uint32_t              n_ch, n_audio, bloc;
    float*                in[MAX_CH];
    float*                out[MAX_CH];
    float                 ctl[CTL_COUNT];
    double                sr, phase;
} Banc;

static void ouvrir(Banc* b, int stereo, double sr, uint32_t bloc, int avec_ecran)
{
    memset(b, 0, sizeof(*b));
    b->d       = lv2_descriptor(stereo ? 1u : 0u);
    b->h       = b->d->instantiate(b->d, sr, ".", avec_ecran ? features : NULL);
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
    for (int i = 0; i < (int)CTL_FIRST_OUTPUT; ++i) {
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

    /* the trigger drives the same state */
    b.ctl[CTL_FX_TRIGGER] = 1.0f; silence(&b); tourner(&b);
    b.ctl[CTL_FX_TRIGGER] = 0.0f; silence(&b); tourner(&b);
    verifie("a trigger pulse flips the state back",
            (double)b.ctl[CTL_FX_STATE], 1.0, 0.001);
    b.ctl[CTL_FX_TRIGGER] = 1.0f; silence(&b); tourner(&b);
    b.ctl[CTL_FX_TRIGGER] = 0.0f; silence(&b); tourner(&b);
    verifie("and again", (double)b.ctl[CTL_FX_STATE], 0.0, 0.001);
    /* held down, it must NOT keep flipping: an edge, not a level */
    b.ctl[CTL_FX_TRIGGER] = 1.0f;
    for (int k = 0; k < 20; ++k) { silence(&b); tourner(&b); }
    verifie("holding the trigger down does not keep flipping",
            (double)b.ctl[CTL_FX_STATE], 1.0, 0.001);
    b.ctl[CTL_FX_TRIGGER] = 0.0f;
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
    for (int i = 0; i < (int)CTL_FIRST_OUTPUT; ++i) {
        b.ctl[i] = (float)NAN;
    }
    int fini = 1;
    for (int k = 0; k < 100; ++k) {
        sinus(&b, 500.0, 0.4);
        tourner(&b);
        if (!tout_fini(&b)) { fini = 0; }
    }
    verifie_vrai("a NaN on every control does not reach the audio", fini);

    for (int i = 0; i < (int)CTL_FIRST_OUTPUT; ++i) {
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
                    for (int i = 0; i < (int)CTL_FIRST_OUTPUT; ++i) {
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

    /* --- drive --- */
    b.ctl[CTL_DRIVE] = 100.0f;
    verifie_vrai("DRIVE ON: a quiet signal is lifted",
                 gain_db(&b, 800.0, 0.02, 60, 60) > 5.0);
    b.ctl[CTL_DRIVE_ON] = 0.0f;
    verifie("DRIVE OFF: unity again", gain_db(&b, 800.0, 0.02, 60, 60), 0.0, 0.1);
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
    verifie("REVERB OFF: no tail at all", crete(b.out[0], b.bloc), 0.0, 1.0e-7);
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

/* ================================================================== */
/* Presets, read out of the file that ships                            */
/* ================================================================== */

typedef struct {
    char  label[32];
    float val[CTL_COUNT];
} Preset;

static int lire_presets(Preset* p, int max)
{
    static char buf[600000];
    FILE* f = fopen("presets.ttl", "r");
    if (!f) {
        printf("  *** presets.ttl not found - run the bench from voice/\n");
        echec = 1;
        return 0;
    }
    const size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);

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

/* The complaint this test exists for: loading a preset used to be twenty
   decibels louder than not loading one. A preset chooses a SOUND; how loud
   the singer is belongs to IN GAIN and to the desk. */
static void essai_presets(void)
{
    Preset p[16];
    const int nb = lire_presets(p, 16);
    verifie_vrai("presets.ttl reads back", nb >= 6);

    for (int k = 0; k < nb; ++k) {
        Banc b;
        char quoi[160];
        ouvrir(&b, 0, 48000.0, 128, 0);
        for (int i = 0; i < (int)CTL_COUNT; ++i) { b.ctl[i] = p[k].val[i]; }
        srand(11);
        const double g = gain_bruit_db(&b, 0.22, 80, 600);
        snprintf(quoi, sizeof(quoi), "preset \"%.31s\" leaves the level alone", p[k].label);
        verifie_entre(quoi, g, -3.0, 3.0);
        verifie_vrai("  and never writes IN GAIN",
                     p[k].val[CTL_IN_GAIN] == ctl_spec[CTL_IN_GAIN].def);
        fermer(&b);
    }
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

int main(void)
{
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
    printf("Presets:\n");                  essai_presets();
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

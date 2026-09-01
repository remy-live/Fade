/* Test bench for fade.lv2.
 *
 * The important part is the SIMULATED SCREEN: a bench without an HMI
 * feature never runs the display code at all, and that is exactly where
 * the bugs hide. We plug in a fake LV2_HMI_WidgetControl that logs
 * everything, then check the rate, the strings and the indicator rule.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <lv2/core/lv2.h>
#include "lv2-hmi.h"

const LV2_Descriptor* lv2_descriptor(uint32_t index);

static int echec = 0;

static void verifie(const char* quoi, double vu, double attendu, double tol)
{
    double d = fabs(vu - attendu);
    printf("  %-46s vu=%10.3f att=%10.3f  %s\n",
           quoi, vu, attendu, (d <= tol) ? "OK" : "ECHEC");
    if (d > tol) echec = 1;
}

static void verifie_vrai(const char* quoi, int cond)
{
    printf("  %-46s %s\n", quoi, cond ? "OK" : "ECHEC");
    if (!cond) echec = 1;
}

/* ---------------- ecran simule ---------------- */

#define MAXTXT 64
typedef struct {
    int  n_label, n_value, n_unit, n_indic, n_led, n_popup;
    char dernier_label[MAXTXT];
    char dernier_value[MAXTXT];
    char dernier_unit[MAXTXT];
    float derniere_barre;
    int  couleur;
    int  trop_long;
    int  non_ascii;
    int  minuscule;
} Ecran;

static Ecran ecran;

static void note_texte(const char* s, size_t maxi)
{
    size_t i;
    if (strlen(s) > maxi) { ecran.trop_long = 1; printf("  *** string too long : \"%s\"\n", s); }
    for (i = 0; s[i]; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c > 126 || c < 32) ecran.non_ascii = 1;
        if (c >= 'a' && c <= 'z') ecran.minuscule = 1;
    }
}

static void f_led_blink(LV2_HMI_WidgetControl_Handle h, LV2_HMI_Addressing a,
                        LV2_HMI_LED_Colour c, int on, int off)
{ (void)h;(void)a;(void)c;(void)on;(void)off; ecran.n_led++; }

static void f_led_bright(LV2_HMI_WidgetControl_Handle h, LV2_HMI_Addressing a,
                         LV2_HMI_LED_Colour c, int b)
{ (void)h;(void)a; ecran.n_led++; ecran.couleur = (int)c;
  if (b == LV2_HMI_LED_Brightness_Low) {
      printf("  *** LED at Brightness_Low (colours skew on this device)\n");
      echec = 1;
  } }

static void f_label(LV2_HMI_WidgetControl_Handle h, LV2_HMI_Addressing a, const char* s)
{ (void)h;(void)a; ecran.n_label++; note_texte(s, 8);
  strncpy(ecran.dernier_label, s, MAXTXT-1); }

static void f_value(LV2_HMI_WidgetControl_Handle h, LV2_HMI_Addressing a, const char* s)
{ (void)h;(void)a; ecran.n_value++; note_texte(s, 8);
  strncpy(ecran.dernier_value, s, MAXTXT-1); }

static void f_unit(LV2_HMI_WidgetControl_Handle h, LV2_HMI_Addressing a, const char* s)
{ (void)h;(void)a; ecran.n_unit++; note_texte(s, 7);
  strncpy(ecran.dernier_unit, s, MAXTXT-1); }

static void f_indic(LV2_HMI_WidgetControl_Handle h, LV2_HMI_Addressing a, const float p)
{ (void)h;(void)a; ecran.n_indic++; ecran.derniere_barre = p;
  if (p < 0.0f || p > 1.0f) { printf("  *** bar outside [0,1] : %f\n", p); echec = 1; } }

static void f_popup(LV2_HMI_WidgetControl_Handle h, LV2_HMI_Addressing a,
                    LV2_HMI_Popup_Style st, const char* t, const char* m)
{ (void)h;(void)a;(void)st;(void)t;(void)m; ecran.n_popup++; }

static LV2_HMI_WidgetControl widget = {
    (LV2_HMI_WidgetControl_Handle)0x1,
    sizeof(LV2_HMI_WidgetControl),
    f_led_blink, f_led_bright, f_label, f_value, f_unit, f_indic, f_popup
};

static LV2_Feature feat_hmi = { LV2_HMI__WidgetControl, &widget };
static const LV2_Feature* features[] = { &feat_hmi, NULL };

/* ---------------- utilitaires ---------------- */

typedef struct {
    const LV2_Descriptor* d;
    LV2_Handle h;
    float *in1, *in2, *out;
    float sw, t12, t21, prog, avance, g1, g2, decl, etat;
    uint32_t bloc;
} Banc;

static void banc_ouvrir(Banc* b, double sr, uint32_t bloc, int avec_ecran)
{
    b->d = lv2_descriptor(0);
    b->h = b->d->instantiate(b->d, sr, ".", avec_ecran ? features : NULL);
    b->bloc = bloc;
    b->in1 = calloc(bloc, sizeof(float));
    b->in2 = calloc(bloc, sizeof(float));
    b->out = calloc(bloc, sizeof(float));
    for (uint32_t i = 0; i < bloc; ++i) { b->in1[i] = 1.0f; b->in2[i] = 0.0f; }
    b->sw = 0.0f; b->t12 = 1000.0f; b->t21 = 1000.0f; b->prog = 0.0f; b->avance = 0.0f;
    b->g1 = 0.0f; b->g2 = 0.0f;   /* 0 dB = gain unite */
    b->decl = 0.0f; b->etat = 0.0f;
    b->d->connect_port(b->h, 0, b->in1);
    b->d->connect_port(b->h, 1, b->in2);
    b->d->connect_port(b->h, 2, b->out);
    b->d->connect_port(b->h, 3, &b->sw);
    b->d->connect_port(b->h, 4, &b->t12);
    b->d->connect_port(b->h, 5, &b->t21);
    b->d->connect_port(b->h, 6, &b->prog);
    b->d->connect_port(b->h, 7, &b->avance);
    b->d->connect_port(b->h, 8, &b->g1);
    b->d->connect_port(b->h, 9, &b->g2);
    b->d->connect_port(b->h, 10, &b->decl);
    b->d->connect_port(b->h, 11, &b->etat);
    b->d->activate(b->h);
}

static void banc_fermer(Banc* b)
{
    b->d->deactivate(b->h);
    b->d->cleanup(b->h);
    free(b->in1); free(b->in2); free(b->out);
}

static long compter(Banc* b, double sr, int vers_2)
{
    long n = 0, limite = (long)(sr * 40.0);
    int fini = 0;
    while (!fini && n < limite) {
        b->d->run(b->h, b->bloc);
        for (uint32_t i = 0; i < b->bloc; ++i) {
            n++;
            if (vers_2 ? (b->out[i] <= 0.0f) : (b->out[i] >= 1.0f)) { fini = 1; break; }
        }
    }
    return n;
}

/* ---------------- essais ---------------- */

static void essai_deux_temps(double sr, uint32_t bloc, float aller, float retour)
{
    Banc b; banc_ouvrir(&b, sr, bloc, 0);
    b.t12 = aller; b.t21 = retour;

    b.d->run(b.h, bloc);
    verifie("depart sur l'entree 1", b.out[bloc-1], 1.0, 1e-6);

    b.sw = 1.0f;
    long n = compter(&b, sr, 1);
    verifie("1->2 duration follows FADE 1>2", (double)n,
            (aller <= 0.0f) ? 1.0 : aller * 0.001 * sr, 2.0);

    b.sw = 0.0f;
    n = compter(&b, sr, 0);
    verifie("2->1 duration follows FADE 2>1", (double)n,
            (retour <= 0.0f) ? 1.0 : retour * 0.001 * sr, 2.0);

    banc_fermer(&b);
}

static void essai_niveau(void)
{
    Banc b; banc_ouvrir(&b, 48000.0, 64, 0);
    for (uint32_t i = 0; i < 64; ++i) { b.in1[i] = 1.0f; b.in2[i] = 1.0f; }
    b.t12 = 100.0f; b.sw = 1.0f;
    double pire = 0.0;
    for (int k = 0; k < 200; ++k) {
        b.d->run(b.h, 64);
        for (uint32_t i = 0; i < 64; ++i) {
            double e = fabs(b.out[i] - 1.0);
            if (e > pire) pire = e;
        }
    }
    verifie("max level error during the fade", pire, 0.0, 1e-6);
    banc_fermer(&b);
}

static void essai_sortie_avancement(void)
{
    Banc b; banc_ouvrir(&b, 48000.0, 64, 0);
    b.t12 = 100.0f; b.t21 = 100.0f; b.sw = 1.0f;
    for (int k = 0; k < 200; ++k) b.d->run(b.h, 64);
    verifie("position reaches 1", b.avance, 1.0, 1e-6);
    b.sw = 0.0f;
    for (int k = 0; k < 200; ++k) b.d->run(b.h, 64);
    verifie("position returns to 0", b.avance, 0.0, 1e-6);
    banc_fermer(&b);
}

static void essai_ecran_indicateur_immediat(void)
{
    memset(&ecran, 0, sizeof(ecran));
    Banc b; banc_ouvrir(&b, 48000.0, 128, 1);

    const LV2_HMI_PluginNotification* notif =
        (const LV2_HMI_PluginNotification*)
        b.d->extension_data(LV2_HMI__PluginNotification);

    verifie_vrai("extension_data returns PluginNotification", notif != NULL);
    if (!notif) { banc_fermer(&b); return; }

    LV2_HMI_AddressingInfo info;
    memset(&info, 0, sizeof(info));
    info.caps = (LV2_HMI_AddressingCapabilities)
                (LV2_HMI_AddressingCapability_Indicator |
                 LV2_HMI_AddressingCapability_Label |
                 LV2_HMI_AddressingCapability_Value |
                 LV2_HMI_AddressingCapability_Unit);
    info.min = 0.0f; info.max = 100.0f; info.steps = 201;

    int avant = ecran.n_indic;
    notif->addressed(b.h, 6, (LV2_HMI_Addressing)0xA1, &info);
    verifie_vrai("bar sent on addressing, before any run()",
                 ecran.n_indic > avant);
    verifie_vrai("unit sent on addressing", ecran.n_unit > 0);

    banc_fermer(&b);
}

static void essai_ecran_cadence(void)
{
    memset(&ecran, 0, sizeof(ecran));
    const double sr = 48000.0;
    const uint32_t bloc = 64;          /* 750 blocs par seconde */
    Banc b; banc_ouvrir(&b, sr, bloc, 1);

    const LV2_HMI_PluginNotification* notif =
        (const LV2_HMI_PluginNotification*)
        b.d->extension_data(LV2_HMI__PluginNotification);

    LV2_HMI_AddressingInfo info;
    memset(&info, 0, sizeof(info));
    info.caps = (LV2_HMI_AddressingCapabilities)
                (LV2_HMI_AddressingCapability_Indicator |
                 LV2_HMI_AddressingCapability_Label |
                 LV2_HMI_AddressingCapability_Value |
                 LV2_HMI_AddressingCapability_Unit);
    notif->addressed(b.h, 6, (LV2_HMI_Addressing)0xA1, &info);

    LV2_HMI_AddressingInfo info_sw;
    memset(&info_sw, 0, sizeof(info_sw));
    info_sw.caps = (LV2_HMI_AddressingCapabilities)
                   (LV2_HMI_AddressingCapability_Value |
                    LV2_HMI_AddressingCapability_LED);
    notif->addressed(b.h, 3, (LV2_HMI_Addressing)0xB1, &info_sw);

    memset(&ecran, 0, sizeof(ecran));
    b.t12 = 1000.0f; b.sw = 1.0f;
    for (int k = 0; k < 750; ++k) b.d->run(b.h, bloc);

    int total = ecran.n_label + ecran.n_value + ecran.n_unit + ecran.n_indic;
    printf("    (1 s of fading: %d screen writes, %d bars, %d LEDs)\n",
           total, ecran.n_indic, ecran.n_led);
    verifie_vrai("fewer than 120 screen writes per second", total < 120);
    verifie_vrai("fewer than 40 bar updates per second", ecran.n_indic <= 40);

    memset(&ecran, 0, sizeof(ecran));
    for (int k = 0; k < 750; ++k) b.d->run(b.h, bloc);
    total = ecran.n_label + ecran.n_value + ecran.n_unit + ecran.n_indic;
    printf("    (1 s idle: %d screen writes)\n", total);
    verifie_vrai("idle: between 1 and 12 writes per second",
                 total >= 1 && total <= 12);

    verifie_vrai("no string beyond the screen limits", !ecran.trop_long);
    verifie_vrai("no non-ASCII character", !ecran.non_ascii);
    verifie_vrai("no lowercase (the screen is all caps)", !ecran.minuscule);
    verifie_vrai("no popup", ecran.n_popup == 0);

    banc_fermer(&b);
}

static void essai_ecran_desassignation(void)
{
    memset(&ecran, 0, sizeof(ecran));
    Banc b; banc_ouvrir(&b, 48000.0, 64, 1);

    const LV2_HMI_PluginNotification* notif =
        (const LV2_HMI_PluginNotification*)
        b.d->extension_data(LV2_HMI__PluginNotification);

    LV2_HMI_AddressingInfo info;
    memset(&info, 0, sizeof(info));
    info.caps = (LV2_HMI_AddressingCapabilities)
                (LV2_HMI_AddressingCapability_Indicator |
                 LV2_HMI_AddressingCapability_Value);
    notif->addressed(b.h, 6, (LV2_HMI_Addressing)0xA1, &info);

    notif->unaddressed(b.h, 6);
    memset(&ecran, 0, sizeof(ecran));
    b.t12 = 200.0f; b.sw = 1.0f;
    for (int k = 0; k < 1500; ++k) b.d->run(b.h, 64);

    verifie_vrai("nothing sent after unaddressed()",
                 ecran.n_indic == 0 && ecran.n_value == 0 && ecran.n_label == 0);
    banc_fermer(&b);
}

static void essai_sans_ecran(void)
{
    Banc b; banc_ouvrir(&b, 48000.0, 64, 0);
    b.t12 = 50.0f; b.sw = 1.0f;
    for (int k = 0; k < 200; ++k) b.d->run(b.h, 64);
    verifie("without HMI the fade still happens", b.out[10], 0.0, 1e-6);
    banc_fermer(&b);
}

static void essai_robustesse(void)
{
    const LV2_Descriptor* d = lv2_descriptor(0);
    LV2_Handle h = d->instantiate(d, 48000.0, ".", features);
    d->run(h, 256);                       /* aucun port branche */

    static float out[256];
    float sw = 1.0f, t12 = -5.0f, t21 = 1e9f, prog = 0, av = 0;
    d->connect_port(h, 2, out);
    d->connect_port(h, 3, &sw);
    d->connect_port(h, 4, &t12);
    d->connect_port(h, 5, &t21);
    d->connect_port(h, 6, &prog);
    d->connect_port(h, 7, &av);
    d->run(h, 256);                       /* entrees nues, temps absurdes */

    t12 = (float)strtod("nan", NULL);
    d->run(h, 256);
    printf("  %-46s %s\n", "bare ports, negative/huge/NaN times", "OK");
    d->cleanup(h);
}

static void essai_aleatoire(void)
{
    Banc b; banc_ouvrir(&b, 48000.0, 128, 1);
    const LV2_HMI_PluginNotification* notif =
        (const LV2_HMI_PluginNotification*)
        b.d->extension_data(LV2_HMI__PluginNotification);
    LV2_HMI_AddressingInfo info;
    memset(&info, 0, sizeof(info));
    info.caps = (LV2_HMI_AddressingCapabilities)0x1F;   /* toutes */
    notif->addressed(b.h, 6, (LV2_HMI_Addressing)0xA1, &info);
    notif->addressed(b.h, 3, (LV2_HMI_Addressing)0xB1, &info);

    srand(12345);
    int mauvais = 0;
    for (int k = 0; k < 40000 && !mauvais; ++k) {
        if (rand() % 20 == 0) b.sw = (float)(rand() % 2);
        if (rand() % 50 == 0) b.t12 = (float)(rand() % 12000) - 1000.0f;
        if (rand() % 50 == 0) b.t21 = (float)(rand() % 12000) - 1000.0f;
        if (rand() % 40 == 0) b.g1  = (float)(rand() % 100) - 70.0f;
        if (rand() % 40 == 0) b.g2  = (float)(rand() % 100) - 70.0f;
        if (rand() % 15 == 0) b.decl = (float)(rand() % 2);
        if (rand() % 300 == 0) { notif->unaddressed(b.h, 6);
                                 notif->addressed(b.h, 6, (LV2_HMI_Addressing)0xA1, &info); }
        b.d->run(b.h, 128);
        for (uint32_t i = 0; i < 128; ++i) {
            if (b.out[i] < -0.001f || b.out[i] > 4.01f) {   /* +12 dB = 3.98 */
                printf("  *** output out of range : %f\n", b.out[i]);
                echec = 1; mauvais = 1; break;
            }
        }
    }
    printf("  %-46s %s\n", "40000 blocks of random hammering",
           mauvais ? "ECHEC" : "OK");
    banc_fermer(&b);
}


/* La table dB->lineaire doit coller a la vraie libm. C'est la seule
   raison d'exister de la table : ne pas dependre de libm sans perdre
   en precision. On mesure l'ecart en DECIBELS, pas en rapport. */
static void essai_table_gain(void)
{
    Banc b; banc_ouvrir(&b, 48000.0, 64, 0);
    b.sw = 0.0f;                       /* on n'entend que l'entree 1 */
    for (uint32_t i = 0; i < 64; ++i) { b.in1[i] = 1.0f; }

    double pire_db = 0.0; double pire_a = 0.0;
    for (double db = -59.9; db <= 12.0001; db += 0.01) {
        b.g1 = (float)db;
        b.d->run(b.h, 64);             /* la rampe se termine dans le bloc */
        b.d->run(b.h, 64);
        double vu = b.out[63];
        double att = pow(10.0, db / 20.0);
        if (vu > 0.0) {
            double e = fabs(20.0 * log10(vu / att));
            if (e > pire_db) { pire_db = e; pire_a = db; }
        }
    }
    printf("    (max error %.5f dB, at %.2f dB)\n", pire_db, pire_a);
    verifie("gain table within 0.01 dB of libm", pire_db, 0.0, 0.01);

    /* Coupure franche au minimum. */
    b.g1 = -60.0f;
    b.d->run(b.h, 64); b.d->run(b.h, 64);
    verifie("at -60 dB the input is muted", b.out[63], 0.0, 0.0);

    /* Le maximum doit bien monter de 12 dB. */
    b.g1 = 12.0f;
    b.d->run(b.h, 64); b.d->run(b.h, 64);
    verifie("at +12 dB the gain is 3.981", b.out[63], 3.98107, 1e-3);

    banc_fermer(&b);
}

/* Chaque gain doit agir sur SON entree, et pas sur l'autre. */
static void essai_gain_par_entree(void)
{
    Banc b; banc_ouvrir(&b, 48000.0, 64, 0);
    for (uint32_t i = 0; i < 64; ++i) { b.in1[i] = 1.0f; b.in2[i] = 1.0f; }

    b.g1 = -6.0f; b.g2 = 0.0f;
    b.sw = 0.0f;                        /* entree 1 seule */
    b.d->run(b.h, 64); b.d->run(b.h, 64);
    verifie("GAIN 1 acts on input 1", b.out[63], 0.501187, 1e-4);

    b.sw = 1.0f; b.t12 = 0.0f;          /* bascule immediate sur l'entree 2 */
    b.d->run(b.h, 64); b.d->run(b.h, 64);
    verifie("GAIN 1 does NOT act on input 2", b.out[63], 1.0, 1e-4);

    b.g2 = -12.0f;
    b.d->run(b.h, 64); b.d->run(b.h, 64);
    verifie("GAIN 2 acts on input 2", b.out[63], 0.251189, 1e-4);

    banc_fermer(&b);
}

/* Un coup d'encodeur ne doit pas faire de saut brutal dans le signal. */
static void essai_gain_sans_clic(void)
{
    Banc b; banc_ouvrir(&b, 48000.0, 256, 0);
    for (uint32_t i = 0; i < 256; ++i) { b.in1[i] = 1.0f; }
    b.sw = 0.0f; b.g1 = 0.0f;
    b.d->run(b.h, 256);

    float avant = b.out[255];
    b.g1 = -40.0f;                      /* saut brutal demande */
    b.d->run(b.h, 256);

    double pire = fabs(b.out[0] - avant);
    for (uint32_t i = 1; i < 256; ++i) {
        double d = fabs(b.out[i] - b.out[i-1]);
        if (d > pire) pire = d;
    }
    printf("    (largest step between adjacent samples: %.6f)\n", pire);
    verifie("40 dB jump spread out, no hard step", pire, 0.0, 0.02);

    banc_fermer(&b);
}


/* Les deux commandes doivent piloter le MEME etat. */
static void essai_deux_commandes(void)
{
    Banc b; banc_ouvrir(&b, 48000.0, 64, 0);
    b.t12 = 0.0f; b.t21 = 0.0f;        /* bascule immediate : on lit l'etat */
    for (uint32_t i = 0; i < 64; ++i) { b.in1[i] = 1.0f; b.in2[i] = 0.0f; }

    b.d->run(b.h, 64);
    verifie("start: input 1", b.out[63], 1.0, 1e-6);
    verifie("STATE output is 0 at start", b.etat, 0.0, 1e-6);

    /* le declencheur seul fait basculer */
    b.decl = 1.0f; b.d->run(b.h, 64);
    verifie("one pulse -> input 2", b.out[63], 0.0, 1e-6);
    verifie("STATE goes to 1", b.etat, 1.0, 1e-6);

    /* le relachement ne doit PAS rebasculer */
    b.decl = 0.0f; b.d->run(b.h, 64);
    verifie("release does not flip back", b.out[63], 0.0, 1e-6);

    /* seconde impulsion : retour */
    b.decl = 1.0f; b.d->run(b.h, 64);
    verifie("second pulse -> input 1", b.out[63], 1.0, 1e-6);
    b.decl = 0.0f; b.d->run(b.h, 64);

    /* la bascule seule fait basculer aussi */
    b.sw = 1.0f; b.d->run(b.h, 64);
    verifie("toggle alone -> input 2", b.out[63], 0.0, 1e-6);
    verifie("STATE follows the toggle", b.etat, 1.0, 1e-6);

    /* et trigger takes over sur ce que la bascule a fait */
    b.decl = 1.0f; b.d->run(b.h, 64);
    verifie("trigger takes over", b.out[63], 1.0, 1e-6);
    b.decl = 0.0f;

    /* la bascule, restee a 1, ne doit PAS reimposer son etat a chaque bloc */
    for (int k = 0; k < 50; ++k) b.d->run(b.h, 64);
    verifie("idle toggle does not overwrite state", b.out[63], 1.0, 1e-6);
    verifie("STATE stays at 0", b.etat, 0.0, 1e-6);

    /* puis un changement de la bascule reprend la main a son tour */
    b.sw = 0.0f; b.d->run(b.h, 64);
    verifie("toggle change -> takes over", b.out[63], 1.0, 1e-6);
    b.sw = 1.0f; b.d->run(b.h, 64);
    verifie("and the other way round", b.out[63], 0.0, 1e-6);

    banc_fermer(&b);
}

/* Le declencheur doit respecter les DEUX durees comme la bascule. */
static void essai_declencheur_temps(void)
{
    Banc b; banc_ouvrir(&b, 48000.0, 64, 0);
    b.t12 = 250.0f; b.t21 = 1000.0f;

    b.decl = 1.0f; b.d->run(b.h, 64); b.decl = 0.0f;
    long n = compter(&b, 48000.0, 1) + 64;
    verifie("pulse out: follows FADE 1>2", (double)n, 0.250*48000.0, 70.0);

    b.decl = 1.0f; b.d->run(b.h, 64); b.decl = 0.0f;
    n = compter(&b, 48000.0, 0) + 64;
    verifie("pulse back: follows FADE 2>1", (double)n, 1.000*48000.0, 70.0);

    banc_fermer(&b);
}

int main(void)
{
    const double taux[]    = { 44100.0, 48000.0, 96000.0 };
    const uint32_t blocs[] = { 32, 128, 256 };

    printf("Two independent fade times:\n");
    for (int a = 0; a < 3; ++a)
      for (int b = 0; b < 3; ++b) {
        printf("sr=%.0f bloc=%u  aller=250 retour=2000\n", taux[a], blocs[b]);
        essai_deux_temps(taux[a], blocs[b], 250.0f, 2000.0f);
      }
    printf("time edge cases\n");
    essai_deux_temps(48000.0, 64, 0.0f, 10000.0f);
    essai_deux_temps(48000.0, 64, 10000.0f, 0.0f);

    printf("Two linked controls:\n");        essai_deux_commandes();
    printf("Trigger and fade times:\n");       essai_declencheur_temps();
    printf("Level:\n");                      essai_niveau();
    printf("Gain table:\n");               essai_table_gain();
    printf("Per-input gain:\n");             essai_gain_par_entree();
    printf("Gain without clicks:\n");              essai_gain_sans_clic();
    printf("Output for the web UI:\n");       essai_sortie_avancement();
    printf("Screen - immediate indicator:\n"); essai_ecran_indicateur_immediat();
    printf("Screen - rate and strings:\n");   essai_ecran_cadence();
    printf("Screen - unaddressing:\n");      essai_ecran_desassignation();
    printf("Without a screen:\n");                  essai_sans_ecran();
    printf("Robustness:\n");                  essai_robustesse();
    printf("Random:\n");                   essai_aleatoire();

    printf("\n%s\n", echec ? "*** SOME CHECKS FAILED ***" : "All checks pass.");
    return echec;
}

/* A stopwatch on run(), on the SAME source the Dwarf gets.
 *
 * It borrows the test bench's fake host wholesale rather than building a
 * second one: a benchmark that sets the plugin up differently from the
 * tests is a benchmark measuring a different plugin. main() is renamed
 * out of the way; the four thousand lines of checks come along unused.
 *
 * Absolute microseconds here are x86 microseconds and mean nothing for
 * the Dwarf. The SHARE does transfer, which is the only thing asked of
 * it: does removing these divisions move the needle at all.
 */
#define _POSIX_C_SOURCE 199309L
#include <time.h>

#define main main_des_tests
#include "test_voice.c"
#undef main

static double maintenant(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + 1e-9 * (double)t.tv_nsec;
}

/* Everything a favourite can have on at once: three grain shifters, four
   doubled voices, the chorus, the sixteen-band hunter, delay, reverb. */
static void charge_max(Banc* b)
{
    b->ctl[CTL_IN_GAIN] = 0.0f;
    b->ctl[CTL_LOW_CUT] = 80.0f;
    b->ctl[CTL_GATE_ON] = 1.0f;   b->ctl[CTL_GATE] = -50.0f;
    b->ctl[CTL_COMP_ON] = 1.0f;   b->ctl[CTL_COMP] = 60.0f;
    b->ctl[CTL_DE_ESS_ON] = 1.0f; b->ctl[CTL_DE_ESS] = 50.0f;
    b->ctl[CTL_EQ_ON] = 1.0f;     b->ctl[CTL_BODY] = 2.0f;
    b->ctl[CTL_PRESENCE] = 2.0f;  b->ctl[CTL_AIR] = 2.0f;
    b->ctl[CTL_DRIVE_ON] = 1.0f;  b->ctl[CTL_DRIVE] = 45.0f;
    b->ctl[CTL_PITCH_ON] = 1.0f;  b->ctl[CTL_PITCH] = -12.0f;
    b->ctl[CTL_PITCH_MIX] = 100.0f;
    b->ctl[CTL_HARM_ON] = 1.0f;   b->ctl[CTL_HARM_1] = 4.0f;
    b->ctl[CTL_HARM_2] = -5.0f;   b->ctl[CTL_HARM_MIX] = 60.0f;
    b->ctl[CTL_DOUBLER_ON] = 1.0f; b->ctl[CTL_DOUBLER] = 60.0f;
    b->ctl[CTL_VOICES] = 4.0f;    b->ctl[CTL_SPREAD] = 60.0f;
    b->ctl[CTL_MOD_ON] = 1.0f;    b->ctl[CTL_MOD] = 50.0f;
    b->ctl[CTL_MOD_SPEED] = 3.0f;
    b->ctl[CTL_FEEDBACK_ON] = 1.0f; b->ctl[CTL_FEEDBACK] = 60.0f;
    b->ctl[CTL_DELAY_ON] = 1.0f;  b->ctl[CTL_DELAY_MIX] = 30.0f;
    b->ctl[CTL_DELAY_REPEATS] = 40.0f;
    b->ctl[CTL_REVERB_ON] = 1.0f; b->ctl[CTL_REVERB] = 60.0f;
    b->ctl[CTL_REVERB_MIX] = 30.0f;
    b->ctl[CTL_FX] = 1.0f;
}

/* A favourite with nothing switched on: the same plugin, and the reason
   a meter read at rest lies. */
static void charge_min(Banc* b)
{
    charge_max(b);
    b->ctl[CTL_PITCH_ON] = 0.0f;  b->ctl[CTL_HARM_ON] = 0.0f;
    b->ctl[CTL_DOUBLER_ON] = 0.0f; b->ctl[CTL_MOD_ON] = 0.0f;
    b->ctl[CTL_DELAY_ON] = 0.0f;  b->ctl[CTL_REVERB_ON] = 0.0f;
    b->ctl[CTL_FEEDBACK_ON] = 0.0f; b->ctl[CTL_FEEDBACK] = 0.0f;
    b->ctl[CTL_DRIVE_ON] = 0.0f;
    b->ctl[CTL_PITCH] = 0.0f;
    b->ctl[CTL_HARM_1] = 0.0f; b->ctl[CTL_HARM_2] = 0.0f;
}

static double mesure(int stereo, void (*regler)(Banc*), int blocs, double* somme)
{
    Banc b;
    const uint32_t bloc = 256;
    const double   sr   = 48000.0;
    ouvrir(&b, stereo, sr, bloc, 0);
    regler(&b);
    /* warm up: the ramps have to reach their targets, the lines fill,
       and the hunter has to have decided at least once */
    for (int k = 0; k < 400; ++k) { sinus(&b, 220.0, 0.3); tourner(&b); }

    double s = 0.0;
    const double t0 = maintenant();
    for (int k = 0; k < blocs; ++k) {
        sinus(&b, 220.0, 0.3);
        tourner(&b);
        for (uint32_t i = 0; i < bloc; ++i) { s += (double)b.out[0][i]; }
    }
    const double dt = maintenant() - t0;
    fermer(&b);
    if (somme) { *somme = s; }
    return dt / (double)blocs * 1e6;      /* microseconds per block */
}

/* Where the time goes, by subtraction: the full load, then the same
   thing with one organ switched off. What the block costs is the
   difference. Crude, and it is the only attribution that needs no
   profiler on the machine that matters. */
typedef struct { const char* nom; void (*eteindre)(Banc*); } Organe;

static void sans_larsen(Banc* b)  { b->ctl[CTL_FEEDBACK_ON] = 0.0f;
                                    b->ctl[CTL_FEEDBACK] = 0.0f; }
static void sans_doubleur(Banc* b){ b->ctl[CTL_DOUBLER_ON] = 0.0f;
                                    b->ctl[CTL_DOUBLER] = 0.0f; }
static void sans_voix4(Banc* b)   { b->ctl[CTL_VOICES] = 2.0f; }
static void sans_pitch(Banc* b)   { b->ctl[CTL_PITCH_ON] = 0.0f;
                                    b->ctl[CTL_PITCH] = 0.0f; }
static void sans_harm(Banc* b)    { b->ctl[CTL_HARM_ON] = 0.0f;
                                    b->ctl[CTL_HARM_1] = 0.0f;
                                    b->ctl[CTL_HARM_2] = 0.0f; }
static void sans_reverb(Banc* b)  { b->ctl[CTL_REVERB_ON] = 0.0f;
                                    b->ctl[CTL_REVERB_MIX] = 0.0f; }
static void sans_delay(Banc* b)   { b->ctl[CTL_DELAY_ON] = 0.0f;
                                    b->ctl[CTL_DELAY_MIX] = 0.0f; }
static void sans_mod(Banc* b)     { b->ctl[CTL_MOD_ON] = 0.0f;
                                    b->ctl[CTL_MOD] = 0.0f; }
static void sans_drive(Banc* b)   { b->ctl[CTL_DRIVE_ON] = 0.0f;
                                    b->ctl[CTL_DRIVE] = 0.0f; }
static void sans_comp(Banc* b)    { b->ctl[CTL_COMP_ON] = 0.0f;
                                    b->ctl[CTL_COMP] = 0.0f; }
static void sans_deess(Banc* b)   { b->ctl[CTL_DE_ESS_ON] = 0.0f;
                                    b->ctl[CTL_DE_ESS] = 0.0f; }

static const Organe organes[] = {
    { "anti-Larsen (16 SVF)", sans_larsen },
    { "doubleur (switch)",    sans_doubleur },
    { "doubleur 4 -> 2 voix", sans_voix4 },
    { "pitch",                sans_pitch },
    { "harmonies (x2)",       sans_harm },
    { "reverbe",              sans_reverb },
    { "delay",                sans_delay },
    { "chorus",               sans_mod },
    { "drive",                sans_drive },
    { "compresseur",          sans_comp },
    { "de-esseur",            sans_deess },
};

static void (*organe_courant)(Banc*) = NULL;

static void charge_sauf(Banc* b)
{
    charge_max(b);
    if (organe_courant) { organe_courant(b); }
}

int main(void)
{
    const int blocs = 4000;
    const double budget = 256.0 / 48000.0 * 1e6;   /* µs a block is worth */

    /* three passes, keep the fastest: the slow ones are the machine, not
       the plugin */
    double max_us = 1e9, min_us = 1e9, s = 0.0;
    for (int p = 0; p < 3; ++p) {
        double u = mesure(1, charge_max, blocs, &s);
        if (u < max_us) { max_us = u; }
        u = mesure(1, charge_min, blocs, &s);
        if (u < min_us) { min_us = u; }
    }
    printf("bloc de 256 a 48 kHz, stereo, x86 - le budget d'un bloc est %.0f us\n\n",
           budget);
    printf("  tout allume      %8.1f us/bloc   %5.2f %% d'un coeur\n",
           max_us, 100.0 * max_us / budget);
    printf("  tout eteint      %8.1f us/bloc   %5.2f %% d'un coeur\n",
           min_us, 100.0 * min_us / budget);
    printf("  rapport          %8.1f x\n", max_us / min_us);
    printf("\n  ou va le temps, par soustraction du plein regime :\n");
    for (size_t o = 0; o < sizeof(organes) / sizeof(organes[0]); ++o) {
        organe_courant = organes[o].eteindre;
        double u = 1e9;
        for (int p = 0; p < 3; ++p) {
            const double v = mesure(1, charge_sauf, blocs, &s);
            if (v < u) { u = v; }
        }
        printf("    %-24s %7.1f us  ->  %5.1f us  %5.1f %%\n",
               organes[o].nom, u, max_us - u,
               100.0 * (max_us - u) / max_us);
    }
    organe_courant = NULL;
    printf("\n(somme %.3f - pour que le compilateur ne jette pas le calcul)\n", s);
    return 0;
}

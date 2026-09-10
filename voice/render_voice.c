#define _POSIX_C_SOURCE 199309L
#define main main_des_tests
#include "test_voice.c"
#undef main

/* Renders the same passage through whatever voice.c is compiled in, so
   two builds can be subtracted. Deterministic: the LFOs start at fixed
   phases, so any difference between two renders is the code. */
int main(int argc, char** argv)
{
    if (argc < 2) { return 2; }
    Banc b;
    ouvrir(&b, 1, 48000.0, 256, 0);
    b.ctl[CTL_IN_GAIN] = 0.0f;
    b.ctl[CTL_LOW_CUT] = 80.0f;
    b.ctl[CTL_GATE_ON] = 1.0f;   b.ctl[CTL_GATE] = -60.0f;
    b.ctl[CTL_COMP_ON] = 1.0f;   b.ctl[CTL_COMP] = 60.0f;
    b.ctl[CTL_DE_ESS_ON] = 1.0f; b.ctl[CTL_DE_ESS] = 50.0f;
    b.ctl[CTL_EQ_ON] = 1.0f;     b.ctl[CTL_BODY] = 2.0f;
    b.ctl[CTL_PRESENCE] = 2.0f;  b.ctl[CTL_AIR] = 2.0f;
    b.ctl[CTL_DRIVE_ON] = 1.0f;  b.ctl[CTL_DRIVE] = 45.0f;
    b.ctl[CTL_PITCH_ON] = 1.0f;  b.ctl[CTL_PITCH] = -12.0f;
    b.ctl[CTL_PITCH_MIX] = 100.0f;
    b.ctl[CTL_HARM_ON] = 1.0f;   b.ctl[CTL_HARM_1] = 4.0f;
    b.ctl[CTL_HARM_2] = -5.0f;   b.ctl[CTL_HARM_MIX] = 60.0f;
    b.ctl[CTL_DOUBLER_ON] = 1.0f; b.ctl[CTL_DOUBLER] = 100.0f;
    b.ctl[CTL_VOICES] = 4.0f;    b.ctl[CTL_SPREAD] = 60.0f;
    b.ctl[CTL_MOD_ON] = 1.0f;    b.ctl[CTL_MOD] = 50.0f;
    b.ctl[CTL_MOD_SPEED] = 3.0f;
    b.ctl[CTL_FEEDBACK_ON] = 1.0f; b.ctl[CTL_FEEDBACK] = 60.0f;
    b.ctl[CTL_DELAY_ON] = 1.0f;  b.ctl[CTL_DELAY_MIX] = 30.0f;
    b.ctl[CTL_REVERB_ON] = 1.0f; b.ctl[CTL_REVERB_MIX] = 30.0f;
    b.ctl[CTL_FX] = 1.0f;

    FILE* f = fopen(argv[1], "wb");
    /* ten seconds: long enough that the slowest drift LFO comes round
       several times, which is where a decimated detune would show */
    for (int k = 0; k < 1900; ++k) {
        /* a voice-ish signal: a fundamental, two partials, and vibrato */
        const double w = 2.0 * PI * 196.0 / 48000.0;
        for (uint32_t i = 0; i < b.bloc; ++i) {
            const double t = b.phase + w * (double)i;
            const float v = (float)(0.30 * sin(t) + 0.15 * sin(2.0 * t)
                                  + 0.07 * sin(3.0 * t + 0.4));
            for (uint32_t c = 0; c < b.n_ch; ++c) { b.in[c][i] = v; }
        }
        b.phase += w * (double)b.bloc;
        while (b.phase > 2.0 * PI) { b.phase -= 2.0 * PI; }
        tourner(&b);
        if (k >= 100) {                    /* past the settle window */
            fwrite(b.out[0], sizeof(float), b.bloc, f);
            fwrite(b.out[1], sizeof(float), b.bloc, f);
        }
    }
    fclose(f);
    fermer(&b);
    return 0;
}

/* The low-pass/high-pass routines were coded by aciddose, and he agreed on 
** using the WTFPL license for the code.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <SDL2/SDL.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#include <math.h> // sqrt(),tanf(),M_PI
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include "pt_audio.h"
#include "pt_header.h"
#include "pt_helpers.h"
#include "pt_blep.h"
#include "pt_config.h"
#include "pt_tables.h"
#include "pt_palette.h"
#include "pt_textout.h"
#include "pt_terminal.h"
#include "pt_visuals.h"
#include "pt_scopes.h"

// rounded constants to fit in floats
#define M_PI_F  3.1415927f
#define M_2PI_F 6.2831855f

typedef struct ledFilter_t
{
    float led[4];
} ledFilter_t;

typedef struct ledFilterCoeff_t
{
    float led, ledFb;
} ledFilterCoeff_t;

typedef struct voice_t
{
    volatile int8_t active;
    const int8_t *data, *newData;
    int32_t length, newLength, phase;
    float volume_f, delta_f, frac_f, lastDelta_f, lastFrac_f, panL_f, panR_f;
} paulaVoice_t;

static volatile int8_t filterFlags = FILTER_LP_ENABLED;
static int8_t amigaPanFlag, defStereoSep = 25, wavRenderingDone;
int8_t forceMixerOff = false;
static uint16_t ch1Pan, ch2Pan, ch3Pan, ch4Pan;
int32_t samplesPerTick;
static int32_t sampleCounter, maxSamplesToMix;
static float *mixBufferL_f, *mixBufferR_f;
static blep_t blep[AMIGA_VOICES], blepVol[AMIGA_VOICES];
static lossyIntegrator_t filterLo, filterHi;
static ledFilterCoeff_t filterLEDC;
static ledFilter_t filterLED;
static paulaVoice_t paula[AMIGA_VOICES];
static SDL_AudioDeviceID dev;

int8_t intMusic(void);      // defined in pt_modplayer.c
void storeTempVariables(void); // defined in pt_modplayer.c

void calcMod2WavTotalRows(void);

void setLEDFilter(uint8_t state)
{
    editor.useLEDFilter = state;

    if (editor.useLEDFilter)
        filterFlags |=  FILTER_LED_ENABLED;
    else
        filterFlags &= ~FILTER_LED_ENABLED;
}

void toggleLEDFilter(void)
{
    editor.useLEDFilter ^= 1;

    if (editor.useLEDFilter)
        filterFlags |=  FILTER_LED_ENABLED;
    else
        filterFlags &= ~FILTER_LED_ENABLED;
}

static void calcCoeffLED(float sr, float hz, ledFilterCoeff_t *filter)
{
    if (hz < (sr / 2.0f))
        filter->led = (M_2PI_F * hz) / sr;
    else
        filter->led = 1.0f;

    filter->ledFb = 0.125f + (0.125f / (1.0f - filter->led)); // Fb = 0.125 : Q ~= 1/sqrt(2) (Butterworth)
}

static void calcCoeffLossyIntegrator(float sr, float hz, lossyIntegrator_t *filter)
{
    filter->coeff[0] = tanf((M_PI_F * hz) / sr);
    filter->coeff[1] = 1.0f / (1.0f + filter->coeff[0]);
}

static void clearLossyIntegrator(lossyIntegrator_t *filter)
{
    filter->buffer[0] = 0.0f;
    filter->buffer[1] = 0.0f;
}

static void clearLEDFilter(ledFilter_t *filter)
{
    filter->led[0] = 0.0f;
    filter->led[1] = 0.0f;
    filter->led[2] = 0.0f;
    filter->led[3] = 0.0f;
}

static inline void lossyIntegratorLED(ledFilterCoeff_t filterC, ledFilter_t *filter, float *in, float *out)
{
    // left channel LED filter
    filter->led[0] += (filterC.led * (in[0] - filter->led[0])
        + filterC.ledFb * (filter->led[0] - filter->led[1]) + 1e-10f);
    filter->led[1] += (filterC.led * (filter->led[0] - filter->led[1]) + 1e-10f);
    out[0] = filter->led[1];

    // right channel LED filter
    filter->led[2] += (filterC.led * (in[1] - filter->led[2])
        + filterC.ledFb * (filter->led[2] - filter->led[3]) + 1e-10f);
    filter->led[3] += (filterC.led * (filter->led[2] - filter->led[3]) + 1e-10f);
    out[1] = filter->led[3];
}

void lossyIntegrator(lossyIntegrator_t *filter, float *in, float *out)
{
    float output;

    // left channel low-pass
    output = (filter->coeff[0] * in[0] + filter->buffer[0]) * filter->coeff[1];
    filter->buffer[0] = filter->coeff[0] * (in[0] - output) + output + 1e-10f;
    out[0] = output;

    // right channel low-pass
    output = (filter->coeff[0] * in[1] + filter->buffer[1]) * filter->coeff[1];
    filter->buffer[1] = filter->coeff[0] * (in[1] - output) + output + 1e-10f;
    out[1] = output;
}

void lossyIntegratorHighPass(lossyIntegrator_t *filter, float *in, float *out)
{
    float low[2];

    lossyIntegrator(filter, in, low);

    out[0] = in[0] - low[0];
    out[1] = in[1] - low[1];
}

// adejr: these sin/cos approximations both use a 0..1
// parameter range and have 'normalized' (1/2 = 0db) coeffs
//
// the coeffs are for LERP(x, x * x, 0.224) * sqrt(2)
// max_error is minimized with 0.224 = 0.0013012886

static float sinApx(float x)
{
    x = x * (2.0f - x);
    return (x * 1.09742972f + x * x * 0.31678383f);
}

static float cosApx(float x)
{
    x = (1.0f - x) * (1.0f + x);
    return (x * 1.09742972f + x * x * 0.31678383f);
}

void clearPaulaAndScopes(void)
{
    uint8_t i;
    paulaVoice_t *v;
    scopeChannel_t *sc;

    SDL_LockAudio();

    for (i = 0; i < AMIGA_VOICES; ++i)
    {
        v  = &paula[i];
        sc = &scope[i];

        v->active       = false;
        sc->active      = false;
        sc->retriggered = false;

        v->phase    = 0;
        v->volume_f = 0.0f;
        v->delta_f  = v->lastDelta_f = 0.0f;
        v->frac_f   = v->lastFrac_f  = 0.0f;
        v->length   = v->newLength   = 2;
        v->data     = v->newData     = NULL;
        // panL/panR are set up later

        sc->phase    = 0;
        sc->phase_f  = 0.0f;
        sc->volume   = 0;
        sc->delta_f  = 0.0f;
        sc->length   = sc->newLength   = 2; // setting these to 2 is IMPORTANT!
        sc->loopFlag = sc->newLoopFlag = false;
        sc->data     = sc->newData     = NULL;
    }

    SDL_UnlockAudio();
}

void mixerUpdateLoops(void) // updates Paula loop (+ scopes)
{
    uint8_t i;
    moduleChannel_t *ch;
    moduleSample_t *s;

    for (i = 0; i < AMIGA_VOICES; ++i)
    {
        ch = &modEntry->channels[i];

        if (ch->n_samplenum == editor.currSample)
        {
            s = &modEntry->samples[editor.currSample];

            paulaSetData(i, ch->n_start + s->loopStart);
            paulaSetLength(i, s->loopLength);
        }
    }
}

static void mixerSetVoicePan(uint8_t ch, uint16_t pan) // pan = 0..256
{
    float p;

    // proper 'normalized' equal-power panning is (assuming pan left to right):
    // L = cos(p * pi * 1/2) * sqrt(2);
    // R = sin(p * pi * 1/2) * sqrt(2);

    p = pan * (1.0f / 256.0f);

    paula[ch].panL_f = cosApx(p);
    paula[ch].panR_f = sinApx(p);
}

void mixerKillVoice(uint8_t ch)
{
    paulaVoice_t *v;
    scopeChannel_t *sc;

    v  = &paula[ch];
    sc = &scope[ch];

    v->active   = false;
    v->volume_f = 0.0f;
    sc->active  = false;
    sc->volume  = 0;

    memset(&blep[ch],    0, sizeof (blep_t));
    memset(&blepVol[ch], 0, sizeof (blep_t));
}

void turnOffVoices(void)
{
    uint8_t i;

    for (i = 0; i < AMIGA_VOICES; ++i)
        mixerKillVoice(i);

    clearLossyIntegrator(&filterLo);
    clearLossyIntegrator(&filterHi);
    clearLEDFilter(&filterLED);

    editor.tuningFlag = false;
}

void paulaRestartDMA(uint8_t ch)
{
    int32_t length;
    paulaVoice_t *v;
    scopeChannel_t *sc;

    v  = &paula[ch];
    sc = &scope[ch];

    length = v->newLength;
    if (length < 2)
        length = 2;

    v->frac_f = 0.0f;
    v->phase  = 0;
    v->data   = v->newData;
    v->length = length;
    v->active = true;

    sc->data        = sc->newData;
    sc->length      = length;
    sc->active      = true;
    sc->retriggered = true;
    sc->loopFlag    = sc->newLoopFlag;
}

void paulaSetPeriod(uint8_t ch, uint16_t period)
{
    float hz_f, audioFreq_f;
    paulaVoice_t *v;

    v = &paula[ch];

    if (period == 0)
    {
        hz_f = v->delta_f = 0.0f;
    }
    else
    {
        if (period < 113)
            period = 113;

        audioFreq_f = editor.outputFreq_f;
        if (editor.isSMPRendering)
            audioFreq_f = editor.pat2SmpHQ ? 28836.0f : 22168.0f;

        hz_f = (float)(PAULA_PAL_CLK) / period;
        v->delta_f = hz_f / audioFreq_f;
    }

    if (v->lastDelta_f == 0.0f)
        v->lastDelta_f = v->delta_f;

    scope[ch].delta_f = hz_f / (VBLANK_HZ / 1.001f); // "/ 1.001" to get real vblank hz
}

void paulaSetVolume(uint8_t ch, int8_t vol)
{
    if (vol & (1 << 6))
        vol = 0x0040;
    else
        vol &= 0x003F;

    paula[ch].volume_f = vol * (1.0f / 64.0f);
    scope[ch].volume   = 0 - vol;
}

void paulaSetLength(uint8_t ch, uint32_t len)
{
    if (len < 2)
        len = 2;

    scope[ch].newLength = paula[ch].newLength = len;
}

void paulaSetData(uint8_t ch, const int8_t *src)
{
    uint8_t smp;
    moduleSample_t *s;

    smp = modEntry->channels[ch].n_samplenum;
    PT_ASSERT(smp <= 30);
    if (smp > 30)
        smp = 30;

    s = &modEntry->samples[smp];

    if (src == NULL)
        src = &modEntry->sampleData[RESERVED_SAMPLE_OFFSET]; // dummy sample

    paula[ch].newData     = src;
    scope[ch].newData     = src;
    scope[ch].newLoopFlag = ((s->loopStart + s->loopLength) > 2) ? true : false;
}

void toggleLowPassFilter(void)
{
    if (filterFlags & FILTER_LP_ENABLED)
    {
        filterFlags &= ~FILTER_LP_ENABLED;

        displayMsg("FILTER MOD: A1200");
    }
    else
    {
        filterFlags |= FILTER_LP_ENABLED;
        clearLossyIntegrator(&filterLo);

        editor.errorMsgActive  = true;
        editor.errorMsgBlock   = false;
        editor.errorMsgCounter = 24; // medium short flash

        displayMsg("FILTER MOD: A500");
    }
}

void mixChannels(int32_t numSamples)
{
    const int8_t *dataPtr;
    int8_t tmpVol;
    uint8_t i;
    int32_t j;
    volatile float *vuMeter_f;
    float tempSample_f, tempVolume_f, mutedVol_f;
    blep_t *bSmp, *bVol;
    paulaVoice_t *v;

    memset(mixBufferL_f, 0, sizeof (float) * numSamples);
    memset(mixBufferR_f, 0, sizeof (float) * numSamples);

    for (i = 0; i < AMIGA_VOICES; ++i)
    {
        v         = &paula[i];
        bSmp      = &blep[i];
        bVol      = &blepVol[i];
        vuMeter_f = &editor.realVuMeterVolumes[i];

        mutedVol_f = -1.0f;
        if (editor.muted[i])
        {
            mutedVol_f  = v->volume_f;
            v->volume_f = 0.0f;
        }

        for (j = 0; v->active && (j < numSamples); ++j)
        {
            dataPtr = v->data;
            if (dataPtr == NULL)
            {
                tempSample_f = 0.0f;
                tempVolume_f = 0.0f;
            }
            else
            {
                tempSample_f = dataPtr[v->phase] * (1.0f / 128.0f);
                tempVolume_f = v->volume_f;
            }

            if (tempSample_f != bSmp->lastValue)
            {
                if ((v->lastDelta_f > 0.0f) && (v->lastDelta_f > v->lastFrac_f))
                    blepAdd(bSmp, v->lastFrac_f / v->lastDelta_f, bSmp->lastValue - tempSample_f);
                bSmp->lastValue = tempSample_f;
            }

            if (tempVolume_f != bVol->lastValue)
            {
                blepAdd(bVol, 0.0f, bVol->lastValue - tempVolume_f);
                bVol->lastValue = tempVolume_f;
            }

            if (bSmp->samplesLeft) tempSample_f += blepRun(bSmp);
            if (bVol->samplesLeft) tempVolume_f += blepRun(bVol);

            tempSample_f *= tempVolume_f;

            if (editor.ui.realVuMeters)
            {
                tmpVol = tempSample_f * 48.0f;
                tmpVol = ABS(tmpVol);
                if (tmpVol > *vuMeter_f)
                    *vuMeter_f = tmpVol;
            }

            mixBufferL_f[j] += (tempSample_f * v->panL_f);
            mixBufferR_f[j] += (tempSample_f * v->panR_f);

            v->frac_f += v->delta_f;
            if (v->frac_f >= 1.0f)
            {
                v->frac_f -= 1.0f;

                v->lastFrac_f  = v->frac_f;
                v->lastDelta_f = v->delta_f;

                if (++v->phase >= v->length)
                {
                    v->phase = 0;

                    // re-fetch Paula register values now
                    v->length = v->newLength;
                    v->data   = v->newData;
                }
            }
        }

        if (mutedVol_f != -1.0f)
            v->volume_f = mutedVol_f;
    }
}

void pat2SmpMixChannels(int32_t numSamples) // pat2smp needs a multi-step mixer routine (lower mix rate), otherwise identical
{
    const int8_t *dataPtr;
    uint8_t i;
    int32_t j, fracTrunc;
    float tempSample_f, tempVolume_f, mutedVol_f;
    blep_t *bSmp, *bVol;
    paulaVoice_t *v;

    memset(mixBufferL_f, 0, sizeof (float) * numSamples);
    memset(mixBufferR_f, 0, sizeof (float) * numSamples);

    for (i = 0; i < AMIGA_VOICES; ++i)
    {
        v    = &paula[i];
        bSmp = &blep[i];
        bVol = &blepVol[i];

        mutedVol_f = -1.0f;
        if (editor.muted[i])
        {
            mutedVol_f  = v->volume_f;
            v->volume_f = 0.0f;
        }

        for (j = 0; v->active && (j < numSamples); ++j)
        {
            dataPtr = v->data;
            if (dataPtr == NULL)
            {
                tempSample_f = 0.0f;
                tempVolume_f = 0.0f;
            }
            else
            {
                tempSample_f = dataPtr[v->phase] * (1.0f / 128.0f);
                tempVolume_f = v->volume_f;
            }

            if (tempSample_f != bSmp->lastValue)
            {
                if ((v->lastDelta_f > 0.0f) && (v->lastDelta_f > v->lastFrac_f))
                    blepAdd(bSmp, v->lastFrac_f / v->lastDelta_f, bSmp->lastValue - tempSample_f);
                bSmp->lastValue = tempSample_f;
            }

            if (tempVolume_f != bVol->lastValue)
            {
                blepAdd(bVol, 0.0f, bVol->lastValue - tempVolume_f);
                bVol->lastValue = tempVolume_f;
            }

            if (bSmp->samplesLeft) tempSample_f += blepRun(bSmp);
            if (bVol->samplesLeft) tempVolume_f += blepRun(bVol);

            tempSample_f *= tempVolume_f;

            mixBufferL_f[j] += (tempSample_f * v->panL_f);
            mixBufferR_f[j] += (tempSample_f * v->panR_f);

            v->frac_f += v->delta_f;
            while (v->frac_f >= 1.0f)
            {
                fracTrunc  = (int32_t)(v->frac_f);
                v->phase  += fracTrunc;
                v->frac_f -= fracTrunc;

                v->lastFrac_f  = v->frac_f;
                v->lastDelta_f = v->delta_f;

                while (v->phase >= v->length)
                {
                    v->phase -= v->length;

                    // re-fetch Paula register values now
                    v->length = v->newLength;
                    v->data   = v->newData;
                }
            }
        }

        if (mutedVol_f != -1.0f)
            v->volume_f = mutedVol_f;
    }
}

static inline void processMixedSamples(int32_t i, float *out_f, uint8_t mono)
{
    out_f[0] = mixBufferL_f[i];
    out_f[1] = mixBufferR_f[i];

    if (!editor.isSMPRendering) // don't apply filters when rendering pattern to sample
    {
        if (filterFlags & FILTER_LP_ENABLED)  lossyIntegrator(&filterLo, out_f, out_f);
        if (filterFlags & FILTER_LED_ENABLED) lossyIntegratorLED(filterLEDC, &filterLED, out_f, out_f);
    }

    lossyIntegratorHighPass(&filterHi, out_f, out_f);

    // normalize
    out_f[0] *= (32767.0f / AMIGA_VOICES);
    out_f[1] *= (32767.0f / AMIGA_VOICES);

    // clamp
    out_f[0]  = CLAMP(out_f[0], -32768.0f, 32767.0f);
    out_f[1]  = CLAMP(out_f[1], -32768.0f, 32767.0f);

    if (mono)
        out_f[0] = (out_f[0] / 2.0f) + (out_f[1] / 2.0f);
}

void outputAudio(int16_t *target, int32_t numSamples)
{
    int16_t *outStream, smpL, smpR;
    int32_t j;
    float out_f[2];

    outStream = target;
    if (editor.isWAVRendering)
    {
        // render to WAV file
        mixChannels(numSamples);
        for (j = 0; j < numSamples; ++j)
        {
            processMixedSamples(j, out_f, false);

            smpL = (int16_t)(out_f[0]);
            smpR = (int16_t)(out_f[1]);

            if (bigEndian)
            {
                smpL = SWAP16(smpL);
                smpR = SWAP16(smpR);
            }

            *outStream++ = smpL;
            *outStream++ = smpR;
        }
    }
    else if (editor.isSMPRendering)
    {
        // render to sample
        pat2SmpMixChannels(numSamples);
        for (j = 0; j < numSamples; ++j)
        {
            processMixedSamples(j, out_f, true); // mono
            smpL = (int16_t)(out_f[0]);

            if (editor.pat2SmpPos >= MAX_SAMPLE_LEN)
            {
                editor.smpRenderingDone = true;
                break;
            }
            else
            {
                editor.pat2SmpBuf[editor.pat2SmpPos++] = smpL;
            }
        }
    }
    else
    {
        // render to real audio
        mixChannels(numSamples);
        for (j = 0; j < numSamples; ++j)
        {
            processMixedSamples(j, out_f, false);

            smpL = (int16_t)(out_f[0]);
            smpR = (int16_t)(out_f[1]);

            *outStream++ = smpL;
            *outStream++ = smpR;
        }
    }
}

void audioCallback(void *userdata, uint8_t *stream, int32_t len)
{
    int16_t *out;
    int32_t sampleBlock, samplesTodo;

    (void)(userdata); // make compiler happy

    if (forceMixerOff) // for MOD2WAV
    {
        memset(stream, 0, len); // mute
        return;
    }

    out = (int16_t *)(stream);

    sampleBlock = len / 4;
    while (sampleBlock)
    {
        samplesTodo = (sampleBlock < sampleCounter) ? sampleBlock : sampleCounter;
        if (samplesTodo > 0)
        {
            outputAudio(out, samplesTodo);
            out += (2 * samplesTodo);

            sampleBlock   -= samplesTodo;
            sampleCounter -= samplesTodo;
        }
        else
        {
            if (editor.songPlaying)
                intMusic();

            sampleCounter = samplesPerTick;
        }
    }
}

static void calculateFilterCoeffs(void)
{
    double lp_R, lp_C, lp_Hz;
    double led_R1, led_R2, led_C1, led_C2, led_Hz;
    double hp_R, hp_C, hp_Hz;

    // Amiga 500 filter emulation, by aciddose (Xhip author)
    // All Amiga computers have three (!) filters, not just the "LED" filter.
    //
    // First comes a static low-pass 6dB formed by the supply current
    // from the Paula's mixture of channels A+B / C+D into the opamp with
    // 0.1uF capacitor and 360 ohm resistor feedback in inverting mode biased by
    // dac vRef (used to center the output).
    //
    // R = 360 ohm
    // C = 0.1uF
    // Low Hz = 4420.97~ = 1 / (2pi * 360 * 0.0000001)
    //
    // Under spice simulation the circuit yields -3dB = 4400Hz.
    // In the Amiga 1200 and CD-32, the low-pass cutoff is 26kHz+, so the
    // static low-pass filter is disabled in the mixer in A1200 mode.
    //
    // Next comes a bog-standard Sallen-Key filter ("LED") with:
    // R1 = 10K ohm
    // R2 = 10K ohm
    // C1 = 6800pF
    // C2 = 3900pF
    // Q ~= 1/sqrt(2)
    //
    // This filter is optionally bypassed by an MPF-102 JFET chip when
    // the LED filter is turned off.
    //
    // Under spice simulation the circuit yields -3dB = 2800Hz.
    // 90 degrees phase = 3000Hz (so, should oscillate at 3kHz!)
    //
    // The buffered output of the Sallen-Key passes into an RC high-pass with:
    // R = 1.39K ohm (1K ohm + 390 ohm)
    // C = 22uF (also C = 330nF, for improved high-frequency)
    //
    // High Hz = 5.2~ = 1 / (2pi * 1390 * 0.000022)
    // Under spice simulation the circuit yields -3dB = 5.2Hz.

    // Amiga 500 RC low-pass filter:
    lp_R  = 360.0;     // 360 ohm resistor
    lp_C  = 0.0000001; // 0.1uF capacitor
    lp_Hz = 1.0 / (2.0 * M_PI * lp_R * lp_C);
    calcCoeffLossyIntegrator(editor.outputFreq_f, (float)(lp_Hz), &filterLo);

    // Amiga 500 Sallen-Key "LED" filter:
    led_R1 = 10000.0;      // 10K ohm resistor
    led_R2 = 10000.0;      // 10K ohm resistor
    led_C1 = 0.0000000068; // 6800pF capacitor
    led_C2 = 0.0000000039; // 3900pF capacitor
    led_Hz = 1.0 / (2.0 * M_PI * sqrt(led_R1 * led_R2 * led_C1 * led_C2));
    calcCoeffLED(editor.outputFreq_f, (float)(led_Hz), &filterLEDC);

    // Amiga 500 RC high-pass filter:
    hp_R  = 1390.0;   // 1K ohm resistor + 390 ohm resistor
    hp_C  = 0.000022; // 22uF capacitor
    hp_Hz = 1.0 / (2.0 * M_PI * hp_R * hp_C);
    calcCoeffLossyIntegrator(editor.outputFreq_f, (float)(hp_Hz), &filterHi);
}

void mixerCalcVoicePans(uint8_t stereoSeparation)
{
    uint8_t scaledPanPos;

    scaledPanPos = (stereoSeparation * 128) / 100;

    ch1Pan = 128 - scaledPanPos;
    ch2Pan = 128 + scaledPanPos;
    ch3Pan = 128 + scaledPanPos;
    ch4Pan = 128 - scaledPanPos;

    mixerSetVoicePan(0, ch1Pan);
    mixerSetVoicePan(1, ch2Pan);
    mixerSetVoicePan(2, ch3Pan);
    mixerSetVoicePan(3, ch4Pan);
}

int8_t setupAudio(void)
{
    SDL_AudioSpec want, have;

    want.freq     = ptConfig.soundFrequency;
    want.format   = AUDIO_S16;
    want.channels = 2;
    want.callback = audioCallback;
    want.userdata = NULL;
    want.samples  = 1024; // should be 2^n for compatibility with all sound cards

    dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (dev == 0)
    {
        showErrorMsgBox("Unable to open audio device: %s", SDL_GetError());
        return (false);
    }

    if (have.freq < 44100) // lower than this is not safe for one-step mixer w/ BLEP
    {
        showErrorMsgBox("Unable to open audio: The audio output rate couldn't be used!");
        return (false);
    }

    if (have.format != want.format)
    {
        showErrorMsgBox("Unable to open audio: The sample format (signed 16-bit) couldn't be used!");
        return (false);
    }

    maxSamplesToMix = (int32_t)(((have.freq * 2.5) / 32.0) + 0.5);

    mixBufferL_f = (float *)(calloc(maxSamplesToMix, sizeof (float)));
    if (mixBufferL_f == NULL)
    {
        showErrorMsgBox("Out of memory!");
        return (false);
    }

    mixBufferR_f = (float *)(calloc(maxSamplesToMix, sizeof (float)));
    if (mixBufferR_f == NULL)
    {
        showErrorMsgBox("Out of memory!");
        return (false);
    }

    editor.mod2WavBuffer = (int16_t *)(malloc(sizeof (int16_t) * maxSamplesToMix));
    if (editor.mod2WavBuffer == NULL)
    {
        showErrorMsgBox("Out of memory!");
        return (false);
    }

    editor.audioBufferSize  = have.samples;
    ptConfig.soundFrequency = have.freq;
    editor.outputFreq       = ptConfig.soundFrequency;
    editor.outputFreq_f     = (float)(ptConfig.soundFrequency);

    mixerCalcVoicePans(ptConfig.stereoSeparation);
    defStereoSep = ptConfig.stereoSeparation;

    filterFlags = ptConfig.a500LowPassFilter ? FILTER_LP_ENABLED : 0;

    calculateFilterCoeffs();

    samplesPerTick = 0;
    sampleCounter  = 0;

    SDL_PauseAudioDevice(dev, false);

    return (true);
}

void audioClose(void)
{
    turnOffVoices();
    editor.songPlaying = false;

    if (dev > 0)
    {
        SDL_PauseAudioDevice(dev, true);
        SDL_CloseAudioDevice(dev);
        dev = 0;
    }

    if (mixBufferL_f != NULL)
    {
        free(mixBufferL_f);
        mixBufferL_f = NULL;
    }

    if (mixBufferR_f != NULL)
    {
        free(mixBufferR_f);
        mixBufferR_f = NULL;
    }

    if (editor.mod2WavBuffer != NULL)
    {
        free(editor.mod2WavBuffer);
        editor.mod2WavBuffer = NULL;
    }
}

void mixerSetSamplesPerTick(int32_t val)
{
    samplesPerTick = val;
}

void mixerClearSampleCounter(void)
{
    sampleCounter = 0;
}

void toggleAmigaPanMode(void)
{
    amigaPanFlag ^= 1;

    if (!amigaPanFlag)
    {
        mixerCalcVoicePans(defStereoSep);

        editor.errorMsgActive  = true;
        editor.errorMsgBlock   = false;
        editor.errorMsgCounter = 24; // medium short flash

        setStatusMessage("AMIGA PANNING OFF", NO_CARRY);
    }
    else
    {
        mixerCalcVoicePans(100);

        editor.errorMsgActive  = true;
        editor.errorMsgBlock   = false;
        editor.errorMsgCounter = 24; // medium short flash

        setStatusMessage("AMIGA PANNING ON", NO_CARRY);
    }
}

// PAT2SMP RELATED STUFF

uint32_t getAudioFrame(int16_t *outStream)
{
    int32_t b, c;

    if (intMusic() == false)
        wavRenderingDone = true;

    b = samplesPerTick;
    while (b > 0)
    {
        c = b;
        if (c > maxSamplesToMix)
            c = maxSamplesToMix;

        outputAudio(outStream, c);
        b -= c;

        outStream += (c * 2);
    }

    return (samplesPerTick * 2);
}

int32_t mod2WavThreadFunc(void *ptr)
{
    uint32_t size, totalSampleCounter, totalRiffChunkLen;
    FILE *fOut;
    wavHeader_t wavHeader;

    fOut = (FILE *)(ptr);
    if (fOut == NULL)
        return (1);

    // skip wav header place, render data first
    fseek(fOut, sizeof (wavHeader_t), SEEK_SET);

    wavRenderingDone   = false;
    totalSampleCounter = 0;

    while (editor.isWAVRendering && !(wavRenderingDone || editor.abortMod2Wav))
    {
        size = getAudioFrame(editor.mod2WavBuffer);
        totalSampleCounter += size;
        fwrite(editor.mod2WavBuffer, sizeof (int16_t), size, fOut);

        editor.ui.updateMod2WavDialog = true;
    }

    if (totalSampleCounter & 1)
        fputc(0, fOut); // pad align byte

    totalRiffChunkLen = ftell(fOut) - 8;

    editor.ui.mod2WavFinished     = true;
    editor.ui.updateMod2WavDialog = true;

    // go back and fill the missing WAV header
    fseek(fOut, 0, SEEK_SET);

    wavHeader.chunkID       = bigEndian ? SWAP32(0x46464952) : 0x46464952; // "RIFF"
    wavHeader.chunkSize     = bigEndian ? SWAP32(totalRiffChunkLen) : (totalRiffChunkLen);
    wavHeader.format        = bigEndian ? SWAP32(0x45564157) : 0x45564157; // "WAVE"
    wavHeader.subchunk1ID   = bigEndian ? SWAP32(0x20746D66) : 0x20746D66; // "fmt "
    wavHeader.subchunk1Size = bigEndian ? SWAP32(16) : 16;
    wavHeader.audioFormat   = bigEndian ? SWAP16(1) : 1;
    wavHeader.numChannels   = bigEndian ? SWAP16(2) : 2;
    wavHeader.sampleRate    = bigEndian ? SWAP32(editor.outputFreq) : editor.outputFreq;
    wavHeader.bitsPerSample = bigEndian ? SWAP16(16) : 16;
    wavHeader.byteRate      = bigEndian ? SWAP32(wavHeader.sampleRate * wavHeader.numChannels * wavHeader.bitsPerSample / 8)
                            : (wavHeader.sampleRate * wavHeader.numChannels * wavHeader.bitsPerSample / 8);
    wavHeader.blockAlign    = bigEndian ? SWAP16(wavHeader.numChannels * wavHeader.bitsPerSample / 8)
                            : (wavHeader.numChannels * wavHeader.bitsPerSample / 8);
    wavHeader.subchunk2ID   = bigEndian ? SWAP32(0x61746164) : 0x61746164; // "data"
    wavHeader.subchunk2Size = bigEndian ? SWAP32(totalSampleCounter *  4) : (totalSampleCounter *  4); // 16-bit stereo = * 4

    fwrite(&wavHeader, sizeof (wavHeader_t), 1, fOut);
    fclose(fOut);

    return (1);
}

int8_t renderToWav(char *fileName, int8_t checkIfFileExist)
{
    FILE *fOut;
    struct stat statBuffer;

    if (checkIfFileExist)
    {
        if (stat(fileName, &statBuffer) == 0)
        {
            editor.ui.askScreenShown = true;
            editor.ui.askScreenType  = ASK_MOD2WAV_OVERWRITE;

            pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
            setStatusMessage("OVERWRITE FILE?", NO_CARRY);

            renderAskDialog();

            return (false);
        }
    }

    if (editor.ui.askScreenShown)
    {
        editor.ui.askScreenShown = false;

        editor.ui.answerNo  = false;
        editor.ui.answerYes = false;
    }

    fOut = fopen(fileName, "wb");
    if (fOut == NULL)
    {
        displayErrorMsg("FILE I/O ERROR");
        terminalPrintf("MOD2WAV failed: file input/output error\n");

        return (false);
    }

    storeTempVariables();
    calcMod2WavTotalRows();
    restartSong();

    terminalPrintf("MOD2WAV started (rows to render: %d)\n", modEntry->rowsInTotal);

    editor.blockMarkFlag = false;

    pointerSetMode(POINTER_MODE_READ_DIR, NO_CARRY);
    setStatusMessage("RENDERING MOD...", NO_CARRY);

    editor.ui.disableVisualizer = true;
    editor.isWAVRendering = true;
    renderMOD2WAVDialog();

    editor.abortMod2Wav = false;
    editor.mod2WavThread = SDL_CreateThread(mod2WavThreadFunc, "mod2wav ProTracker thread", fOut);

    return (true);
}

// for MOD2WAV - ONLY used for a visual percentage counter, so accuracy is not important
void calcMod2WavTotalRows(void)
{
    // TODO: Should run replayer in a tick loop instead to get estimate..?
    // This is quite accurate though...

    int8_t n_pattpos[AMIGA_VOICES], n_loopcount[AMIGA_VOICES];
    uint8_t modRow, pBreakFlag, posJumpAssert, pBreakPosition, calcingRows, ch, pos;
    int16_t modOrder;
    uint16_t modPattern;
    note_t *note;

    // for pattern loop
    memset(n_pattpos,   0, sizeof (n_pattpos));
    memset(n_loopcount, 0, sizeof (n_loopcount));

    modEntry->rowsCounter = 0;
    modEntry->rowsInTotal = 0;

    modRow         = 0;
    modOrder       = 0;
    modPattern     = modEntry->head.order[0];
    pBreakPosition = 0;
    posJumpAssert  = false;
    pBreakFlag     = false;
    calcingRows    = true;

    memset(editor.rowVisitTable, 0, MOD_ORDERS * MOD_ROWS);
    while (calcingRows)
    {
        editor.rowVisitTable[(modOrder * MOD_ROWS) + modRow] = true;

        for (ch = 0; ch < AMIGA_VOICES; ++ch)
        {
            note = &modEntry->patterns[modPattern][(modRow * AMIGA_VOICES) + ch];
            if (note->command == 0x0B) // Bxx - Position Jump
            {
                modOrder = note->param - 1;
                pBreakPosition = 0;
                posJumpAssert  = true;
            }
            else if (note->command == 0x0D) // Dxx - Pattern Break
            {
                pBreakPosition = (((note->param >> 4) * 10) + (note->param & 0x0F));
                if (pBreakPosition > 63)
                    pBreakPosition = 0;

                posJumpAssert = true;
            }
            else if ((note->command == 0x0F) && (note->param == 0)) // F00 - Set Speed 0 (stop)
            {
                calcingRows = false;
                break;
            }
            else if ((note->command == 0x0E) && ((note->param >> 4) == 0x06)) // E6x - Pattern Loop
            {
                pos = note->param & 0x0F;
                if (pos == 0)
                {
                    n_pattpos[ch] = modRow;
                }
                else
                {
                    // this is so ugly
                    if (n_loopcount[ch] == 0)
                    {
                        n_loopcount[ch] = pos;

                        pBreakPosition = n_pattpos[ch];
                        pBreakFlag     = true;

                        for (pos = pBreakPosition; pos <= modRow; ++pos)
                            editor.rowVisitTable[(modOrder * MOD_ROWS) + pos] = false;
                    }
                    else
                    {
                        if (--n_loopcount[ch])
                        {
                            pBreakPosition = n_pattpos[ch];
                            pBreakFlag     = true;

                            for (pos = pBreakPosition; pos <= modRow; ++pos)
                                editor.rowVisitTable[(modOrder * MOD_ROWS) + pos] = false;
                        }
                    }
                }
            }
        }

        modRow++;
        modEntry->rowsInTotal++;

        if (pBreakFlag)
        {
            modRow = pBreakPosition;
            pBreakPosition = 0;
            pBreakFlag = false;
        }

        if ((modRow >= MOD_ROWS) || posJumpAssert)
        {
            modRow = pBreakPosition;
            pBreakPosition = 0;
            posJumpAssert  = false;

            modOrder = (modOrder + 1) & 0x7F;
            if (modOrder >= modEntry->head.orderCount)
            {
                modOrder = 0;
                calcingRows = false;
                break;
            }

            modPattern = modEntry->head.order[modOrder];
            if (modPattern > (MAX_PATTERNS - 1))
                modPattern =  MAX_PATTERNS - 1;
        }

        if (editor.rowVisitTable[(modOrder * MOD_ROWS) + modRow])
        {
            // row has been visited before, we're now done!
            calcingRows = false;
            break;
        }
    }
}

int8_t quantizeFloatTo8bit(float smpFloat)
{
    smpFloat = ROUND_SMP_F(smpFloat);
    smpFloat = CLAMP(smpFloat, -128.0f, 127.0f);
    return (int8_t)(smpFloat);
}

int8_t quantize32bitTo8bit(int32_t smp32)
{
    double smp_d;

    smp_d = smp32 / 16777216.0;
    smp_d = ROUND_SMP_D(smp_d);
    smp_d = CLAMP(smp_d, -128.0, 127.0);

    return (int8_t)(smp_d);
}

int8_t quantize24bitTo8bit(int32_t smp32)
{
    double smp_d;

    smp_d = smp32 / 65536.0;
    smp_d = ROUND_SMP_D(smp_d);
    smp_d = CLAMP(smp_d, -128.0, 127.0);

    return (int8_t)(smp_d);
}

int8_t quantize16bitTo8bit(int16_t smp16)
{
    double smp_d;

    smp_d = smp16 / 256.0;
    smp_d = ROUND_SMP_D(smp_d);
    smp_d = CLAMP(smp_d, -128.0, 127.0);

    return (int8_t)(smp_d);
}

void normalize32bitSigned(int32_t *sampleData, uint32_t sampleLength)
{
    uint32_t sample, sampleVolPeak, i;
    double gain;

    sampleVolPeak = 0;
    for (i = 0; i < sampleLength; ++i)
    {
        sample = ABS(sampleData[i]);
        if (sampleVolPeak < sample)
            sampleVolPeak = sample;
    }

    // prevent division by zero!
    if (sampleVolPeak <= 0)
        sampleVolPeak  = 1;

    gain = ((4294967296.0 / 2.0) - 1.0) / (double)(sampleVolPeak);
    for (i = 0; i < sampleLength; ++i)
        sampleData[i] = (int32_t)(sampleData[i] * gain);
}

void normalize24bitSigned(int32_t *sampleData, uint32_t sampleLength)
{
    uint32_t sample, sampleVolPeak, i;
    double gain;

    sampleVolPeak = 0;
    for (i = 0; i < sampleLength; ++i)
    {
        sample = ABS(sampleData[i]);
        if (sampleVolPeak < sample)
            sampleVolPeak = sample;
    }

    // prevent division by zero!
    if (sampleVolPeak <= 0)
        sampleVolPeak  = 1;

    gain = ((16777216.0 / 2.0) - 1.0) / (double)(sampleVolPeak);
    for (i = 0; i < sampleLength; ++i)
        sampleData[i] = (int32_t)(sampleData[i] * gain);
}

void normalize16bitSigned(int16_t *sampleData, uint32_t sampleLength)
{
    uint32_t sample, sampleVolPeak, i;
    float gain;

    sampleVolPeak = 0;
    for (i = 0; i < sampleLength; ++i)
    {
        sample = ABS(sampleData[i]);
        if (sampleVolPeak < sample)
            sampleVolPeak = sample;
    }

    // prevent division by zero!
    if (sampleVolPeak <= 0)
        sampleVolPeak  = 1;

    gain = ((65536.0f / 2.0f) - 1.0f) / (float)(sampleVolPeak);
    for (i = 0; i < sampleLength; ++i)
        sampleData[i] = (int16_t)(sampleData[i] * gain);
}

void normalize8bitFloatSigned(float *sampleData, uint32_t sampleLength)
{
    uint32_t i;
    float sample, sampleVolPeak, gain;

    sampleVolPeak = 0.0f;
    for (i = 0; i < sampleLength; ++i)
    {
        sample = fabsf(sampleData[i]);
        if (sampleVolPeak < sample)
            sampleVolPeak = sample;
    }

    if (sampleVolPeak > 0.0f)
    {
        gain = ((256.0f / 2.0f) - 1.0f) / sampleVolPeak;
        for (i = 0; i < sampleLength; ++i)
            sampleData[i] *= gain;
    }
}

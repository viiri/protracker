#include <SDL2/SDL.h>
#include <stdint.h>
#include <string.h>
#include "pt_header.h"
#include "pt_helpers.h"
#include "pt_visuals.h"
#include "pt_scopes.h"
#include "pt_sampler.h"
#include "pt_palette.h"
#include "pt_tables.h"

// for monoscope
const int16_t mixScaleTable[AMIGA_VOICES] = { 388, 570, 595, 585 };

scopeChannel_t scope[4];
static SDL_Thread *scopeThread;
static uint64_t next60HzTime_64bit;

extern int8_t forceMixerOff;  // pt_audio.c
extern uint32_t *pixelBuffer; // pt_main.c

void updateScopes(void)
{
    uint8_t i;
    int32_t samplePlayPos;
    scopeChannel_t *sc;
    moduleSample_t *s;

    if (editor.isWAVRendering)
        return;

    s = &modEntry->samples[editor.currSample];
    hideSprite(SPRITE_SAMPLING_POS_LINE);

    for (i = 0; i < AMIGA_VOICES; i++)
    {
        sc = &scope[i];
        if (sc->retriggered)
        {
            sc->retriggered = false;

            // we just (re)triggered the scopes, reset integer phase and set new phase for next cycle
            sc->phase   = 0;
            sc->phase_f = sc->delta_f; // phase_f = 0.0f; phase_f += sc->delta_f;
        }
        else if (sc->active)
        {
            // slow, but can't use modulus since I need to make a swap event after every full sample cycle.
            // max iterations: ~261 = ((paula_clock / vblank_hz) / lowest_period) / smallest_loop_length
            while (sc->phase_f >= sc->length)
            {
                sc->phase_f -= sc->length;

                sc->length   = sc->newLength;
                sc->data     = sc->newData;
                sc->loopFlag = sc->newLoopFlag; // used for scope display wrapping
            }

            sc->phase    = (int32_t)(sc->phase_f); // truncate
            sc->phase_f += sc->delta_f;
        }

        // update sample read position sprite (TODO: could use less extensive 'if' logic)
        if (editor.ui.samplerScreenShown && !editor.muted[i] && (modEntry->channels[i].n_samplenum == editor.currSample) && !editor.ui.terminalShown)
        {
            if (sc->active && (sc->phase >= 2) && !editor.ui.samplerVolBoxShown && !editor.ui.samplerFiltersBoxShown)
            {
                // get real sampling position regardless of where the scope data points to
                samplePlayPos = (int32_t)(&sc->data[sc->phase] - &modEntry->sampleData[s->offset]);
                if ((samplePlayPos >= 0) && (samplePlayPos < s->length))
                {
                    samplePlayPos = 3 + smpPos2Scr(samplePlayPos);
                    if ((samplePlayPos >= 3) && (samplePlayPos <= 316))
                        setSpritePos(SPRITE_SAMPLING_POS_LINE, samplePlayPos, 138);
                }
            }
        }
    }
}

void drawScopes(void)
{
    const int8_t *readPtr;
    int8_t volume;
    uint8_t i, y, totalVoicesActive;
    int16_t scopeData;
    int32_t x, readPos, monoScopeBuffer[MONOSCOPE_WIDTH], scopeTemp, dataLen;
    const uint32_t *ptr32Src;
    uint32_t *ptr32Dst, *scopePtr, scopePixel;
    scopeChannel_t *sc;

    scopePixel = palette[PAL_QADSCP];

    if (editor.ui.visualizerMode == VISUAL_QUADRASCOPE)
    {
        // -- quadrascope --
        scopePtr = pixelBuffer + ((71 * SCREEN_W) + 128);

        for (i = 0; i < AMIGA_VOICES; ++i)
        {
            sc = &scope[i];

            // clear background
            ptr32Src = trackerFrameBMP + ((71 * SCREEN_W) + 128);
            ptr32Dst = pixelBuffer     + ((55 * SCREEN_W) + (128 + (i * (SCOPE_WIDTH + 8))));
            y = 33;

            while (y--)
            {
                memcpy(ptr32Dst, ptr32Src, SCOPE_WIDTH * sizeof (int32_t));
                ptr32Dst += SCREEN_W;
            }

            // render scopes

            readPtr = sc->data;
            dataLen = sc->length;

            if (!sc->active || editor.muted[i] || (readPtr == NULL) || (dataLen <= 0))
            {
                for (x = 0; x < SCOPE_WIDTH; ++x) // draw idle scope
                    scopePtr[x] = scopePixel;
            }
            else
            {
                readPos = sc->phase;
                volume  = sc->volume;

                if (sc->loopFlag)
                {
                    for (x = 0; x < SCOPE_WIDTH; ++x)
                    {
                        scopeData = (readPtr[readPos++ % dataLen] * volume) >> 9;
                        scopePtr[(scopeData * SCREEN_W) + x] = scopePixel;
                    }
                }
                else
                {
                    for (x = 0; x < SCOPE_WIDTH; ++x)
                    {
                        scopeData = 0;
                        if (readPos < dataLen)
                            scopeData = (readPtr[readPos++] * volume) >> 9;

                        scopePtr[(scopeData * SCREEN_W) + x] = scopePixel;
                    }
                }
            }

            scopePtr += (SCOPE_WIDTH + 8);
        }
    }
    else
    {
        // -- monoscope --
        scopePtr = pixelBuffer + ((76 * SCREEN_W) + 120);

        // clear background
        ptr32Src = monoScopeBMP + (11 * (MONOSCOPE_WIDTH + 3));
        ptr32Dst = pixelBuffer  + (55 * SCREEN_W) + 120;

        y = 44;
        while (y--)
        {
            memcpy(ptr32Dst, ptr32Src, MONOSCOPE_WIDTH * sizeof (int32_t));
            ptr32Dst += SCREEN_W;
        }

        // mix channels

        memset(monoScopeBuffer, 0, sizeof (monoScopeBuffer));

        totalVoicesActive = 0;
        for (i = 0; i < AMIGA_VOICES; ++i)
        {
            sc = &scope[i];

            readPtr = sc->data;
            dataLen = sc->length;

            if (sc->active && !editor.muted[i] && (readPtr != NULL) && (dataLen > 0))
            {
                readPos = sc->phase;
                volume  = sc->volume;

                if (sc->loopFlag)
                {
                    for (x = 0; x < MONOSCOPE_WIDTH; ++x)
                        monoScopeBuffer[x] += (readPtr[readPos++ % dataLen] * volume);
                }
                else
                {
                    if (dataLen > MONOSCOPE_WIDTH)
                        dataLen = MONOSCOPE_WIDTH;

                    for (x = 0; x < dataLen; ++x)
                        monoScopeBuffer[x] += (readPtr[readPos++] * volume);
                }

                totalVoicesActive++;
            }
        }

        // render buffer
        for (x = 0; x < MONOSCOPE_WIDTH; ++x)
        {
            scopeTemp = monoScopeBuffer[x];
            if (totalVoicesActive > 0)
            {
                scopeTemp /= mixScaleTable[totalVoicesActive - 1];
                scopeTemp  = CLAMP(scopeTemp, -21, 22);
            }

            scopePtr[(scopeTemp * SCREEN_W) + x] = scopePixel;
        }
    }
}

int32_t scopeThreadFunc(void *ptr)
{
    uint64_t timeNow_64bit;
    double delayMs_f, perfFreq_f, frameLength_f;

    (void)(ptr);

    SDL_SetThreadPriority(SDL_THREAD_PRIORITY_HIGH);

    while (editor.programRunning)
    {
        perfFreq_f = (double)(SDL_GetPerformanceFrequency()); // should be safe for double 
        if (perfFreq_f == 0.0)
            continue; // panic!

        timeNow_64bit = SDL_GetPerformanceCounter();
        if (next60HzTime_64bit > timeNow_64bit)
        {
            delayMs_f = (double)(next60HzTime_64bit - timeNow_64bit) * (1000.0 / perfFreq_f); // should be safe for double
            SDL_Delay((uint32_t)(delayMs_f + 0.5));
        }

        frameLength_f = perfFreq_f / REAL_VBLANK_HZ;
        next60HzTime_64bit += (uint32_t)(frameLength_f + 0.5);

        updateScopes();
    }

    return (true);
}

uint8_t initScopes(void)
{
    double frameLength_f;

    frameLength_f = (double)(SDL_GetPerformanceFrequency()) / REAL_VBLANK_HZ;
    next60HzTime_64bit = SDL_GetPerformanceCounter() + (uint32_t)(frameLength_f + 0.5);

    scopeThread = SDL_CreateThread(scopeThreadFunc, "PT Clone Scope Thread", NULL);
    if (scopeThread == NULL)
        return (false);

    return (true);
}

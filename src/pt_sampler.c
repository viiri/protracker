#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "pt_header.h"
#include "pt_helpers.h"
#include "pt_textout.h"
#include "pt_audio.h"
#include "pt_palette.h"
#include "pt_tables.h"
#include "pt_visuals.h"
#include "pt_blep.h"
#include "pt_mouse.h"
#include "pt_terminal.h"
#include "pt_scopes.h"

// rounded constant to fit in float
#define M_PI_F 3.1415927f

#define SAMPLE_AREA_Y_CENTER 138
#define SAMPLE_AREA_HEIGHT 33

extern uint32_t *pixelBuffer; // pt_main.c

typedef struct sampleMixer_t
{
    int32_t length, index;
    float frac, delta;
} sampleMixer_t;

void setLoopSprites(void);

static int32_t getNumSamplesPerPixel(void)
{
    return (int32_t)((editor.sampler.samDisplay / SAMPLE_AREA_WIDTH_F) + 0.5f);
}

void updateSamplePos(void)
{
    moduleSample_t *s;

    PT_ASSERT((editor.currSample >= 0) && (editor.currSample <= 30));
    if ((editor.currSample >= 0) && (editor.currSample <= 30))
    {
        s = &modEntry->samples[editor.currSample];
        if (s->length > 0)
            editor.samplePos = CLAMP(editor.samplePos, 0, s->length - 1);
        else
            editor.samplePos = 0;

        if (editor.ui.editOpScreenShown && (editor.ui.editOpScreen == 2))
            editor.ui.updatePosText = true;
    }
}

void fillSampleFilterUndoBuffer(void)
{
    moduleSample_t *s;

    PT_ASSERT((editor.currSample >= 0) && (editor.currSample <= 30));
    if ((editor.currSample >= 0) && (editor.currSample <= 30))
    {
        s = &modEntry->samples[editor.currSample];
        memcpy(editor.tempSample, &modEntry->sampleData[s->offset], s->length);
    }
}

static void line(uint32_t *frameBuffer, int16_t line_x1, int16_t line_x2, int16_t line_y1, int16_t line_y2)
{
    int16_t d, x, y, ax, ay, sx, sy, dx, dy;

    if ((line_x1 < 0) || (line_x2 < 0) || (line_x1 >= SCREEN_W) || (line_x2 >= SCREEN_W)) return;
    if ((line_y1 < 0) || (line_y2 < 0) || (line_y1 >= SCREEN_H) || (line_y2 >= SCREEN_H)) return;

    dx = line_x2 - line_x1;
    ax = ABS(dx) * 2;
    sx = SGN(dx);
    dy = line_y2 - line_y1;
    ay = ABS(dy) * 2;
    sy = SGN(dy);
    x  = line_x1;
    y  = line_y1;

    if (ax > ay)
    {
        d = ay - (ax / 2);

        while (true)
        {
            if ((y < 0) || (x < 0) || (y >= SCREEN_H) || (x >= SCREEN_W))
                break;

            frameBuffer[(y * SCREEN_W) + x] = palette[PAL_QADSCP];

            if (x == line_x2)
                break;

            if (d >= 0)
            {
                y += sy;
                d -= ax;
            }

            x += sx;
            d += ay;
        }
    }
    else
    {
        d = ax - (ay / 2);

        while (true)
        {
            if ((y < 0) || (x < 0) || (y >= SCREEN_H) || (x >= SCREEN_W))
                break;

            frameBuffer[(y * SCREEN_W) + x] = palette[PAL_QADSCP];

            if (y == line_y2)
                break;

            if (d >= 0)
            {
                x += sx;
                d -= ay;
            }

            y += sy;
            d += ax;
        }
    }
}

static void setDragBar(void)
{
    uint8_t y;
    uint16_t x;
    int32_t barLen;
    const uint32_t *ptr32Src;
    uint32_t *ptr32Dst, pixel;

    // clear drag bar background
    ptr32Src = samplerScreenBMP + ((85 * 320) + 4);
    memcpy(&pixelBuffer[(206 * SCREEN_W) + 4], ptr32Src, 312 * sizeof (int32_t));
    memcpy(&pixelBuffer[(207 * SCREEN_W) + 4], ptr32Src, 312 * sizeof (int32_t));
    memcpy(&pixelBuffer[(208 * SCREEN_W) + 4], ptr32Src, 312 * sizeof (int32_t));
    memcpy(&pixelBuffer[(209 * SCREEN_W) + 4], ptr32Src, 312 * sizeof (int32_t));

    if ((editor.sampler.samLength > 0) && (editor.sampler.samDisplay != editor.sampler.samLength))
    {
        // update drag bar coordinates
        editor.sampler.dragStart = 4 + (uint16_t)(((editor.sampler.samOffset * 311) / (double)(editor.sampler.samLength)) + 0.5);
        editor.sampler.dragEnd   = 5 + (uint16_t)((((editor.sampler.samDisplay + editor.sampler.samOffset) * 311) / (double)(editor.sampler.samLength)) + 0.5);

        if (editor.sampler.dragStart < 4)
            editor.sampler.dragStart = 4;
        else if (editor.sampler.dragStart > 315)
            editor.sampler.dragStart = 315;

        if (editor.sampler.dragEnd < 5)
            editor.sampler.dragEnd = 5;
        else if (editor.sampler.dragEnd > 316)
            editor.sampler.dragEnd = 316;

        if (editor.sampler.dragStart > (editor.sampler.dragEnd - 1))
            editor.sampler.dragStart =  editor.sampler.dragEnd - 1;

        // draw drag bar
        ptr32Dst = pixelBuffer + ((206 * SCREEN_W) + editor.sampler.dragStart);
        pixel    = palette[PAL_QADSCP];
        barLen   = CLAMP(editor.sampler.dragEnd - editor.sampler.dragStart, 1, 312);

        y = 4;
        while (y--)
        {
            x = barLen;
            while (x--)
                *ptr32Dst++ = pixel;

            ptr32Dst += (SCREEN_W - barLen);
        }
    }
}

int32_t smpOfsToScaledOfs(int32_t x) // used drawing the sample
{
    int32_t scaledOffset;
    float offsetScale;

    if (editor.sampler.samDisplay <= 0)
        return (0);

   offsetScale = editor.sampler.samDisplay / (float)(SAMPLE_AREA_WIDTH);
   scaledOffset = (int32_t)(((editor.sampler.samOffset / (float)(editor.sampler.samDisplay)) * (float)(SAMPLE_AREA_WIDTH)) + 0.5f);

   return ((int32_t)(((scaledOffset + x) * offsetScale) + 0.5f));
}

uint16_t getScaledSample(int32_t index)
{
    uint16_t smp;

    if ((editor.sampler.samStart == NULL) || (editor.sampler.samLength <= 0))
        return (31);

    index = CLAMP(index, 0, editor.sampler.samLength - 1);
    smp = (127 - editor.sampler.samStart[index]) / 4;

    return (smp);
}

int32_t smpPos2Scr(int32_t pos) // sample pos -> screen x pos
{
    uint8_t roundFlag;
    float scaledPos_f;

    roundFlag = editor.sampler.samDisplay >= SAMPLE_AREA_WIDTH;

    scaledPos_f = (pos / (float)(editor.sampler.samDisplay)) * SAMPLE_AREA_WIDTH_F;
    if (roundFlag)
        scaledPos_f += 0.5f;

    pos = (int32_t)(scaledPos_f);

    if (editor.sampler.samDisplay > 0)
    {
        scaledPos_f = (editor.sampler.samOffset / (float)(editor.sampler.samDisplay)) * SAMPLE_AREA_WIDTH_F;
        if (roundFlag)
            scaledPos_f += 0.5f;

        pos -= (int32_t)(scaledPos_f);
    }

    return (pos);
}

int32_t scr2SmpPos(int32_t x) // screen x pos -> sample pos
{
    uint8_t roundFlag;
    float scaledPos_f;

    roundFlag = editor.sampler.samDisplay >= SAMPLE_AREA_WIDTH;

    if (editor.sampler.samDisplay > 0)
    {
        scaledPos_f = (editor.sampler.samOffset / (float)(editor.sampler.samDisplay)) * SAMPLE_AREA_WIDTH_F;
        if (roundFlag)
            scaledPos_f += 0.5f;

        x += (int32_t)(scaledPos_f);
    }

    scaledPos_f = (x / SAMPLE_AREA_WIDTH_F) * editor.sampler.samDisplay;
    if (roundFlag)
        scaledPos_f += 0.5f;

    return ((int32_t)(scaledPos_f));
}

static void renderSampleData(void)
{
    int8_t *smpPtr, *ptr8;
    int16_t smp, lo, hi, prevLo, prevHi;
    uint16_t y1, y2, y, x;
    int32_t i, numSmpsPerPixel, smpIdx, smpLen;
    const uint32_t *ptr32Src;
    uint32_t *ptr32Dst;
    moduleSample_t *s;

    s = &modEntry->samples[editor.currSample];

    // clear sample data
    ptr32Src = samplerScreenBMP + (17 * 320);
    ptr32Dst = pixelBuffer + (138 * SCREEN_W);
    y = SAMPLE_VIEW_HEIGHT;
    while (y--)
    {
        memcpy(ptr32Dst, ptr32Src, 320 * sizeof (int32_t));

        ptr32Src += 320;
        ptr32Dst += SCREEN_W;
    }

    // display center line
    if (editor.ui.dottedCenterFlag)
        memset(pixelBuffer + ((169 * SCREEN_W) + 3), 0x00373737, SAMPLE_AREA_WIDTH * sizeof (int32_t));

    // render sample data
    if ((editor.sampler.samDisplay >= 0) && (editor.sampler.samDisplay <= MAX_SAMPLE_LEN))
    {
        y1 = SAMPLE_AREA_Y_CENTER + getScaledSample(scr2SmpPos(0));

        numSmpsPerPixel = editor.sampler.samDisplay / SAMPLE_AREA_WIDTH;
        if (numSmpsPerPixel <= 1)
        {
            // 1:1 or zoomed in

            for (x = 1; x < SAMPLE_AREA_WIDTH; ++x)
            {
                y2 = SAMPLE_AREA_Y_CENTER + getScaledSample(scr2SmpPos(x));
                line(pixelBuffer, 3 + (x - 1), 3 + x, y1, y2);
                y1 = y2;
            }
        }
        else
        {
            // zoomed out

            prevLo  = y1;
            prevHi  = y1;
            smpLen  = s->length;
            smpPtr  = &modEntry->sampleData[s->offset];

            for (x = 0; x < SAMPLE_AREA_WIDTH; x++)
            {
                smpIdx = scr2SmpPos(x);

                lo =  127;
                hi = -128;

                // get peak for low/high
                ptr8 = smpPtr + smpIdx;
                for (i = 0; i < numSmpsPerPixel; ++i)
                {
                    if ((smpIdx + i) >= smpLen)
                        break;

                    smp = *ptr8++;
                    if (smp < lo) lo = smp;
                    if (smp > hi) hi = smp;
                }

                lo = SAMPLE_AREA_Y_CENTER + ((127 - lo) / 4);
                hi = SAMPLE_AREA_Y_CENTER + ((127 - hi) / 4);

                line(pixelBuffer, 3 + x, 3 + x, hi, lo);

                if (x > 0)
                {
                    if (lo > prevHi) line(pixelBuffer, 2 + x, 3 + x, prevHi, lo);
                    if (hi < prevLo) line(pixelBuffer, 2 + x, 3 + x, prevLo, hi);
                }

                prevLo = lo;
                prevHi = hi;
            }
        }
    }

    // render "sample display" text
    if (editor.sampler.samStart == editor.sampler.blankSample)
        printSixDecimalsBg(pixelBuffer, 264, 214, 0, palette[PAL_GENTXT], palette[PAL_GENBKG]);
    else
        printSixDecimalsBg(pixelBuffer, 264, 214, editor.sampler.samDisplay, palette[PAL_GENTXT], palette[PAL_GENBKG]);

    setDragBar();
    setLoopSprites();
}

void invertRange(void)
{
    uint8_t y;
    int16_t x;
    int32_t rangeLen, pitch, start, end;
    uint32_t *ptr32Dst, pixel1, pixel2;

    if ((editor.markStartOfs == 0) && (editor.markEndOfs == 0))
        return; // very first sample is the "no range" special case

    start = smpPos2Scr(editor.markStartOfs);
    end   = smpPos2Scr(editor.markEndOfs);

    if ((start >= SAMPLE_AREA_WIDTH) || (end < 0))
        return; // range is outside of view (passed it by scrolling)

    start = CLAMP(start, 0, SAMPLE_AREA_WIDTH - 1);
    end   = CLAMP(end,   0, SAMPLE_AREA_WIDTH - 1);

    rangeLen = (end + 1) - start;
    if (rangeLen < 1)
        rangeLen = 1;

    pitch = SCREEN_W - rangeLen;

    pixel1 = palette[PAL_BACKGRD];
    pixel2 = palette[PAL_QADSCP];

    ptr32Dst = pixelBuffer + ((138 * SCREEN_W) + 3 + start);

    y = 64;
    while (y--)
    {
        x = rangeLen;
        while (x--)
        {
                 if (*ptr32Dst ==     pixel1) *ptr32Dst = 0x00666666;
            else if (*ptr32Dst == 0x00666666) *ptr32Dst = pixel1;
            else if (*ptr32Dst == 0x00CCCCCC) *ptr32Dst = pixel2;
            else if (*ptr32Dst ==     pixel2) *ptr32Dst = 0x00CCCCCC;

            ptr32Dst++;
        }

        ptr32Dst += pitch;
    }
}

void displaySample(void)
{
    if (editor.ui.samplerScreenShown)
    {
        renderSampleData();
        if (editor.markEndOfs > 0)
            invertRange();

        editor.ui.update9xxPos = true;
    }
}

void redrawSample(void)
{
    moduleSample_t *s;

    if (editor.ui.samplerScreenShown)
    {
        PT_ASSERT((editor.currSample >= 0) && (editor.currSample <= 30));
        if ((editor.currSample >= 0) && (editor.currSample <= 30))
        {
            editor.markStartOfs = 0;
            editor.markEndOfs   = 0;

            editor.sampler.samOffset = 0;

            s = &modEntry->samples[editor.currSample];
            if (s->length > 0)
            {
                editor.sampler.samStart   = &modEntry->sampleData[s->offset];
                editor.sampler.samDisplay = s->length;
                editor.sampler.samLength  = s->length;
            }
            else
            {
                // "blank sample" template
                editor.sampler.samStart   = editor.sampler.blankSample;
                editor.sampler.samLength  = SAMPLE_AREA_WIDTH;
                editor.sampler.samDisplay = SAMPLE_AREA_WIDTH;
            }

            renderSampleData();
            updateSamplePos();

            editor.ui.update9xxPos     = true;
            editor.ui.lastSampleOffset = 0x900;

            // for quadrascope
            editor.sampler.samDrawStart = s->offset;
            editor.sampler.samDrawEnd   = s->offset + s->length;
        }
    }
}

void highPassSample(int32_t cutOff)
{
    int32_t i, from, to;
    float *sampleData, baseFreq_f, cutOff_f, in[2], out[2];
    moduleSample_t *s;
    lossyIntegrator_t filterHi;

    PT_ASSERT((editor.currSample >= 0) && (editor.currSample <= 30));
    if ((editor.currSample < 0) || (editor.currSample > 30))
        return;

    if (cutOff == 0)
    {
        displayErrorMsg("CUTOFF CAN'T BE 0");
        return;
    }

    s = &modEntry->samples[editor.currSample];
    if (s->length == 0)
    {
        displayErrorMsg("SAMPLE IS EMPTY");
        return;
    }

    from = 0;
    to   = s->length;

    if ((editor.markEndOfs - editor.markStartOfs) > 0)
    {
        from = editor.markStartOfs;
        to   = editor.markEndOfs;
    }

    if (to > s->length)
        to = s->length;

    sampleData = (float *)(malloc(s->length * sizeof (float)));
    if (sampleData == NULL)
    {
        displayErrorMsg(editor.outOfMemoryText);
        terminalPrintf("Sample filtering failed: out of memory!\n");

        return;
    }

    fillSampleFilterUndoBuffer();

    // setup filter coefficients

    baseFreq_f = (float)(FILTERS_BASE_FREQ);

    cutOff_f = (float)(cutOff);
    if (cutOff_f >= (baseFreq_f / 2.0f))
    {
        cutOff_f = baseFreq_f / 2.0f;
        editor.hpCutOff = (int32_t)(cutOff_f);
    }

    filterHi.coeff[0] = tanf(M_PI_F * cutOff_f / baseFreq_f);
    filterHi.coeff[1] = 1.0f / (1.0f + filterHi.coeff[0]);

    // copy over sample data to float buffer
    for (i = 0; i < s->length; ++i)
        sampleData[i] = modEntry->sampleData[s->offset + i];

    // filter forwards
    filterHi.buffer[0] = 0.0f;
    if (to <= s->length)
    {
        for (i = from; i < to; ++i)
        {
            in[0] = sampleData[i];
            lossyIntegratorHighPass(&filterHi, in, out);
            sampleData[i] = out[0];
        }
    }

    // filter backwards
    filterHi.buffer[0] = 0.0f;
    if (to <= s->length)
    {
        for (i = (to - 1); i >= from; --i)
        {
            in[0] = sampleData[i];
            lossyIntegratorHighPass(&filterHi, in, out);
            sampleData[i] = out[0];
        }
    }

    if (editor.normalizeFiltersFlag)
        normalize8bitFloatSigned(sampleData, s->length);

    for (i = from; i < to; ++i)
        modEntry->sampleData[s->offset + i] = quantizeFloatTo8bit(sampleData[i]);

    free(sampleData);

    displaySample();
    updateWindowTitle(MOD_IS_MODIFIED);
}

void lowPassSample(int32_t cutOff)
{
    int32_t i, from, to;
    float *sampleData, baseFreq_f, cutOff_f, in[2], out[2];
    moduleSample_t *s;
    lossyIntegrator_t filterLo;

    PT_ASSERT((editor.currSample >= 0) && (editor.currSample <= 30));
    if ((editor.currSample < 0) || (editor.currSample > 30))
        return;

    if (cutOff == 0)
    {
        displayErrorMsg("CUTOFF CAN'T BE 0");
        return;
    }

    s = &modEntry->samples[editor.currSample];
    if (s->length == 0)
    {
        displayErrorMsg("SAMPLE IS EMPTY");
        return;
    }

    from = 0;
    to   = s->length;

    if ((editor.markEndOfs - editor.markStartOfs) > 0)
    {
        from = editor.markStartOfs;
        to   = editor.markEndOfs;
    }

    if (to > s->length)
        to = s->length;

    sampleData = (float *)(malloc(s->length * sizeof (float)));
    if (sampleData == NULL)
    {
        displayErrorMsg(editor.outOfMemoryText);
        terminalPrintf("Sample filtering failed: out of memory!\n");

        return;
    }

    fillSampleFilterUndoBuffer();

    // setup filter coefficients

    baseFreq_f = (float)(FILTERS_BASE_FREQ);

    cutOff_f = (float)(cutOff);
    if (cutOff_f >= (baseFreq_f / 2.0f))
    {
        cutOff_f = baseFreq_f / 2.0f;
        editor.lpCutOff = (int32_t)(cutOff_f);
    }

    filterLo.coeff[0] = tanf(M_PI_F * cutOff_f / baseFreq_f);
    filterLo.coeff[1] = 1.0f / (1.0f + filterLo.coeff[0]);

    // copy over sample data to float buffer
    for (i = 0; i < s->length; ++i)
        sampleData[i] = modEntry->sampleData[s->offset + i];

    // filter forwards
    filterLo.buffer[0] = 0.0f;
    if (to <= s->length)
    {
        for (i = from; i < to; ++i)
        {
            in[0] = sampleData[i];
            lossyIntegrator(&filterLo, in, out);
            sampleData[i] = out[0];
        }
    }

    // filter backwards
    filterLo.buffer[0] = 0.0f;
    if (to <= s->length)
    {
        for (i = (to - 1); i >= from; --i)
        {
            in[0] = sampleData[i];
            lossyIntegrator(&filterLo, in, out);
            sampleData[i] = out[0];
        }
    }

    if (editor.normalizeFiltersFlag)
        normalize8bitFloatSigned(sampleData, s->length);

    for (i = from; i < to; ++i)
        modEntry->sampleData[s->offset + i] = quantizeFloatTo8bit(sampleData[i]);

    free(sampleData);

    displaySample();
    updateWindowTitle(MOD_IS_MODIFIED);
}

void redoSampleData(int8_t sample)
{
    moduleSample_t *s;

    PT_ASSERT((sample >= 0) && (sample <= 30));
    if ((sample < 0) || (sample > 30))
        return;

    s = &modEntry->samples[sample];

    turnOffVoices();

    memset(&modEntry->sampleData[s->offset], 0, MAX_SAMPLE_LEN);
    if ((editor.smpRedoBuffer[sample] != NULL) && (editor.smpRedoLengths[sample] > 0))
        memcpy(&modEntry->sampleData[s->offset], editor.smpRedoBuffer[sample], editor.smpRedoLengths[sample]);

    s->fineTune   = editor.smpRedoFinetunes[sample];
    s->volume     = editor.smpRedoVolumes[sample];
    s->length     = editor.smpRedoLengths[sample];
    s->loopStart  = editor.smpRedoLoopStarts[sample];
    s->loopLength = (editor.smpRedoLoopLengths[sample] < 2) ? 2 : editor.smpRedoLoopLengths[sample];

    displayMsg("SAMPLE RESTORED !");
    terminalPrintf("Sample %02x was restored\n", sample + 1);

    editor.samplePos = 0;
    updateCurrSample();

    // this routine can be called while the sampler toolboxes are open, so redraw them
    if (editor.ui.samplerScreenShown)
    {
             if (editor.ui.samplerVolBoxShown)     renderSamplerVolBox();
        else if (editor.ui.samplerFiltersBoxShown) renderSamplerFiltersBox();
    }
}

void fillSampleRedoBuffer(int8_t sample)
{
    moduleSample_t *s;

    PT_ASSERT((sample >= 0) && (sample <= 30));
    if ((sample < 0) || (sample > 30))
        return;

    s = &modEntry->samples[sample];

    if (editor.smpRedoBuffer[sample] != NULL)
    {
        free(editor.smpRedoBuffer[sample]);
        editor.smpRedoBuffer[sample] = NULL;
    }

    editor.smpRedoFinetunes[sample]   = s->fineTune;
    editor.smpRedoVolumes[sample]     = s->volume;
    editor.smpRedoLengths[sample]     = s->length;
    editor.smpRedoLoopStarts[sample]  = s->loopStart;
    editor.smpRedoLoopLengths[sample] = s->loopLength;

    if (s->length > 0)
    {
        editor.smpRedoBuffer[sample] = (int8_t *)(malloc(s->length));
        if (editor.smpRedoBuffer[sample] != NULL)
            memcpy(editor.smpRedoBuffer[sample], &modEntry->sampleData[s->offset], s->length);
    }
}

int8_t allocSamplerVars(void)
{
    editor.sampler.copyBuf = (int8_t *)(malloc(MAX_SAMPLE_LEN));
    if (editor.sampler.copyBuf == NULL)
        return (false);

    editor.sampler.blankSample = (int8_t *)(calloc(MAX_SAMPLE_LEN, 1));
    if (editor.sampler.blankSample == NULL)
        return (false);

    return (true);
}

void deAllocSamplerVars(void)
{
    uint8_t i;

    if (editor.sampler.copyBuf != NULL)
    {
        free(editor.sampler.copyBuf);
        editor.sampler.copyBuf = NULL;
    }

    if (editor.sampler.blankSample != NULL)
    {
        free(editor.sampler.blankSample);
        editor.sampler.blankSample = NULL;
    }

    for (i = 0; i < MOD_SAMPLES; ++i)
    {
        if (editor.smpRedoBuffer[i] != NULL)
        {
            free(editor.smpRedoBuffer[i]);
            editor.smpRedoBuffer[i] = NULL;
        }
    }
}

void samplerRemoveDcOffset(void)
{
    int8_t *smpDat;
    int32_t i, from, to, offset;
    moduleSample_t *s;

    PT_ASSERT((editor.currSample >= 0) && (editor.currSample <= 30));
    if ((editor.currSample < 0) || (editor.currSample > 30))
        return;

    s = &modEntry->samples[editor.currSample];
    if (s->length == 0)
    {
        displayErrorMsg("SAMPLE IS EMPTY");
        return;
    }

    smpDat = &modEntry->sampleData[s->offset];

    from = 0;
    to   = s->length;

    if ((editor.markEndOfs - editor.markStartOfs) > 0)
    {
        from = editor.markStartOfs;
        to   = editor.markEndOfs;
    }

    if (to > s->length)
        to = s->length;

    // calculate offset value
    offset = 0;
    for (i = from; i < to; ++i)
        offset += smpDat[i];

    if (to <= 0)
        return;

    offset /= to;

    // remove DC offset
    for (i = from; i < to; ++i)
        smpDat[i] = (int8_t)(CLAMP(smpDat[i] - offset, -128, 127));

    displaySample();
    updateWindowTitle(MOD_IS_MODIFIED);
}

void mixChordSample(void)
{
    char smpText[22 + 1];
    int8_t *smpData, sameNotes, smpVolume, smpLoopFlag;
    uint8_t smpFinetune;
    int32_t i, j, readFrac_trunc, mixPos2, smpLength, smpLoopStart, smpLoopEnd;
    float *mixerData, smp1_f, smp2_f, smp_f;
    sampleMixer_t mixCh[4];
    moduleSample_t *s;

    PT_ASSERT((editor.currSample >= 0) && (editor.currSample <= 30));
    if ((editor.currSample < 0) || (editor.currSample > 30))
        return;

    if (editor.note1 == 36)
    {
        displayErrorMsg("NO BASENOTE!");
        return;
    }

    if (modEntry->samples[editor.currSample].length == 0)
    {
        displayErrorMsg("SAMPLE IS EMPTY");
        return;
    }

    // check if all notes are the same (illegal)
    sameNotes = true;
    if ((editor.note2 != 36) && (editor.note2 != editor.note1)) sameNotes = false; else editor.note2 = 36;
    if ((editor.note3 != 36) && (editor.note3 != editor.note1)) sameNotes = false; else editor.note3 = 36;
    if ((editor.note4 != 36) && (editor.note4 != editor.note1)) sameNotes = false; else editor.note4 = 36;

    if (sameNotes)
    {
        displayErrorMsg("ONLY ONE NOTE!");
        return;
    }

    // sort the notes

    for (i = 0; i < 3; ++i)
    {
        if (editor.note2 == 36)
        {
            editor.note2 = editor.note3;
            editor.note3 = editor.note4;
            editor.note4 = 36;
        }
    }

    for (i = 0; i < 3; ++i)
    {
        if (editor.note3 == 36)
        {
            editor.note3 = editor.note4;
            editor.note4 = 36;
        }
    }

    // remove eventual note duplicates

    if (editor.note4 == editor.note3) editor.note4 = 36;
    if (editor.note4 == editor.note2) editor.note4 = 36;
    if (editor.note3 == editor.note2) editor.note3 = 36;

    if ((editor.tuningNote > 35) || (modEntry->samples[editor.currSample].fineTune > 15))
    {
        displayErrorMsg("MIX ERROR !");
        terminalPrintf("Sample chord making failed: overflown variables!\n");

        return;
    }

    editor.ui.updateNote1Text = true;
    editor.ui.updateNote2Text = true;
    editor.ui.updateNote3Text = true;
    editor.ui.updateNote4Text = true;

    // setup some variables

    turnOffVoices();

    smpLength    = modEntry->samples[editor.currSample].length;
    smpLoopStart = modEntry->samples[editor.currSample].loopStart;
    smpLoopEnd   = smpLoopStart + modEntry->samples[editor.currSample].loopLength;
    smpLoopFlag  = (modEntry->samples[editor.currSample].loopLength > 2) || (modEntry->samples[editor.currSample].loopStart >= 2);
    smpData      = &modEntry->sampleData[modEntry->samples[editor.currSample].offset];

    if (editor.newOldFlag == 0)
    {
        // find a free sample slot for the new sample

        for (i = 0; i < MOD_SAMPLES; ++i)
        {
            if (modEntry->samples[i].length == 0)
                break;
        }

        if (i == MOD_SAMPLES)
        {
            displayErrorMsg("NO EMPTY SAMPLE!");
            return;
        }

        smpFinetune = modEntry->samples[editor.currSample].fineTune;
        smpVolume   = modEntry->samples[editor.currSample].volume;

        memcpy(smpText, modEntry->samples[editor.currSample].text, sizeof (smpText));

        s = &modEntry->samples[i];

        s->fineTune = smpFinetune;
        s->volume = smpVolume;

        memcpy(s->text, smpText, sizeof (smpText));

        editor.currSample = (int8_t)(i);
    }
    else
    {
        // overwrite current sample
        s = &modEntry->samples[editor.currSample];
    }

    mixerData = (float *)(calloc(MAX_SAMPLE_LEN, sizeof (float)));
    if (mixerData == NULL)
    {
        displayErrorMsg(editor.outOfMemoryText);
        terminalPrintf("Sample chord making failed: out of memory!\n");

        return;
    }

    s->length = smpLoopFlag ? MAX_SAMPLE_LEN : editor.chordLength; // if (old) sample loops, make longest possible sample for easier loop adjustment
    s->loopLength = 2;
    s->loopStart  = 0;

    s->text[21] = '!'; // chord sample indicator
    s->text[22] = '\0';

    memset(mixCh, 0, sizeof (mixCh));

    // setup mixing lengths and deltas

    if (editor.note1 < 36)
    {
        mixCh[0].delta  = periodTable[editor.tuningNote] / (float)(periodTable[(37 * s->fineTune) + editor.note1]);
        mixCh[0].length = (smpLength * periodTable[(37 * s->fineTune) + editor.note1]) / periodTable[editor.tuningNote];
    }

    if (editor.note2 < 36)
    {
        mixCh[1].delta  = periodTable[editor.tuningNote] / (float)(periodTable[(37 * s->fineTune) + editor.note2]);
        mixCh[1].length = (smpLength * periodTable[(37 * s->fineTune) + editor.note2]) / periodTable[editor.tuningNote];
    }

    if (editor.note3 < 36)
    {
        mixCh[2].delta  = periodTable[editor.tuningNote] / (float)(periodTable[(37 * s->fineTune) + editor.note3]);
        mixCh[2].length = (smpLength * periodTable[(37 * s->fineTune) + editor.note3]) / periodTable[editor.tuningNote];
    }

    if (editor.note4 < 36)
    {
        mixCh[3].delta  = periodTable[editor.tuningNote] / (float)(periodTable[(37 * s->fineTune) + editor.note4]);
        mixCh[3].length = (smpLength * periodTable[(37 * s->fineTune) + editor.note4]) / periodTable[editor.tuningNote];
    }

    // start mixing

    for (i = 0; i < 4; ++i) // four maximum channels (notes)
    {
        if (mixCh[i].length > 0) // mix active channels only
        {
            for (j = 0; j < MAX_SAMPLE_LEN; ++j) // don't mix more than we can handle in a sample slot
            {
                // lookup next (future) sample (for linear interpolation)
                mixPos2 = mixCh[i].index + 1;
                if (smpLoopFlag) // and wrap if needed
                {
                    if (mixPos2 >= smpLoopEnd)
                        mixPos2  = smpLoopStart;
                }
                else
                {
                    if (mixPos2 >= smpLength)
                        mixPos2  = 0;
                }

                smp1_f = smpData[mixCh[i].index] / 128.0f;
                smp2_f = smpData[mixPos2       ] / 128.0f;
                smp_f  = LERP(smp1_f, smp2_f, mixCh[i].frac);

                mixerData[j] += smp_f;

                mixCh[i].frac += mixCh[i].delta;
                if (mixCh[i].frac >= 1.0f)
                {
                    readFrac_trunc = (int32_t)(mixCh[i].frac);

                    mixCh[i].index += readFrac_trunc;
                    mixCh[i].frac  -= readFrac_trunc;

                    if (smpLoopFlag)
                    {
                        while (mixCh[i].index >= smpLoopEnd)
                               mixCh[i].index  = smpLoopStart + (mixCh[i].index - smpLoopEnd);
                    }
                    else if (mixCh[i].index >= smpLength)
                    {
                        break; // we're done mixing this channel
                    }
                }
            }
        }
    }

    // normalize gain and scale back to 8-bit

    normalize8bitFloatSigned(mixerData, s->length);

    memset(&modEntry->sampleData[s->offset], 0, MAX_SAMPLE_LEN);
    for (i = 0; i < s->length; ++i)
        modEntry->sampleData[s->offset + i] = quantizeFloatTo8bit(mixerData[i]);

    // ...over and out!

    free(mixerData);

    editor.samplePos = 0;
    updateCurrSample();

    updateWindowTitle(MOD_IS_MODIFIED);
}

void samplerResample(void)
{
    int8_t *oldSampleData, *newSampleData;
    int16_t refPeriod, newPeriod;
    int32_t readFrac_trunc, readPhase, readLength, writePhase, writeLength;
    float readDelta, readFrac, smp1_f, smp2_f, smd_f;
    moduleSample_t *s;

    PT_ASSERT((editor.currSample >= 0) && (editor.currSample <= 30));
    if ((editor.currSample < 0) || (editor.currSample > 30))
        return;

    s = &modEntry->samples[editor.currSample];
    if (s->length == 0)
    {
        displayErrorMsg("SAMPLE IS EMPTY");
        return;
    }

    if ((editor.tuningNote > 35) || (editor.resampleNote > 35) || (s->fineTune > 15))
    {
        displayErrorMsg("RESAMPLE ERROR!");
        terminalPrintf("Sample resampling failed: overflown variables!\n");
        return;
    }

    // allocate memory for our temp sample data
    oldSampleData = (int8_t *)(malloc(s->length));
    if (oldSampleData == NULL)
    {
        displayErrorMsg(editor.outOfMemoryText);
        terminalPrintf("Sample resampling failed: out of memory!\n");

        return;
    }

    // setup resampling variables

    newSampleData = &modEntry->sampleData[s->offset];

    refPeriod = periodTable[editor.tuningNote];
    newPeriod = periodTable[(37 * s->fineTune) + editor.resampleNote];

    PT_ASSERT(refPeriod >= 0);

    readLength  = s->length;
    writeLength = (readLength * newPeriod) / refPeriod;

    if (writeLength <= 0)
    {
        free(oldSampleData);

        displayErrorMsg("RESAMPLE ERROR !");
        terminalPrintf("Sample resampling failed: new sample length == 0!\n");

        return;
    }

    readDelta = readLength / (float)(writeLength);
    readFrac  = 0.0f;

    writeLength = writeLength & 0xFFFFFFFE;
    if (writeLength > MAX_SAMPLE_LEN)
        writeLength = MAX_SAMPLE_LEN;

    // kill mixer voices and copy old sample data into temp buffer
    turnOffVoices();
    memcpy(oldSampleData, newSampleData, readLength);

    // resample!

    readPhase  = 0;
    writePhase = 0;

    while (writePhase < writeLength)
    {
        smp1_f = oldSampleData[readPhase];
        smp2_f = oldSampleData[(readPhase + 1) % readLength];

        smd_f = LERP(smp1_f, smp2_f, readFrac);
        smd_f = ROUND_SMP_F(smd_f);
        smd_f = CLAMP(smd_f, -128.0f, 127.0f);

        newSampleData[writePhase++] = (int8_t)(smd_f);

        readFrac += readDelta;
        if (readFrac >= 1.0f)
        {
            readFrac_trunc = (int32_t)(readFrac);

            readPhase += readFrac_trunc;
            readFrac  -= readFrac_trunc;
        }
    }

    free(oldSampleData);

    // wipe non-used data in new sample
    if (writeLength < MAX_SAMPLE_LEN)
        memset(&newSampleData[writePhase], 0, MAX_SAMPLE_LEN - writeLength);

    // update sample attributes
    s->length   = writeLength;
    s->fineTune = 0;

    // scale loop points (and deactivate if overflowing)

    PT_ASSERT(readDelta > 0.0f);

    if ((s->loopStart + s->loopLength) > 2)
    {
        s->loopStart  = (int32_t)((s->loopStart  / (float)(readDelta)) + 0.5f) & 0xFFFFFFFE;
        s->loopLength = (int32_t)((s->loopLength / (float)(readDelta)) + 0.5f) & 0xFFFFFFFE;

        if ((s->loopStart + s->loopLength) > s->length)
        {
            s->loopStart  = 0;
            s->loopLength = 2;
        }
    }

    updateCurrSample();
    updateWindowTitle(MOD_IS_MODIFIED);
}

void doMix(void)
{
    int8_t *fromPtr1, *fromPtr2, *mixPtr;
    uint8_t smpFrom1, smpFrom2, smpTo;
    int16_t tmpSmp;
    int32_t i, mixLength;
    double smp;
    moduleSample_t *s1, *s2, *s3;

    smpFrom1 = hexToInteger2(&editor.mixText[4]);
    smpFrom2 = hexToInteger2(&editor.mixText[7]);
    smpTo    = hexToInteger2(&editor.mixText[13]);

    if ((smpFrom1 == 0) || (smpFrom1 > 0x1F) ||
        (smpFrom2 == 0) || (smpFrom2 > 0x1F) ||
        (smpTo    == 0) || (smpTo    > 0x1F))
    {
        displayErrorMsg("NOT RANGE 01-1F !");
        return;
    }

    s1 = &modEntry->samples[--smpFrom1];
    s2 = &modEntry->samples[--smpFrom2];
    s3 = &modEntry->samples[--smpTo];

    if ((s1->length == 0) || (s2->length == 0))
    {
        displayErrorMsg("EMPTY SAMPLES !!!");
        return;
    }

    if (s1->length > s2->length)
    {
        fromPtr1  = &modEntry->sampleData[s1->offset];
        fromPtr2  = &modEntry->sampleData[s2->offset];
        mixLength = s1->length;
    }
    else
    {
        fromPtr1  = &modEntry->sampleData[s2->offset];
        fromPtr2  = &modEntry->sampleData[s1->offset];
        mixLength = s2->length;
    }

    mixPtr = (int8_t *)(calloc(mixLength, 1));
    if (mixPtr == NULL)
    {
        displayErrorMsg(editor.outOfMemoryText);
        terminalPrintf("Sample mixing failed: out of memory!\n");

        return;
    }

    turnOffVoices();

    if (mixLength <= MAX_SAMPLE_LEN)
    {
        for (i = 0; i < mixLength; ++i)
        {
            tmpSmp = (i < s2->length) ? (fromPtr1[i] + fromPtr2[i]) : fromPtr1[i];

            if (editor.halfClipFlag == 0)
            {
                smp = tmpSmp / 2.0;
                smp = ROUND_SMP_D(smp);

                mixPtr[i] = (int8_t)(CLAMP(smp, -128.0, 127.0));
            }
            else
            {
                mixPtr[i] = (int8_t)(CLAMP(tmpSmp, -128, 127));
            }
        }

        memset(&modEntry->sampleData[s3->offset],      0, MAX_SAMPLE_LEN);
        memcpy(&modEntry->sampleData[s3->offset], mixPtr, mixLength);
    }

    free(mixPtr);

    s3->length     = mixLength;
    s3->volume     = 64;
    s3->fineTune   = 0;
    s3->loopStart  = 0;
    s3->loopLength = 2;

    editor.currSample = smpTo;

    editor.samplePos = 0;
    updateCurrSample();
    updateWindowTitle(MOD_IS_MODIFIED);
}

// this is actually treble increase
void boostSample(int8_t sample, int8_t ignoreMark)
{
    int8_t *smpDat;
    int16_t tmp16_1, tmp16_2, tmp16_3;
    int32_t i, from, to;
    double smp;
    moduleSample_t *s;

    PT_ASSERT((sample >= 0) && (sample <= 30));
    if ((sample < 0) || (sample > 30))
        return;

    s = &modEntry->samples[sample];
    if (s->length == 0)
        return; // don't display warning/show warning pointer, it is done elsewhere

    smpDat = &modEntry->sampleData[s->offset];

    from = 0;
    to   = s->length;

    if (!ignoreMark)
    {
        if ((editor.markEndOfs - editor.markStartOfs) > 0)
        {
            from = editor.markStartOfs;
            to   = editor.markEndOfs;

            if (to > s->length)
                to = s->length;
        }
    }

    tmp16_3 = 0;
    for (i = from; i < to; ++i)
    {
        tmp16_1  = smpDat[i];

        tmp16_2  = tmp16_1;
        tmp16_1 -= tmp16_3;
        tmp16_3  = tmp16_2;

        smp = tmp16_1 / 4.0;
        smp = ROUND_SMP_D(smp);
        tmp16_2 += (int16_t)(CLAMP(smp, -128.0, 127.0));

        smpDat[i] = (int8_t)(CLAMP(tmp16_2, -128, 127));
    }

    // don't redraw sample here, it is done elsewhere
}

// this is actually treble decrease
void filterSample(int8_t sample, int8_t ignoreMark)
{
    int8_t *smpDat;
    int32_t i, from, to;
    double smp_d;
    moduleSample_t *s;

    PT_ASSERT((sample >= 0) && (sample <= 30));
    if ((sample < 0) || (sample > 30))
        return;

    s = &modEntry->samples[sample];
    if (s->length == 0)
        return; // don't display warning/show warning pointer, it is done elsewhere

    smpDat = &modEntry->sampleData[s->offset];

    from = 0;
    to   = s->length;

    if (!ignoreMark)
    {
        if ((editor.markEndOfs - editor.markStartOfs) > 0)
        {
            from = editor.markStartOfs;
            to   = editor.markEndOfs;

            if (to > s->length)
                to = s->length;
        }
    }

    if (to < 1)
        return;

    for (i = from; i < (to - 1); ++i)
    {
        smp_d = (smpDat[i] + smpDat[i + 1]) / 2.0;
        smp_d = ROUND_SMP_D(smp_d);
        smp_d = CLAMP(smp_d, -128.0, 127.0);

        smpDat[i] = (int8_t)(smp_d);
    }

    // don't redraw sample here, it is done elsewhere
}

void toggleTuningTone(void)
{
    if ((editor.currMode == MODE_PLAY) || (editor.currMode == MODE_RECORD))
        return;

    editor.tuningFlag ^= 1;
    if (editor.tuningFlag)
    {
        // turn tuning tone on

        editor.tuningChan = (editor.cursor.channel + 1) & 3;

        if (editor.tuningNote > 35)
            editor.tuningNote = 35;

        paulaSetPeriod(editor.tuningChan, periodTable[editor.tuningNote]);
        paulaSetVolume(editor.tuningChan, 64);
        paulaSetData(editor.tuningChan, tuneToneData);
        paulaSetLength(editor.tuningChan, sizeof (tuneToneData));
        paulaRestartDMA(editor.tuningChan);

        // force loop flag on for scopes
        scope[editor.tuningChan].newLoopFlag = scope[editor.tuningChan].loopFlag = true;
    }
    else
    {
        // turn tuning tone off
        mixerKillVoice(editor.tuningChan);
    }
}

void sampleMarkerToBeg(void)
{
    invertRange();

    if (input.keyb.shiftKeyDown && (editor.markEndOfs > 0))
    {
        editor.markStartOfs = editor.sampler.samOffset;
    }
    else
    {
        editor.markStartOfs = editor.sampler.samOffset;
        editor.markEndOfs   = editor.markStartOfs;
    }

    invertRange();

    editor.samplePos = editor.markEndOfs;
    updateSamplePos();
}

void sampleMarkerToCenter(void)
{
    int32_t middlePos;

    middlePos = editor.sampler.samOffset + (int32_t)((editor.sampler.samDisplay / 2.0f) + 0.5f);

    invertRange();

    if (input.keyb.shiftKeyDown && (editor.markEndOfs > 0))
    {
             if (editor.markStartOfs < middlePos) editor.markEndOfs   = middlePos;
        else if (editor.markEndOfs   > middlePos) editor.markStartOfs = middlePos;
    }
    else
    {
        editor.markStartOfs = middlePos;
        editor.markEndOfs   = editor.markStartOfs;
    }

    invertRange();

    editor.samplePos = editor.markEndOfs;
    updateSamplePos();
}

void sampleMarkerToEnd(void)
{
    int32_t endPos;

    endPos = editor.sampler.samOffset + editor.sampler.samDisplay;

    invertRange();

    if (input.keyb.shiftKeyDown && (editor.markEndOfs > 0))
    {
        editor.markEndOfs = endPos;
    }
    else
    {
        editor.markStartOfs = endPos - getNumSamplesPerPixel();
        editor.markEndOfs   = editor.markStartOfs;
    }

    invertRange();

    editor.samplePos = editor.markEndOfs;
    updateSamplePos();
}

void samplerSamCopy(void)
{
    moduleSample_t *s;

    PT_ASSERT((editor.currSample >= 0) && (editor.currSample <= 30));
    if ((editor.currSample < 0) || (editor.currSample > 30))
        return;

    if (editor.markEndOfs == 0)
    {
        displayErrorMsg("NO RANGE SELECTED");
        return;
    }

    if ((editor.markEndOfs - editor.markStartOfs) == 0)
    {
        displayErrorMsg("SET LARGER RANGE");
        return;
    }

    s = &modEntry->samples[editor.currSample];
    if (s->length == 0)
    {
        displayErrorMsg("SAMPLE IS EMPTY");
        return;
    }

    editor.sampler.copyBufSize = editor.markEndOfs - editor.markStartOfs;

    if ((int32_t)(editor.markStartOfs + editor.sampler.copyBufSize) > MAX_SAMPLE_LEN)
    {
        displayErrorMsg("COPY ERROR !");
        terminalPrintf("Sample copy failed: overflown variables!\n");
        return;
    }

    memcpy(editor.sampler.copyBuf, &modEntry->sampleData[s->offset + editor.markStartOfs], editor.sampler.copyBufSize);

    invertRange();
    invertRange();
}

void samplerSamDelete(uint8_t cut)
{
    int8_t *tmpBuf;
    int32_t sampleLength, copyLength, markEnd, markStart;
    moduleSample_t *s;

    PT_ASSERT((editor.currSample >= 0) && (editor.currSample <= 30));
    if ((editor.currSample < 0) || (editor.currSample > 30))
        return;

    if (editor.markEndOfs == 0)
    {
        displayErrorMsg("NO RANGE SELECTED");
        return;
    }

    if ((editor.markEndOfs - editor.markStartOfs) == 0)
    {
        displayErrorMsg("SET LARGER RANGE");
        return;
    }

    if (cut)
        samplerSamCopy();

    s = &modEntry->samples[editor.currSample];

    sampleLength = s->length;
    if (sampleLength == 0)
    {
        displayErrorMsg("SAMPLE IS EMPTY");
        return;
    }

    turnOffVoices();

    // if whole sample is marked, nuke it
    if ((editor.markEndOfs - editor.markStartOfs) >= sampleLength)
    {
        memset(&modEntry->sampleData[s->offset], 0, MAX_SAMPLE_LEN);

        invertRange();
        invertRange();

        editor.markStartOfs = 0;
        editor.markEndOfs   = 0;

        invertRange();

        editor.sampler.samStart   = editor.sampler.blankSample;
        editor.sampler.samDisplay = SAMPLE_AREA_WIDTH;
        editor.sampler.samLength  = SAMPLE_AREA_WIDTH;

        s->length     = 0;
        s->loopStart  = 0;
        s->loopLength = 2;
        s->volume     = 0;
        s->fineTune   = 0;

        editor.samplePos = 0;
        updateCurrSample();

        updateWindowTitle(MOD_IS_MODIFIED);
        return;
    }

    markEnd   = (editor.markEndOfs > sampleLength) ? sampleLength : editor.markEndOfs;
    markStart = editor.markStartOfs;

    copyLength = (editor.markStartOfs + sampleLength) - markEnd;
    if ((copyLength < 2) || (copyLength > MAX_SAMPLE_LEN))
    {
        displayErrorMsg("SAMPLE CUT FAIL !");
        terminalPrintf("Sample data cutting failed: length error!\n");

        return;
    }

    tmpBuf = (int8_t *)(malloc(copyLength));
    if (tmpBuf == NULL)
    {
        displayErrorMsg(editor.outOfMemoryText);
        terminalPrintf("Sample data cutting failed: out of memory!\n");

        return;
    }

    // copy start part
    memcpy(tmpBuf, &modEntry->sampleData[s->offset], editor.markStartOfs);

    // copy end part
    if ((sampleLength - markEnd) > 0)
        memcpy(&tmpBuf[editor.markStartOfs], &modEntry->sampleData[s->offset + markEnd], sampleLength - markEnd);

    // nuke sample data and copy over the result
    memset(&modEntry->sampleData[s->offset],      0, MAX_SAMPLE_LEN);
    memcpy(&modEntry->sampleData[s->offset], tmpBuf, copyLength);

    free(tmpBuf);

    editor.sampler.samLength = copyLength;
    if ((editor.sampler.samOffset + editor.sampler.samDisplay) >= editor.sampler.samLength)
    {
        if (editor.sampler.samDisplay < editor.sampler.samLength)
        {
            if ((editor.sampler.samLength - editor.sampler.samDisplay) < 0)
            {
                editor.sampler.samOffset  = 0;
                editor.sampler.samDisplay = editor.sampler.samLength;
            }
            else
            {
                editor.sampler.samOffset = editor.sampler.samLength - editor.sampler.samDisplay;
            }
        }
        else
        {
            editor.sampler.samOffset  = 0;
            editor.sampler.samDisplay = editor.sampler.samLength;
        }
    }

    if (s->loopLength > 2) // loop enabled?
    {
        if (markEnd > s->loopStart)
        {
            if (markStart < (s->loopStart + s->loopLength))
            {
                // we cut data inside the loop, ïncrease loop length
                s->loopLength -= ((markEnd - markStart) & 0xFFFFFFFE);
                if (s->loopLength < 2)
                    s->loopLength = 2;
            }

            // we cut data after the loop, don't modify loop points
        }
        else
        {
            // we cut data before the loop, adjust loop start point
            s->loopStart = (s->loopStart - (markEnd - editor.markStartOfs)) & 0xFFFFFFFE;
            if (s->loopStart < 0)
            {
                s->loopStart  = 0;
                s->loopLength = 2;
            }
        }
    }

    s->length = copyLength & 0xFFFFFFFE;

    if (editor.sampler.samDisplay <= 2)
    {
        editor.sampler.samStart   = editor.sampler.blankSample;
        editor.sampler.samLength  = SAMPLE_AREA_WIDTH;
        editor.sampler.samDisplay = SAMPLE_AREA_WIDTH;
    }

    invertRange();
    invertRange();

    if (editor.sampler.samDisplay == 0)
    {
        editor.markStartOfs = 0;
        editor.markEndOfs   = 0;
    }
    else
    {
        if (editor.markStartOfs >= s->length)
            editor.markStartOfs  = s->length - 2;

        editor.markEndOfs = editor.markStartOfs;
    }

    invertRange();

    editor.samplePos = editor.markStartOfs;
    updateSamplePos();
    recalcChordLength();
    displaySample();

    editor.ui.updateCurrSampleLength = true;
    editor.ui.updateCurrSampleRepeat = true;
    editor.ui.updateCurrSampleReplen = true;
    editor.ui.updateSongSize         = true;

    updateWindowTitle(MOD_IS_MODIFIED);
}

void samplerSamPaste(void)
{
    int8_t *tmpBuf, wasZooming;
    uint32_t readPos;
    moduleSample_t *s;

    PT_ASSERT((editor.currSample >= 0) && (editor.currSample <= 30));
    if ((editor.currSample < 0) || (editor.currSample > 30))
        return;

    if ((editor.sampler.copyBuf == NULL) || (editor.sampler.copyBufSize == 0))
    {
        displayErrorMsg("BUFFER IS EMPTY");
        return;
    }

    s = &modEntry->samples[editor.currSample];

    if (s->length == 0)
    {
        invertRange();
        editor.markStartOfs = 0;
        editor.markEndOfs   = 0;
        invertRange();
    }
    else if (editor.markEndOfs == 0)
    {
        displayErrorMsg("SET CURSOR POS");
        return;
    }

    if ((s->length + editor.sampler.copyBufSize) > MAX_SAMPLE_LEN)
    {
        displayErrorMsg("NOT ENOUGH ROOM");
        return;
    }

    tmpBuf = (int8_t *)(malloc(MAX_SAMPLE_LEN));
    if (tmpBuf == NULL)
    {
        displayErrorMsg(editor.outOfMemoryText);
        terminalPrintf("Sample data pasting failed: out of memory\n");

        return;
    }

    readPos = 0;

    turnOffVoices();

    wasZooming = (editor.sampler.samDisplay != editor.sampler.samLength) ? true : false;

    // copy start part
    if (editor.markStartOfs > 0)
    {
        memcpy(&tmpBuf[readPos], &modEntry->sampleData[s->offset], editor.markStartOfs);
        readPos += editor.markStartOfs;
    }

    // copy buffer
    memcpy(&tmpBuf[readPos], editor.sampler.copyBuf, editor.sampler.copyBufSize);

    // copy end part
    if (editor.markStartOfs >= 0)
    {
        readPos += editor.sampler.copyBufSize;

        if ((s->length - editor.markStartOfs) > 0)
            memcpy(&tmpBuf[readPos], &modEntry->sampleData[s->offset + editor.markStartOfs], s->length - editor.markStartOfs);
    }

    s->length = (s->length + editor.sampler.copyBufSize) & 0xFFFFFFFE;
    if (s->length > MAX_SAMPLE_LEN)
        s->length = MAX_SAMPLE_LEN;

    editor.sampler.samLength = s->length;

    if (s->loopLength > 2) // loop enabled?
    {
        if (editor.markStartOfs > s->loopStart)
        {
            if (editor.markStartOfs < (s->loopStart + s->loopLength))
            {
                // we pasted data inside the loop, increase loop length
                s->loopLength += (editor.sampler.copyBufSize & 0xFFFFFFFE);
                if ((s->loopStart + s->loopLength) > s->length)
                {
                    s->loopStart  = 0;
                    s->loopLength = 2;
                }
            }

            // we pasted data after the loop, don't modify loop points
        }
        else
        {
            // we pasted data before the loop, adjust loop start point
            s->loopStart = (s->loopStart + editor.sampler.copyBufSize) & 0xFFFFFFFE;
            if ((s->loopStart + s->loopLength) > s->length)
            {
                s->loopStart  = 0;
                s->loopLength = 2;
            }
        }
    }

    memset(&modEntry->sampleData[s->offset],      0, MAX_SAMPLE_LEN);
    memcpy(&modEntry->sampleData[s->offset], tmpBuf, s->length);

    free(tmpBuf);

    invertRange();
    //invertRange(); WTF
    editor.markEndOfs = editor.markStartOfs + editor.sampler.copyBufSize;
    invertRange();

    editor.samplePos = editor.markEndOfs;
    updateSamplePos();
    recalcChordLength();

    if (wasZooming)
        displaySample();
    else
        redrawSample();

    editor.ui.updateCurrSampleLength = true;
    editor.ui.updateSongSize = true;

    updateWindowTitle(MOD_IS_MODIFIED);
}

void samplerPlayWaveform(void)
{
    uint8_t chn;
    int16_t tempPeriod;
    moduleChannel_t *ch;
    moduleSample_t *s;

    PT_ASSERT((editor.currSample >= 0) && (editor.currSample <= 30));
    if ((editor.currSample < 0) || (editor.currSample > 30))
        return;

    chn = editor.cursor.channel;
    PT_ASSERT(chn < AMIGA_VOICES);
    if (chn >= AMIGA_VOICES)
        return;

    s = &modEntry->samples[editor.currSample];
    if (s->length == 0)
    {
        mixerKillVoice(chn);
    }
    else
    {
        if ((editor.currPlayNote > 35) || (s->fineTune > 15))
            return;

        ch = &modEntry->channels[chn];

        tempPeriod      = periodTable[(37 * s->fineTune) + editor.currPlayNote];
        ch->n_samplenum = editor.currSample;
        ch->n_volume    = s->volume;
        ch->n_period    = tempPeriod;
        ch->n_start     = &modEntry->sampleData[s->offset];
        ch->n_length    = (s->loopStart > 0) ? (s->loopStart + s->loopLength) : s->length; // yes, this is correct. Do not touch
        ch->n_loopstart = &modEntry->sampleData[s->offset + s->loopStart];
        ch->n_replen    = s->loopLength;

        if (ch->n_length == 0)
            ch->n_length = 2;

        paulaSetVolume(chn, ch->n_volume);
        paulaSetPeriod(chn, ch->n_period);
        paulaSetData(chn,   ch->n_start);
        paulaSetLength(chn, ch->n_length);
        paulaRestartDMA(chn);

        // these take effect after the current DMA cycle is done
        paulaSetData(chn,   ch->n_loopstart);
        paulaSetLength(chn, ch->n_replen);

        updateSpectrumAnalyzer(chn, ch->n_volume, ch->n_period);
    }
}

void samplerPlayDisplay(void)
{
    uint8_t chn;
    int16_t tempPeriod;
    moduleChannel_t *ch;
    moduleSample_t *s;

    PT_ASSERT((editor.currSample >= 0) && (editor.currSample <= 30));
    if ((editor.currSample < 0) || (editor.currSample > 30))
        return;

    chn = editor.cursor.channel;
    PT_ASSERT(chn < AMIGA_VOICES);
    if (chn >= AMIGA_VOICES)
        return;

    s = &modEntry->samples[editor.currSample];
    if (s->length == 0)
    {
        mixerKillVoice(chn);
    }
    else
    {
        if ((editor.currPlayNote > 35) || (s->fineTune > 15))
            return;

        ch = &modEntry->channels[chn];

        tempPeriod      = periodTable[(37 * s->fineTune) + editor.currPlayNote];
        ch->n_samplenum = editor.currSample;
        ch->n_period    = tempPeriod;
        ch->n_volume    = s->volume;
        ch->n_start     = &modEntry->sampleData[s->offset + editor.sampler.samOffset];
        ch->n_length    = editor.sampler.samDisplay;
        ch->n_loopstart = &modEntry->sampleData[s->offset];
        ch->n_replen    = 2;

        if (ch->n_length == 0)
            ch->n_length = 2;

        paulaSetVolume(chn, ch->n_volume);
        paulaSetPeriod(chn, ch->n_period);
        paulaSetData(chn,   ch->n_start);
        paulaSetLength(chn, ch->n_length);
        paulaRestartDMA(chn);

        // these take effect after the current DMA cycle is done
        paulaSetData(chn,   ch->n_loopstart);
        paulaSetLength(chn, ch->n_replen);

        updateSpectrumAnalyzer(chn, ch->n_volume, ch->n_period);
    }
}

void samplerPlayRange(void)
{
    uint8_t chn;
    int16_t tempPeriod;
    moduleChannel_t *ch;
    moduleSample_t *s;

    PT_ASSERT((editor.currSample >= 0) && (editor.currSample <= 30));
    if ((editor.currSample < 0) || (editor.currSample > 30))
        return;

    chn = editor.cursor.channel;
    PT_ASSERT(chn < AMIGA_VOICES);
    if (chn >= AMIGA_VOICES)
        return;

    if (editor.markEndOfs == 0)
    {
        displayErrorMsg("NO RANGE SELECTED");
        return;
    }

    if ((editor.markEndOfs - editor.markStartOfs) < 2)
    {
        displayErrorMsg("SET LARGER RANGE");
        return;
    }

    s = &modEntry->samples[editor.currSample];
    if (s->length == 0)
    {
        mixerKillVoice(chn);
    }
    else
    {
        if ((editor.currPlayNote > 35) || (s->fineTune > 15))
            return;

        ch = &modEntry->channels[chn];

        tempPeriod      = periodTable[(37 * s->fineTune) + editor.currPlayNote];
        ch->n_samplenum = editor.currSample;
        ch->n_period    = tempPeriod;
        ch->n_volume    = s->volume;
        ch->n_start     = &modEntry->sampleData[s->offset + editor.markStartOfs];
        ch->n_length    = editor.markEndOfs - editor.markStartOfs;
        ch->n_loopstart = &modEntry->sampleData[s->offset];
        ch->n_replen    = 2;

        if (ch->n_length == 0)
            ch->n_length = 2;

        paulaSetVolume(chn, ch->n_volume);
        paulaSetPeriod(chn, ch->n_period);
        paulaSetData(chn,   ch->n_start);
        paulaSetLength(chn, ch->n_length);
        paulaRestartDMA(chn);

        // these take effect after the current DMA cycle is done
        paulaSetData(chn,   ch->n_loopstart);
        paulaSetLength(chn, ch->n_replen);

        updateSpectrumAnalyzer(chn, ch->n_volume, ch->n_period);
    }
}

void setLoopSprites(void)
{
    moduleSample_t *s;

    if (!editor.ui.samplerScreenShown)
    {
        hideSprite(SPRITE_LOOP_PIN_LEFT);
        hideSprite(SPRITE_LOOP_PIN_RIGHT);
        return;
    }

    PT_ASSERT((editor.currSample >= 0) && (editor.currSample <= 30));
    if ((editor.currSample < 0) || (editor.currSample > 30))
        return;

    s = &modEntry->samples[editor.currSample];
    if ((s->loopStart + s->loopLength) > 2)
    {
        if (editor.sampler.samDisplay > 0)
        {
            editor.sampler.loopStartPos = smpPos2Scr(s->loopStart);
            if ((editor.sampler.loopStartPos >= 0) && (editor.sampler.loopStartPos <= SAMPLE_AREA_WIDTH))
                setSpritePos(SPRITE_LOOP_PIN_LEFT, editor.sampler.loopStartPos, 138);
            else
                hideSprite(SPRITE_LOOP_PIN_LEFT);

            editor.sampler.loopEndPos = smpPos2Scr(s->loopStart + s->loopLength);
            if ((editor.sampler.loopEndPos >= 0) && (editor.sampler.loopEndPos <= SAMPLE_AREA_WIDTH))
                setSpritePos(SPRITE_LOOP_PIN_RIGHT, editor.sampler.loopEndPos + 3, 138);
            else
                hideSprite(SPRITE_LOOP_PIN_RIGHT);
        }
    }
    else
    {
        editor.sampler.loopStartPos  = 0;
        editor.sampler.loopEndPos    = 0;

        hideSprite(SPRITE_LOOP_PIN_LEFT);
        hideSprite(SPRITE_LOOP_PIN_RIGHT);
    }

    textOutBg(pixelBuffer, 288, 225, ((s->loopStart + s->loopLength) > 2) ? "ON " : "OFF", palette[PAL_GENTXT], palette[PAL_GENBKG]);
}

void samplerShowAll(void)
{
    if (editor.sampler.samDisplay == editor.sampler.samLength)
        return; // don't attempt to show all if already showing all! }

    editor.sampler.samOffset  = 0;
    editor.sampler.samDisplay = editor.sampler.samLength;

    displaySample();
}

void samplerZoomOut(void)
{
    int32_t newDisplay, tmpDisplay, tmpOffset;

    if (editor.sampler.samDisplay == editor.sampler.samLength)
        return; // don't attempt to zoom out if 100% zoomed out

    tmpOffset  = editor.sampler.samOffset;
    tmpDisplay = editor.sampler.samDisplay;

    newDisplay = editor.sampler.samDisplay * 2;
    if (newDisplay > editor.sampler.samLength)
    {
        editor.sampler.samOffset = 0;
        editor.sampler.samDisplay = editor.sampler.samLength;
    }
    else
    {
        tmpDisplay /= 2;
        if (tmpOffset >= tmpDisplay)
            tmpOffset -= tmpDisplay;

        tmpDisplay = tmpOffset + newDisplay;
        if (tmpDisplay > editor.sampler.samLength)
            tmpOffset  = editor.sampler.samLength - newDisplay;

        editor.sampler.samOffset  = tmpOffset;
        editor.sampler.samDisplay = newDisplay;
    }

    displaySample();
}

void samplerRangeAll(void)
{
    invertRange();

    editor.markStartOfs = editor.sampler.samOffset;
    editor.markEndOfs   = editor.sampler.samOffset + editor.sampler.samDisplay;

    invertRange();
}

void samplerShowRange(void)
{
    moduleSample_t *s;

    PT_ASSERT((editor.currSample >= 0) && (editor.currSample <= 30));
    if ((editor.currSample < 0) || (editor.currSample > 30))
        return;

    s = &modEntry->samples[editor.currSample];
    if (s->length == 0)
    {
        displayErrorMsg("SAMPLE IS EMPTY");
        return;
    }

    if (editor.markEndOfs == 0)
    {
        displayErrorMsg("NO RANGE SELECTED");
        return;
    }

    if ((editor.markEndOfs - editor.markStartOfs) < 2)
    {
        displayErrorMsg("SET LARGER RANGE");
        return;
    }

    editor.sampler.samDisplay = editor.markEndOfs - editor.markStartOfs;
    editor.sampler.samOffset  = editor.markStartOfs;

    if ((editor.sampler.samDisplay + editor.sampler.samOffset) > editor.sampler.samLength)
        editor.sampler.samOffset = editor.sampler.samLength - editor.sampler.samDisplay;

    samplerRangeAll();
    displaySample();
}

void volBoxBarPressed(int8_t mouseButtonHeld)
{
    int32_t mouseX;

    if ((input.mouse.y < 0) || (input.mouse.x < 0) || (input.mouse.y >= SCREEN_H) || (input.mouse.x >= SCREEN_W))
        return;

    if (!mouseButtonHeld)
    {
        if ((input.mouse.x >= 72) && (input.mouse.x <= 173))
        {
            if ((input.mouse.y >= 154) && (input.mouse.y <= 174)) editor.ui.forceVolDrag = 1;
            if ((input.mouse.y >= 165) && (input.mouse.y <= 175)) editor.ui.forceVolDrag = 2;
        }
    }
    else
    {
        if (editor.sampler.lastMouseX != input.mouse.x)
        {
            editor.sampler.lastMouseX = input.mouse.x;
            mouseX = CLAMP(editor.sampler.lastMouseX - 107, 0, 60);

            if (editor.ui.forceVolDrag == 1)
            {
                editor.vol1 = (int16_t)((mouseX * 200) / 60);
                editor.ui.updateVolFromText = true;
                showVolFromSlider();
            }
            else if (editor.ui.forceVolDrag == 2)
            {
                editor.vol2 = (int16_t)((mouseX * 200) / 60);
                editor.ui.updateVolToText = true;
                showVolToSlider();
            }
        }
    }
}

void samplerBarPressed(int8_t mouseButtonHeld)
{
    int32_t tmp32;

    if ((input.mouse.y < 0) || (input.mouse.x < 0) || (input.mouse.y >= SCREEN_H) || (input.mouse.x >= SCREEN_W))
        return;

    if (!mouseButtonHeld)
    {
        if ((input.mouse.x >= 4) && (input.mouse.x <= 315))
        {
            if (input.mouse.x < editor.sampler.dragStart)
            {
                tmp32 = editor.sampler.samOffset - editor.sampler.samDisplay;
                if (tmp32 < 0)
                    tmp32 = 0;

                if (tmp32 == editor.sampler.samOffset)
                    return;

                editor.sampler.samOffset = tmp32;

                displaySample();

                return;
            }

            if (input.mouse.x > editor.sampler.dragEnd)
            {
                tmp32 = editor.sampler.samOffset + editor.sampler.samDisplay;
                if ((tmp32 + editor.sampler.samDisplay) <= editor.sampler.samLength)
                {
                    if (tmp32 == editor.sampler.samOffset)
                        return;

                    editor.sampler.samOffset = tmp32;
                }
                else
                {
                    tmp32 = editor.sampler.samLength - editor.sampler.samDisplay;
                    if (tmp32 == editor.sampler.samOffset)
                        return;

                    editor.sampler.samOffset = tmp32;
                }

                displaySample();

                return;
            }

            editor.sampler.lastSamPos = (uint16_t)(input.mouse.x);
            editor.sampler.saveMouseX = editor.sampler.lastSamPos - editor.sampler.dragStart;

            editor.ui.forceSampleDrag = true;
        }
    }

    if (input.mouse.x != editor.sampler.lastSamPos)
    {
        editor.sampler.lastSamPos = (uint16_t)(input.mouse.x);

        tmp32 = editor.sampler.lastSamPos - editor.sampler.saveMouseX - 4;
        if (tmp32 < 0)
            tmp32 = 0;

        tmp32 = (int32_t)(((tmp32 * editor.sampler.samLength) / 311.0f) + 0.5f);
        if ((tmp32 + editor.sampler.samDisplay) <= editor.sampler.samLength)
        {
            if (tmp32 == editor.sampler.samOffset)
                return;

            editor.sampler.samOffset = tmp32;
        }
        else
        {
            tmp32 = editor.sampler.samLength - editor.sampler.samDisplay;
            if (tmp32 == editor.sampler.samOffset)
                return;

            editor.sampler.samOffset = tmp32;
        }

        displaySample();
    }
}

int32_t x_to_loopX(int32_t mouseX)
{
    int32_t offset;
    moduleSample_t *s;

    s = &modEntry->samples[editor.currSample];

    mouseX -= 3;
    if (mouseX < 0)
        mouseX = 0;

    offset = editor.sampler.samOffset + (int32_t)((((mouseX * editor.sampler.samDisplay) / (float)(SAMPLE_AREA_WIDTH))) + 0.5f);
    return (CLAMP(offset, 0, s->length));
}

void samplerEditSample(int8_t mouseButtonHeld)
{
    // EDIT SAMPLE ROUTINE (non-PT feature inspired by FT2)

    int8_t y;
    int32_t x, smp_x0, smp_x1, xDistance, smp_y0, smp_y1, yDistance;
    float s_f;
    moduleSample_t *s;

    PT_ASSERT((editor.currSample >= 0) && (editor.currSample <= 30));
    if ((editor.currSample < 0) || (editor.currSample > 30))
        return;

    if ((input.mouse.y < 0) || (input.mouse.x < 0) || (input.mouse.y >= SCREEN_H) || (input.mouse.x >= SCREEN_W))
        return;

    s = &modEntry->samples[editor.currSample];

    if (!mouseButtonHeld)
    {
        if ((input.mouse.x >= 3) && (input.mouse.x <= 316) && (input.mouse.y >= 138) && (input.mouse.y <= 201))
        {
            if (s->length == 0)
            {
                displayErrorMsg("SAMPLE LENGTH = 0");
            }
            else
            {
                editor.sampler.lastMouseX = input.mouse.x;
                editor.sampler.lastMouseY = input.mouse.y;

                editor.ui.forceSampleEdit = true;
                updateWindowTitle(MOD_IS_MODIFIED);
            }
        }

        return;
    }

    x = scr2SmpPos(input.mouse.x - 3);
    x = CLAMP(x, 0, s->length);

    if (!input.keyb.shiftKeyDown)
        y = (int8_t)(CLAMP(-(input.mouse.y - 169) * 4, -128, 127));
    else
        y = (int8_t)(CLAMP(-(editor.sampler.lastMouseY - 169) * 4, -128, 127));

    if (y >= 124) // kludge
        y  = 127;

    modEntry->sampleData[s->offset + x] = y;

    // interpolate x gaps
    if (input.mouse.x != editor.sampler.lastMouseX)
    {
        smp_y0 = CLAMP(-(editor.sampler.lastMouseY - 169) * 4, -128, 127);
        if (smp_y0 >= 124) // kludge
            smp_y0  = 127;

        smp_y1 = y;
        yDistance = smp_y1 - smp_y0;

        if (input.mouse.x > editor.sampler.lastMouseX)
        {
            smp_x1 = x;
            smp_x0 = scr2SmpPos(editor.sampler.lastMouseX - 3);
            smp_x0 = CLAMP(smp_x0, 0, s->length);

            xDistance = smp_x1 - smp_x0;
            if (xDistance > 0)
            {
                for (x = smp_x0; x < smp_x1; ++x)
                {
                    PT_ASSERT(x < s->length);

                    s_f = smp_y0 + ((x - smp_x0) / (float)(xDistance)) * (float)(yDistance);
                    modEntry->sampleData[s->offset + x] = (int8_t)(s_f + 0.5f);
                }
            }
        }
        else if (input.mouse.x < editor.sampler.lastMouseX)
        {
            smp_x0 = x;
            smp_x1 = scr2SmpPos(editor.sampler.lastMouseX - 3);
            smp_x1 = CLAMP(smp_x1, 0, s->length);

            xDistance = smp_x1 - smp_x0;
            if (xDistance > 0)
            {
                for (x = smp_x0; x < smp_x1; ++x)
                {
                    PT_ASSERT(x < s->length);

                    s_f = smp_y0 + ((smp_x1 - x) / (float)(xDistance)) * (float)(yDistance);
                    modEntry->sampleData[s->offset + x] = (int8_t)(s_f + 0.5f);
                }
            }
        }

        editor.sampler.lastMouseX = input.mouse.x;

        if (!input.keyb.shiftKeyDown)
            editor.sampler.lastMouseY = input.mouse.y;
    }

    displaySample();
}

void samplerSamplePressed(int8_t mouseButtonHeld)
{
    int16_t mouseX;
    int32_t tmpPos;
    moduleSample_t *s;

    PT_ASSERT((editor.currSample >= 0) && (editor.currSample <= 30));
    if ((editor.currSample < 0) || (editor.currSample > 30))
        return;

    if ((input.mouse.y < 0) || (input.mouse.x < 0) || (input.mouse.y >= SCREEN_H) || (input.mouse.x >= SCREEN_W))
        return;

    if (!mouseButtonHeld)
    {
        if (input.mouse.y < 142)
        {
            if ((input.mouse.x >= editor.sampler.loopStartPos) && (input.mouse.x <= (editor.sampler.loopStartPos + 3)))
            {
                editor.ui.leftLoopPinMoving  = true;
                editor.ui.rightLoopPinMoving = false;
                editor.ui.sampleMarkingPos   = 1;

                editor.sampler.lastMouseX = input.mouse.x;

                return;
            }
            else if ((input.mouse.x >= (editor.sampler.loopEndPos + 3)) && (input.mouse.x <= (editor.sampler.loopEndPos + 6)))
            {
                editor.ui.rightLoopPinMoving = true;
                editor.ui.leftLoopPinMoving  = false;
                editor.ui.sampleMarkingPos   = 1;

                editor.sampler.lastMouseX = input.mouse.x;

                return;
            }
        }
    }

    mouseX = (int16_t)(input.mouse.x);
    s = &modEntry->samples[editor.currSample];

    if (editor.ui.leftLoopPinMoving)
    {
        if (editor.sampler.lastMouseX != mouseX)
        {
            editor.sampler.lastMouseX = mouseX;

            mouseX += 2;

            tmpPos = (x_to_loopX(mouseX) - s->loopStart) & 0xFFFFFFFE;
            if ((s->loopStart + tmpPos) >= ((s->loopStart + s->loopLength) - 2))
            {
                s->loopStart  = (s->loopStart + s->loopLength) - 2;
                s->loopLength = 2;
            }
            else
            {
                s->loopStart = s->loopStart + tmpPos;

                s->loopLength -= tmpPos;
                if (s->loopLength < 2)
                    s->loopLength = 2;
            }

            editor.ui.updateCurrSampleRepeat = true;
            editor.ui.updateCurrSampleReplen = true;

            setLoopSprites();
            mixerUpdateLoops();
            updateWindowTitle(MOD_IS_MODIFIED);
        }

        return;
    }

    if (editor.ui.rightLoopPinMoving)
    {
        if (editor.sampler.lastMouseX != mouseX)
        {
            editor.sampler.lastMouseX = mouseX;

            s = &modEntry->samples[editor.currSample];

            if (--mouseX < 0)
                  mouseX = 0;

            s->loopLength = (x_to_loopX(mouseX) - s->loopStart) & 0xFFFFFFFE;
            if (s->loopLength < 2)
                s->loopLength = 2;

            editor.ui.updateCurrSampleRepeat = true;
            editor.ui.updateCurrSampleReplen = true;

            setLoopSprites();
            mixerUpdateLoops();
            updateWindowTitle(MOD_IS_MODIFIED);
        }

        return;
    }

    if (!mouseButtonHeld)
    {
        if ((mouseX < 3) || (mouseX > 319))
            return;

        editor.ui.sampleMarkingPos = (int16_t)(mouseX);
        editor.sampler.lastSamPos  = editor.ui.sampleMarkingPos;

        invertRange();
        if (s->length == 0)
        {
            editor.markStartOfs = 0;
            editor.markEndOfs   = 0;
        }
        else
        {
            
            editor.markStartOfs = scr2SmpPos(editor.ui.sampleMarkingPos - 3);
            editor.markEndOfs   = scr2SmpPos(editor.ui.sampleMarkingPos - 3);

            if (editor.markEndOfs > s->length)
                editor.markEndOfs = s->length;
        }
        invertRange();

        if (s->length == 0)
            editor.markStartOfs = 0;

        else
            editor.samplePos = scr2SmpPos(mouseX - 3);

        updateSamplePos();

        return;
    }

    mouseX = CLAMP(mouseX, 3, 319);

    if (mouseX != editor.sampler.lastSamPos)
    {
        editor.sampler.lastSamPos = (uint16_t)(mouseX);

        invertRange();
        if (s->length == 0)
        {
            editor.markStartOfs = 0;
            editor.markEndOfs   = 0;
        }
        else
        {
            if (editor.sampler.lastSamPos > editor.ui.sampleMarkingPos)
            {
                editor.markStartOfs = scr2SmpPos(editor.ui.sampleMarkingPos - 3);
                editor.markEndOfs   = scr2SmpPos(editor.sampler.lastSamPos  - 3);
            }
            else
            {
                editor.markStartOfs = scr2SmpPos(editor.sampler.lastSamPos  - 3);
                editor.markEndOfs   = scr2SmpPos(editor.ui.sampleMarkingPos - 3);
            }

            if (editor.markEndOfs > s->length)
                editor.markEndOfs = s->length;
        }
        invertRange();
    }

    if (s->length == 0)
        editor.samplePos = 0;
    else
        editor.samplePos = scr2SmpPos(mouseX - 3);

    updateSamplePos();
}

void samplerLoopToggle(void)
{
    moduleSample_t *s;

    PT_ASSERT((editor.currSample >= 0) && (editor.currSample <= 30));
    if ((editor.currSample < 0) || (editor.currSample > 30))
        return;

    s = &modEntry->samples[editor.currSample];
    if (s->length < 2)
        return;

    turnOffVoices();

    if ((s->loopStart + s->loopLength) > 2)
    {
        // disable loop

        editor.sampler.tmpLoopStart  = s->loopStart;
        editor.sampler.tmpLoopLength = s->loopLength;

        s->loopStart  = 0;
        s->loopLength = 2;
    }
    else
    {
        // enable loop

        if ((editor.sampler.tmpLoopStart == 0) && (editor.sampler.tmpLoopLength == 0))
        {
            s->loopStart  = 0;
            s->loopLength = s->length;
        }
        else
        {
            s->loopStart  = editor.sampler.tmpLoopStart;
            s->loopLength = editor.sampler.tmpLoopLength;

            if ((s->loopStart + s->loopLength) > s->length)
            {
                s->loopStart  = 0;
                s->loopLength = s->length;
            }
        }
    }

    editor.ui.updateCurrSampleRepeat = true;
    editor.ui.updateCurrSampleReplen = true;

    displaySample();
    mixerUpdateLoops();
    updateWindowTitle(MOD_IS_MODIFIED);
}

void exitFromSam(void)
{
    editor.ui.samplerScreenShown = false;
    memcpy(&pixelBuffer[121 * SCREEN_W], &trackerFrameBMP[121 * SCREEN_W], 320 * 134 * sizeof (int32_t));

    updateCursorPos();
    setLoopSprites();

    editor.ui.updateStatusText   = true;
    editor.ui.updateSongSize     = true;
    editor.ui.updateSongTiming   = true;
    editor.ui.updateSongBPM      = true;
    editor.ui.updateCurrPattText = true;
    editor.ui.updatePatternData  = true;

    editor.markStartOfs = 0;
    editor.markEndOfs   = 0;
}

void samplerScreen(void)
{
    if (editor.ui.samplerScreenShown)
    {
        exitFromSam();
        return;
    }

    editor.ui.samplerScreenShown = true;
    memcpy(&pixelBuffer[(121 * SCREEN_W)], samplerScreenBMP, 320 * 134 * sizeof (int32_t));
    hideSprite(SPRITE_PATTERN_CURSOR);

    editor.ui.updateStatusText   = true;
    editor.ui.updateSongSize     = true;
    editor.ui.updateSongTiming   = true;
    editor.ui.updateResampleNote = true;
    editor.ui.update9xxPos       = true;

    redrawSample();
}

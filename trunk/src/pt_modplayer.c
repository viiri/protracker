// Very accurate C port of ProTracker 2.3D's replayer by 8bitbubsy, slightly modified.
// Earlier versions of the PT clone used a completely different and less accurate replayer.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "pt_header.h"
#include "pt_audio.h"
#include "pt_helpers.h"
#include "pt_palette.h"
#include "pt_tables.h"
#include "pt_modloader.h"
#include "pt_config.h"
#include "pt_sampler.h"
#include "pt_visuals.h"
#include "pt_textout.h"
#include "pt_terminal.h"
#include "pt_scopes.h"

extern int8_t forceMixerOff; // pt_audio.c

static int8_t pBreakPosition, posJumpAssert, pBreakFlag, oldRow, modPattern;
static uint8_t pattDelTime, setBPMFlag, updateUIPositions, lowMask = 0xFF;
static uint8_t pattDelTime2, modHasBeenPlayed, oldSpeed;
static int16_t modOrder, oldPattern, oldOrder;
static uint16_t modBPM, oldBPM;

void modSetSpeed(uint8_t speed)
{
    editor.modSpeed = speed;
    modEntry->currSpeed = speed;
    editor.modTick = 0;
}

void doStopIt(void)
{
    moduleChannel_t *c;
    uint8_t i;

    pattDelTime        = 0;
    pattDelTime2       = 0;
    editor.playMode    = PLAY_MODE_NORMAL;
    editor.currMode    = MODE_IDLE;
    editor.songPlaying = false;

    pointerSetMode(POINTER_MODE_IDLE, DO_CARRY);

    for (i = 0; i < AMIGA_VOICES; ++i)
    {
        c = &modEntry->channels[i];

        c->n_wavecontrol = 0;
        c->n_glissfunk   = 0;
        c->n_finetune    = 0;
        c->n_loopcount   = 0;
    }
}

void setPattern(int16_t pattern)
{
    modPattern = pattern;
    if (modPattern > (MAX_PATTERNS - 1))
        modPattern =  MAX_PATTERNS - 1;

    modEntry->currPattern = modPattern;
}

void storeTempVariables(void) // this one is accessed in other files, so non-static
{
    oldBPM     = modEntry->currBPM;
    oldRow     = modEntry->currRow;
    oldOrder   = modEntry->currOrder;
    oldSpeed   = modEntry->currSpeed;
    oldPattern = modEntry->currPattern;
}

static void setVUMeterHeight(moduleChannel_t *ch)
{
    int8_t vol;

    vol = ch->n_volume;
    if ((ch->n_cmd & 0x0F00) == 0x0C00) // handle Cxx effect
    {
        vol = ch->n_cmd & 0xFF;
        if (vol > 0x40)
            vol = 0x40;
    }

    if (!editor.muted[ch->n_chanindex])
        editor.vuMeterVolumes[ch->n_chanindex] = vuMeterHeights[vol];
}

static void updateFunk(moduleChannel_t *ch)
{
    int8_t funkspeed;

    funkspeed = ch->n_glissfunk >> 4;
    if (funkspeed > 0)
    {
        ch->n_funkoffset += funkTable[funkspeed];
        if (ch->n_funkoffset >= 128)
        {
            ch->n_funkoffset = 0;

            if ((ch->n_loopstart != NULL) && (ch->n_wavestart != NULL)) // SAFETY BUG FIX
            {
                if (++ch->n_wavestart >= (ch->n_loopstart + ch->n_replen))
                      ch->n_wavestart  =  ch->n_loopstart;

                if (ch->n_wavestart != NULL) // SAFETY BUG FIX
                    *ch->n_wavestart = -1 - *ch->n_wavestart;
            }
        }
    }
}

static void setGlissControl(moduleChannel_t *ch)
{
    ch->n_glissfunk = (ch->n_glissfunk & 0xF0) | (ch->n_cmd & 0x000F);
}

static void setVibratoControl(moduleChannel_t *ch)
{
    ch->n_wavecontrol = (ch->n_wavecontrol & 0xF0) | (ch->n_cmd & 0x000F);
}

static void setFineTune(moduleChannel_t *ch)
{
    ch->n_finetune = ch->n_cmd & 0x000F;
}

static void jumpLoop(moduleChannel_t *ch)
{
    uint8_t tempParam;

    if (!editor.modTick)
    {
        if (!(ch->n_cmd & 0x000F))
        {
            ch->n_pattpos = modEntry->row;
        }
        else
        {
            if (!ch->n_loopcount)
            {
                ch->n_loopcount = ch->n_cmd & 0x000F;
            }
            else
            {
                if (!--ch->n_loopcount)
                    return;
            }

            pBreakPosition = ch->n_pattpos;
            pBreakFlag = 1;

            if (editor.isWAVRendering)
            {
                for (tempParam = pBreakPosition; tempParam <= modEntry->row; ++tempParam)
                    editor.rowVisitTable[(modOrder * MOD_ROWS) + tempParam] = false;
            }
        }
    }
}

static void setTremoloControl(moduleChannel_t *ch)
{
    ch->n_wavecontrol = ((ch->n_cmd & 0x000F) << 4) | (ch->n_wavecontrol & 0x0F);
}

static void karplusStrong(moduleChannel_t *ch)
{
    (void)(ch);
    // this effect is horrible, I'm not implementing it.
}

static void doRetrg(moduleChannel_t *ch)
{
    paulaSetData(ch->n_chanindex,   ch->n_start); // n_start is increased on 9xx
    paulaSetLength(ch->n_chanindex, ch->n_length);
    paulaSetPeriod(ch->n_chanindex, ch->n_period);
    paulaRestartDMA(ch->n_chanindex);

    // these take effect after the current DMA cycle is done
    paulaSetData(ch->n_chanindex,   ch->n_loopstart);
    paulaSetLength(ch->n_chanindex, ch->n_replen);

    updateSpectrumAnalyzer(ch->n_chanindex, ch->n_volume, ch->n_period);
    setVUMeterHeight(ch);
}

static void retrigNote(moduleChannel_t *ch)
{
    if (ch->n_cmd & 0x000F)
    {
        if (!editor.modTick)
        {
            if (ch->n_note & 0x0FFF)
                return;
        }

        if (!(editor.modTick % (ch->n_cmd & 0x000F)))
            doRetrg(ch);
    }
}

static void volumeSlide(moduleChannel_t *ch)
{
    uint8_t cmd;

    cmd = ch->n_cmd & 0x00FF;
    if (!(cmd & 0xF0))
    {
        ch->n_volume -= (cmd & 0x0F);
        if (ch->n_volume < 0)
            ch->n_volume = 0;
    }
    else
    {
        ch->n_volume += (cmd >> 4);
        if (ch->n_volume > 64)
            ch->n_volume = 64;
    }
}

static void volumeFineUp(moduleChannel_t *ch)
{
    if (!editor.modTick)
    {
        ch->n_volume += (ch->n_cmd & 0x000F);
        if (ch->n_volume > 64)
            ch->n_volume = 64;
    }
}

static void volumeFineDown(moduleChannel_t *ch)
{
    if (!editor.modTick)
    {
        ch->n_volume -= (ch->n_cmd & 0x000F);
        if (ch->n_volume < 0)
            ch->n_volume = 0;
    }
}

static void noteCut(moduleChannel_t *ch)
{
    if (editor.modTick == (ch->n_cmd & 0x000F))
        ch->n_volume = 0;
}

static void noteDelay(moduleChannel_t *ch)
{
    if (editor.modTick == (ch->n_cmd & 0x000F))
    {
        if (ch->n_note & 0x0FFF)
            doRetrg(ch);
    }
}

static void patternDelay(moduleChannel_t *ch)
{
    if (!editor.modTick)
    {
        if (!pattDelTime2)
            pattDelTime = (ch->n_cmd & 0x000F) + 1;
    }
}

static void funkIt(moduleChannel_t *ch)
{
    if (!editor.modTick)
    {
        ch->n_glissfunk = ((ch->n_cmd & 0x000F) << 4) | (ch->n_glissfunk & 0x0F);

        if (ch->n_glissfunk & 0xF0)
            updateFunk(ch);
    }
}

static void positionJump(moduleChannel_t *ch)
{
    modOrder       = (ch->n_cmd & 0x00FF) - 1; // 0xFF (B00) jumps to pat 0
    pBreakPosition = 0;
    posJumpAssert  = 1;
}

static void volumeChange(moduleChannel_t *ch)
{
    ch->n_volume = ch->n_cmd & 0x00FF;
    if (ch->n_volume > 64)
        ch->n_volume = 64;
}

static void patternBreak(moduleChannel_t *ch)
{
    pBreakPosition = (((ch->n_cmd & 0x00F0) >> 4) * 10) + (ch->n_cmd & 0x000F);
    if (pBreakPosition > 63)
        pBreakPosition = 0;

    posJumpAssert = 1;
}

static void setSpeed(moduleChannel_t *ch)
{
    if (ch->n_cmd & 0x00FF)
    {
        editor.modTick = 0;

        if ((editor.timingMode == TEMPO_MODE_VBLANK) || ((ch->n_cmd & 0x00FF) < 32))
            modSetSpeed(ch->n_cmd & 0x00FF);
        else
            setBPMFlag = ch->n_cmd & 0x00FF; // CIA doesn't refresh its registers until the next interrupt, so change it later
    }
    else
    {
        editor.songPlaying = false;
        editor.playMode    = PLAY_MODE_NORMAL;
        editor.currMode    = MODE_IDLE;
        pointerSetMode(POINTER_MODE_IDLE, DO_CARRY);
    }
}

static void arpeggio(moduleChannel_t *ch)
{
    uint8_t i, dat;
    const int16_t *arpPointer;

    dat = editor.modTick % 3;
    if (!dat)
    {
        paulaSetPeriod(ch->n_chanindex, ch->n_period);
    }
    else
    {
             if (dat == 1) dat = (ch->n_cmd & 0x00F0) >> 4;
        else if (dat == 2) dat =  ch->n_cmd & 0x000F;

        arpPointer = &periodTable[37 * ch->n_finetune];
        for (i = 0; i < 37; ++i)
        {
            if (ch->n_period >= arpPointer[i])
            {
                paulaSetPeriod(ch->n_chanindex, arpPointer[i + dat]);
                break;
            }
        }
    }
}

static void portaUp(moduleChannel_t *ch)
{
    ch->n_period -= ((ch->n_cmd & 0x00FF) & lowMask);
    lowMask = 0xFF;

    if ((ch->n_period & 0x0FFF) < 113)
        ch->n_period = (ch->n_period & 0xF000) | 113;

    paulaSetPeriod(ch->n_chanindex, ch->n_period & 0x0FFF);
}

static void portaDown(moduleChannel_t *ch)
{
    ch->n_period += ((ch->n_cmd & 0x00FF) & lowMask);
    lowMask = 0xFF;

    if ((ch->n_period & 0x0FFF) > 856)
        ch->n_period = (ch->n_period & 0xF000) | 856;

    paulaSetPeriod(ch->n_chanindex, ch->n_period & 0x0FFF);
}

static void filterOnOff(moduleChannel_t *ch)
{
    setLEDFilter(!(ch->n_cmd & 0x0001));
}

static void finePortaUp(moduleChannel_t *ch)
{
    if (!editor.modTick)
    {
        lowMask = 0x0F;
        portaUp(ch);
    }
}

static void finePortaDown(moduleChannel_t *ch)
{
    if (!editor.modTick)
    {
        lowMask = 0x0F;
        portaDown(ch);
    }
}

static void setTonePorta(moduleChannel_t *ch)
{
    uint8_t i;
    const int16_t *portaPointer;
    uint16_t note;

    note = ch->n_note & 0x0FFF;
    portaPointer = &periodTable[37 * ch->n_finetune];

    i = 0;
    for (;;)
    {
        // portaPointer[36] = 0, so i=36 is safe
        if (note >= portaPointer[i])
            break;

        if (++i >= 37)
        {
            i = 35;
            break;
        }
    }

    if ((ch->n_finetune & 8) && i) i--;

    ch->n_wantedperiod  = portaPointer[i];
    ch->n_toneportdirec = 0;

         if (ch->n_period == ch->n_wantedperiod) ch->n_wantedperiod  = 0;
    else if (ch->n_period  > ch->n_wantedperiod) ch->n_toneportdirec = 1;
}

static void tonePortNoChange(moduleChannel_t *ch)
{
    uint8_t i;
    const int16_t *portaPointer;

    if (ch->n_wantedperiod)
    {
        if (ch->n_toneportdirec)
        {
            ch->n_period -= ch->n_toneportspeed;
            if (ch->n_period <= ch->n_wantedperiod)
            {
                ch->n_period = ch->n_wantedperiod;
                ch->n_wantedperiod = 0;
            }
        }
        else
        {
            ch->n_period += ch->n_toneportspeed;
            if (ch->n_period >= ch->n_wantedperiod)
            {
                ch->n_period = ch->n_wantedperiod;
                ch->n_wantedperiod = 0;
            }
        }

        if (!(ch->n_glissfunk & 0x0F))
        {
            paulaSetPeriod(ch->n_chanindex, ch->n_period);
        }
        else
        {
            portaPointer = &periodTable[37 * ch->n_finetune];

            i = 0;
            for (;;)
            {
                // portaPointer[36] = 0, so i=36 is safe
                if (ch->n_period >= portaPointer[i])
                    break;

                if (++i >= 37)
                {
                    i = 35;
                    break;
                }
            }

            paulaSetPeriod(ch->n_chanindex, portaPointer[i]);
        }
    }
}

static void tonePortamento(moduleChannel_t *ch)
{
    if (ch->n_cmd & 0x00FF)
    {
        ch->n_toneportspeed = ch->n_cmd & 0x00FF;
        ch->n_cmd &= 0xFF00;
    }

    tonePortNoChange(ch);
}

static void vibratoNoChange(moduleChannel_t *ch)
{
    uint8_t vibratoTemp;
    int16_t vibratoData;

    vibratoTemp = (ch->n_vibratopos / 4) & 31;
    vibratoData = ch->n_wavecontrol & 3;

    if (!vibratoData)
    {
        vibratoData = vibratoTable[vibratoTemp];
    }
    else
    {
        if (vibratoData == 1)
        {
            if (ch->n_vibratopos < 0)
                vibratoData = 255 - (vibratoTemp * 8);
            else
                vibratoData = vibratoTemp * 8;
        }
        else
        {
            vibratoData = 255;
        }
    }

    vibratoData = (vibratoData * (ch->n_vibratocmd & 0x0F)) / 128;

    if (ch->n_vibratopos < 0)
        vibratoData = ch->n_period - vibratoData;
    else
        vibratoData = ch->n_period + vibratoData;

    paulaSetPeriod(ch->n_chanindex, vibratoData);

    ch->n_vibratopos += ((ch->n_vibratocmd >> 4) * 4);
}

static void vibrato(moduleChannel_t *ch)
{
    if (ch->n_cmd & 0x00FF)
    {
        if (ch->n_cmd & 0x000F)
            ch->n_vibratocmd = (ch->n_vibratocmd & 0xF0) | (ch->n_cmd & 0x000F);

        if (ch->n_cmd & 0x00F0)
            ch->n_vibratocmd = (ch->n_cmd & 0x00F0) | (ch->n_vibratocmd & 0x0F);
    }

    vibratoNoChange(ch);
}

static void tonePlusVolSlide(moduleChannel_t *ch)
{
    tonePortNoChange(ch);
    volumeSlide(ch);
}

static void vibratoPlusVolSlide(moduleChannel_t *ch)
{
    vibratoNoChange(ch);
    volumeSlide(ch);
}

static void tremolo(moduleChannel_t *ch)
{
    int8_t tremoloTemp;
    int16_t tremoloData;

    if (ch->n_cmd & 0x00FF)
    {
        if (ch->n_cmd & 0x000F)
            ch->n_tremolocmd = (ch->n_tremolocmd & 0xF0) | (ch->n_cmd & 0x000F);

        if (ch->n_cmd & 0x00F0)
            ch->n_tremolocmd = (ch->n_cmd & 0x00F0) | (ch->n_tremolocmd & 0x0F);
    }

    tremoloTemp = (ch->n_tremolopos / 4) & 31;
    tremoloData = (ch->n_wavecontrol >> 4) & 3;

    if (!tremoloData)
    {
        tremoloData = vibratoTable[tremoloTemp];
    }
    else
    {
        if (tremoloData == 1)
        {
            if (ch->n_vibratopos < 0) // PT bug, should've been n_tremolopos
                tremoloData = 255 - (tremoloTemp * 8);
            else
                tremoloData = tremoloTemp * 8;
        }
        else
        {
            tremoloData = 255;
        }
    }

    tremoloData = (tremoloData * (ch->n_tremolocmd & 0x0F)) / 64;

    if (ch->n_tremolopos < 0)
    {
        tremoloData = ch->n_volume - tremoloData;
        if (tremoloData < 0)
            tremoloData = 0;
    }
    else
    {
        tremoloData = ch->n_volume + tremoloData;
        if (tremoloData > 64)
            tremoloData = 64;
    }

    paulaSetVolume(ch->n_chanindex, tremoloData);

    ch->n_tremolopos += ((ch->n_tremolocmd >> 4) * 4);
}

static void sampleOffset(moduleChannel_t *ch)
{
    uint16_t newOffset;

    if (ch->n_cmd & 0x00FF)
        ch->n_sampleoffset = ch->n_cmd & 0x00FF;

    newOffset = ch->n_sampleoffset * 256;

    if ((ch->n_length <= 65534) && (newOffset < ch->n_length))
    {
        ch->n_length -= newOffset;
        ch->n_start  += newOffset;
    }
    else
    {
        ch->n_length = 2;
    }
}

static void E_Commands(moduleChannel_t *ch)
{
    switch ((ch->n_cmd & 0x00F0) >> 4)
    {
        case 0x00: filterOnOff(ch);       break;
        case 0x01: finePortaUp(ch);       break;
        case 0x02: finePortaDown(ch);     break;
        case 0x03: setGlissControl(ch);   break;
        case 0x04: setVibratoControl(ch); break;
        case 0x05: setFineTune(ch);       break;
        case 0x06: jumpLoop(ch);          break;
        case 0x07: setTremoloControl(ch); break;
        case 0x08: karplusStrong(ch);     break;
        case 0x09: retrigNote(ch);        break;
        case 0x0A: volumeFineUp(ch);      break;
        case 0x0B: volumeFineDown(ch);    break;
        case 0x0C: noteCut(ch);           break;
        case 0x0D: noteDelay(ch);         break;
        case 0x0E: patternDelay(ch);      break;
        case 0x0F: funkIt(ch);            break;
        default: break;
    }
}

static void checkMoreEffects(moduleChannel_t *ch)
{
    switch ((ch->n_cmd & 0x0F00) >> 8)
    {
        case 0x09: sampleOffset(ch); break;
        case 0x0B: positionJump(ch); break;
        case 0x0D: patternBreak(ch); break;
        case 0x0E: E_Commands(ch);   break;
        case 0x0F: setSpeed(ch);     break;
        case 0x0C: volumeChange(ch); break;

        default: paulaSetPeriod(ch->n_chanindex, ch->n_period); break;
    }
}

static void checkEffects(moduleChannel_t *ch)
{
    updateFunk(ch);

    if (ch->n_cmd & 0x0FFF)
    {
        switch ((ch->n_cmd & 0x0F00) >> 8)
        {
            case 0x00: arpeggio(ch);            break;
            case 0x01: portaUp(ch);             break;
            case 0x02: portaDown(ch);           break;
            case 0x03: tonePortamento(ch);      break;
            case 0x04: vibrato(ch);             break;
            case 0x05: tonePlusVolSlide(ch);    break;
            case 0x06: vibratoPlusVolSlide(ch); break;
            case 0x0E: E_Commands(ch);          break;
            case 0x07:
                paulaSetPeriod(ch->n_chanindex, ch->n_period);
                tremolo(ch);
            break;
            case 0x0A:
                paulaSetPeriod(ch->n_chanindex, ch->n_period);
                volumeSlide(ch);
            break;

            default: paulaSetPeriod(ch->n_chanindex, ch->n_period); break;
        }
    }

    paulaSetVolume(ch->n_chanindex, ch->n_volume);
}

static void setPeriod(moduleChannel_t *ch)
{
    uint8_t i;
    uint16_t note;

    note = ch->n_note & 0x0FFF;
    for (i = 0; i < 37; ++i)
    {
        // periodTable[36] = 0, so i=36 is safe
        if (note >= periodTable[i])
            break;
    }

    // BUG: yes it's 'safe' if i=37 because of padding at the end of period table
    ch->n_period = periodTable[(37 * ch->n_finetune) + i];

    if ((ch->n_cmd & 0x0FF0) != 0x0ED0) // no note delay
    {
        if (!(ch->n_wavecontrol & 0x04)) ch->n_vibratopos = 0;
        if (!(ch->n_wavecontrol & 0x40)) ch->n_tremolopos = 0;

        paulaSetLength(ch->n_chanindex, ch->n_length);
        paulaSetData(ch->n_chanindex,   ch->n_start);

        if (ch->n_start == NULL)
        {
            ch->n_loopstart = NULL;
            paulaSetLength(ch->n_chanindex, 2);
            ch->n_replen = 2;
        }

        paulaSetPeriod(ch->n_chanindex, ch->n_period);
        paulaRestartDMA(ch->n_chanindex);

        updateSpectrumAnalyzer(ch->n_chanindex, ch->n_volume, ch->n_period);
        setVUMeterHeight(ch);
    }

    checkMoreEffects(ch);
}

static void checkMetronome(moduleChannel_t *ch, note_t *note)
{
    if (editor.metroFlag && (editor.metroChannel > 0))
    {
        if ((ch->n_chanindex == (editor.metroChannel - 1)) && ((modEntry->row % editor.metroSpeed) == 0))
        {
            note->sample = 0x1F;
            note->period = (((modEntry->row / editor.metroSpeed) % editor.metroSpeed) == 0) ? 160 : 214;
        }
    }
}

static void playVoice(moduleChannel_t *ch)
{
    uint8_t cmd;
    moduleSample_t *s;
    note_t note;

    if (!ch->n_note && !ch->n_cmd)
        paulaSetPeriod(ch->n_chanindex, ch->n_period);

    note = modEntry->patterns[modPattern][(modEntry->row * AMIGA_VOICES) + ch->n_chanindex];
    checkMetronome(ch, &note);

    ch->n_note = note.period;
    ch->n_cmd  = (note.command << 8) | note.param;

    if ((note.sample >= 1) && (note.sample <= 31)) // SAFETY BUG FIX: don't handle sample-numbers >31
    {
        ch->n_samplenum = note.sample - 1;
        s = &modEntry->samples[ch->n_samplenum];

        ch->n_start = &modEntry->sampleData[s->offset];
        ch->n_finetune = s->fineTune;
        ch->n_volume   = s->volume;
        ch->n_length   = s->length;
        ch->n_replen   = s->loopLength;

        if (s->loopStart > 0)
        {
            ch->n_loopstart = ch->n_start + s->loopStart;
            ch->n_wavestart = ch->n_loopstart;
            ch->n_length    = s->loopStart + ch->n_replen;
        }
        else
        {
            ch->n_loopstart = ch->n_start;
            ch->n_wavestart = ch->n_start;
        }

        if (ch->n_length == 0)
            ch->n_loopstart = ch->n_wavestart = &modEntry->sampleData[RESERVED_SAMPLE_OFFSET]; // dummy sample
    }

    if (ch->n_note & 0x0FFF)
    {
        if ((ch->n_cmd & 0x0FF0) == 0x0E50) // set finetune
        {
            setFineTune(ch);
            setPeriod(ch);
        }
        else
        {
            cmd = (ch->n_cmd & 0x0F00) >> 8;
            if ((cmd == 0x03) || (cmd == 0x05))
            {
                setVUMeterHeight(ch);
                setTonePorta(ch);
                checkMoreEffects(ch);
            }
            else if (cmd == 0x09)
            {
                checkMoreEffects(ch);
                setPeriod(ch);
            }
            else
            {
                setPeriod(ch);
            }
        }
    }
    else
    {
        checkMoreEffects(ch);
    }
}

static void nextPosition(void)
{
    modEntry->row  = pBreakPosition;
    pBreakPosition = 0;
    posJumpAssert  = false;

    if ((editor.playMode != PLAY_MODE_PATTERN) ||
        ((editor.currMode == MODE_RECORD) && (editor.recordMode != RECORD_PATT)))
    {
        if (editor.stepPlayEnabled)
        {
            doStopIt();

            editor.stepPlayEnabled   = false;
            editor.stepPlayBackwards = false;

            if (!editor.isWAVRendering && !editor.isSMPRendering)
                modEntry->currRow = modEntry->row;

            return;
        }

        modOrder = (modOrder + 1) & 0x7F;
        if (modOrder >= modEntry->head.orderCount)
        {
            modOrder = 0;
            modHasBeenPlayed = true;
        }

        modPattern = modEntry->head.order[modOrder];
        if (modPattern > (MAX_PATTERNS - 1))
            modPattern =  MAX_PATTERNS - 1;

        updateUIPositions = true;
    }
}

int8_t intMusic(void)
{
    uint8_t i;
    int16_t *patt;
    moduleChannel_t *c;

    if (updateUIPositions)
    {
        updateUIPositions = false;

        if (!editor.isWAVRendering && !editor.isSMPRendering)
        {
            if (editor.playMode != PLAY_MODE_PATTERN)
            {
                modEntry->currOrder   = modOrder;
                modEntry->currPattern = modPattern;

                patt = &modEntry->head.order[modOrder];
                editor.currPatternDisp   = patt;
                editor.currPosEdPattDisp = patt;
                editor.currPatternDisp   = patt;
                editor.currPosEdPattDisp = patt;

                if (editor.ui.posEdScreenShown)
                    editor.ui.updatePosEd = true;

                editor.ui.updateSongPos      = true;
                editor.ui.updateSongPattern  = true;
                editor.ui.updateCurrPattText = true;
            }

            //editor.ui.updatePatternData = true;
        }
    }

    // PT quirk: CIA refreshes its timer values on the next interrupt, so do the real tempo change here
    if (setBPMFlag != 0)
    {
        modSetTempo(setBPMFlag);
        setBPMFlag = 0;
    }

    if (editor.isWAVRendering && (editor.modTick == 0))
        editor.rowVisitTable[(modOrder * MOD_ROWS) + modEntry->row] = true;

    if (!editor.stepPlayEnabled)
        editor.modTick++;

    if ((editor.modTick >= editor.modSpeed) || editor.stepPlayEnabled)
    {
        editor.modTick = 0;

        if (!pattDelTime2)
        {
            for (i = 0; i < AMIGA_VOICES; ++i)
            {
                c = &modEntry->channels[i];

                playVoice(c);
                paulaSetVolume(i, c->n_volume);

                // these take effect after the current DMA cycle is done
                paulaSetData(i, c->n_loopstart);
                paulaSetLength(i, c->n_replen);
            }
        }
        else
        {
            for (i = 0; i < AMIGA_VOICES; ++i)
                checkEffects(&modEntry->channels[i]);
        }

        if (!editor.isWAVRendering && !editor.isSMPRendering)
        {
            modEntry->currRow = modEntry->row;
            editor.ui.updatePatternData = true;
        }

        if (!editor.stepPlayBackwards)
        {
            modEntry->row++;
            modEntry->rowsCounter++;
        }

        if (pattDelTime)
        {
            pattDelTime2 = pattDelTime;
            pattDelTime  = 0;
        }

        if (pattDelTime2)
        {
            pattDelTime2--;
            if (pattDelTime2)
                modEntry->row--;
        }

        if (pBreakFlag)
        {
            modEntry->row = pBreakPosition;
            pBreakPosition = 0;
            pBreakFlag = 0;
        }

        if (editor.blockMarkFlag)
            editor.ui.updateStatusText = true;

        if (editor.stepPlayEnabled)
        {
            doStopIt();

            modEntry->currRow = modEntry->row & 0x3F;
            editor.ui.updatePatternData = true;

            editor.stepPlayEnabled      = false;
            editor.stepPlayBackwards    = false;
            editor.ui.updatePatternData = true;

            return (true);
        }

        if ((modEntry->row >= MOD_ROWS) || posJumpAssert)
        {
            if (editor.isSMPRendering)
                modHasBeenPlayed = true;

            nextPosition();
        }

        if (editor.isWAVRendering && !pattDelTime2 && editor.rowVisitTable[(modOrder * MOD_ROWS) + modEntry->row])
            modHasBeenPlayed = true;
    }
    else
    {
        for (i = 0; i < AMIGA_VOICES; ++i)
            checkEffects(&modEntry->channels[i]);

        if (posJumpAssert)
            nextPosition();
    }

    if ((editor.isSMPRendering || editor.isWAVRendering) && modHasBeenPlayed && (editor.modTick == (editor.modSpeed - 1)))
    {
        modHasBeenPlayed = false;
        return (false);
    }

    return (true);
}

void modSetPattern(uint8_t pattern)
{
    modPattern = pattern;
    modEntry->currPattern = modPattern;
    editor.ui.updateCurrPattText = true;
}

void modSetPos(int16_t order, int16_t row)
{
    int16_t posEdPos;

    if (row != -1)
    {
        row = CLAMP(row, 0, 63);

        editor.modTick    = 0;
        modEntry->row     = (int8_t)(row);
        modEntry->currRow = (int8_t)(row);
    }

    if (order != -1)
    {
        if (order >= 0)
        {
            modOrder = order;
            modEntry->currOrder = order;
            editor.ui.updateSongPos = true;

            if ((editor.currMode == MODE_PLAY) && (editor.playMode == PLAY_MODE_NORMAL))
            {
                modPattern = modEntry->head.order[order];
                if (modPattern > (MAX_PATTERNS - 1))
                    modPattern =  MAX_PATTERNS - 1;

                modEntry->currPattern = modPattern;
                editor.ui.updateCurrPattText = true;
            }

            editor.ui.updateSongPattern = true;
            editor.currPatternDisp = &modEntry->head.order[modOrder];

            posEdPos = modEntry->currOrder;
            if (posEdPos > (modEntry->head.orderCount - 1))
                posEdPos =  modEntry->head.orderCount - 1;

            editor.currPosEdPattDisp = &modEntry->head.order[posEdPos];

            if (editor.ui.posEdScreenShown)
                editor.ui.updatePosEd = true;
        }
    }

    editor.ui.updatePatternData = true;

    if (editor.blockMarkFlag)
        editor.ui.updateStatusText = true;
}

void modSetTempo(uint16_t bpm)
{
    uint16_t ciaVal;
    float f_hz, f_smp;

    if (bpm > 0)
    {
        modBPM = bpm;

        if (!editor.isSMPRendering && !editor.isWAVRendering)
        {
            modEntry->currBPM = bpm;
            editor.ui.updateSongBPM = true;
        }

        ciaVal = 1773447 / bpm; // yes, truncate here
        f_hz   = (float)(CIA_PAL_CLK) / ciaVal;

        if (editor.isSMPRendering)
            f_smp = (editor.pat2SmpHQ ? 28836.0f : 22168.0f) / f_hz;
        else
            f_smp = editor.outputFreq_f / f_hz;

        mixerSetSamplesPerTick((int32_t)(f_smp + 0.5f));
    }
}

void modStop(void)
{
    uint8_t i;
    moduleChannel_t *ch;

    editor.songPlaying = false;
    turnOffVoices();

    for (i = 0; i < AMIGA_VOICES; ++i)
    {
        ch = &modEntry->channels[i];

        ch->n_wavecontrol = 0;
        ch->n_glissfunk   = 0;
        ch->n_finetune    = 0;
        ch->n_loopcount   = 0;
    }

    pBreakFlag       = false;
    pattDelTime      = 0;
    pattDelTime2     = 0;
    pBreakPosition   = 0;
    posJumpAssert    = false;
    modHasBeenPlayed = true;
}

void playPattern(int8_t startRow)
{
    modEntry->row      = startRow & 0x3F;
    modEntry->currRow  = modEntry->row;
    editor.modTick     = 0;
    editor.playMode    = PLAY_MODE_PATTERN;
    editor.currMode    = MODE_PLAY;
    editor.didQuantize = false;

    if (!editor.stepPlayEnabled)
        pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);

    editor.songPlaying = true;
    mixerClearSampleCounter();
}

void incPatt(void)
{
    if (++modPattern > (MAX_PATTERNS - 1))
          modPattern = 0;

    modEntry->currPattern = modPattern;

    editor.ui.updatePatternData  = true;
    editor.ui.updateCurrPattText = true;
}

void decPatt(void)
{
    if (--modPattern < 0)
          modPattern = MAX_PATTERNS - 1;

    modEntry->currPattern = modPattern;

    editor.ui.updatePatternData  = true;
    editor.ui.updateCurrPattText = true;
}

void modPlay(int16_t patt, int16_t order, int8_t row)
{
    uint8_t oldPlayMode, oldMode;

    if (row != -1)
    {
        if ((row >= 0) && (row <= 63))
        {
            modEntry->row     = row;
            modEntry->currRow = row;
        }
    }
    else
    {
        modEntry->row     = 0;
        modEntry->currRow = 0;
    }

    if (editor.playMode != PLAY_MODE_PATTERN)
    {
        if (modOrder >= modEntry->head.orderCount)
        {
            modOrder = 0;
            modEntry->currOrder = 0;
        }

        if ((order >= 0) && (order < modEntry->head.orderCount))
        {
            modOrder = order;
            modEntry->currOrder = order;
        }

        if (order >= modEntry->head.orderCount)
        {
            modOrder = 0;
            modEntry->currOrder = 0;
        }
    }

    if ((patt >= 0) && (patt <= (MAX_PATTERNS - 1)))
    {
        modPattern = patt;
        modEntry->currPattern = patt;
    }
    else
    {
        modPattern = modEntry->head.order[modOrder];
        modEntry->currPattern = modEntry->head.order[modOrder];
    }

    editor.currPatternDisp   = &modEntry->head.order[modOrder];
    editor.currPosEdPattDisp = &modEntry->head.order[modOrder];

    oldPlayMode = editor.playMode;
    oldMode     = editor.currMode;

    doStopIt();
    turnOffVoices();

    editor.playMode = oldPlayMode;
    editor.currMode = oldMode;

    if (editor.playMode == PLAY_MODE_NORMAL)
    {
        editor.ticks50Hz = 0;
        editor.playTime  = 0;
    }

    editor.modTick     = editor.modSpeed;
    modHasBeenPlayed   = false;
    editor.songPlaying = true;
    editor.didQuantize = false;

    if (!editor.isSMPRendering && !editor.isWAVRendering)
    {
        editor.ui.updateSongPos      = true;
        editor.ui.updateSongTime     = true;
        editor.ui.updatePatternData  = true;
        editor.ui.updateSongPattern  = true;
        editor.ui.updateCurrPattText = true;
    }

    mixerClearSampleCounter();
}

void clearSong(void)
{
    uint8_t i;
    moduleChannel_t *ch;

    if (modEntry != NULL)
    {
        memset(editor.ui.pattNames,        0, MAX_PATTERNS * 16);
        memset(modEntry->head.order,       0, sizeof (modEntry->head.order));
        memset(modEntry->head.moduleTitle, 0, sizeof (modEntry->head.moduleTitle));

        editor.muted[0] = false;
        editor.muted[1] = false;
        editor.muted[2] = false;
        editor.muted[3] = false;

        editor.f6Pos  = 0;
        editor.f7Pos  = 16;
        editor.f8Pos  = 32;
        editor.f9Pos  = 48;
        editor.f10Pos = 63;

        editor.playTime        = 0;
        editor.ticks50Hz       = 0;
        editor.metroFlag       = false;
        editor.currSample      = 0;
        editor.editMoveAdd     = 1;
        editor.blockMarkFlag   = false;
        editor.swapChannelFlag = false;

        modEntry->head.orderCount   = 1;
        modEntry->head.patternCount = 1;

        for (i = 0; i < MAX_PATTERNS; ++i)
            memset(modEntry->patterns[i], 0, (MOD_ROWS * AMIGA_VOICES) * sizeof (note_t));

        for (i = 0; i < AMIGA_VOICES; ++i)
        {
            ch = &modEntry->channels[i];

            ch->n_wavecontrol = 0;
            ch->n_glissfunk   = 0;
            ch->n_finetune    = 0;
            ch->n_loopcount   = 0;
        }

        modSetPos(0, 0); // this also refreshes pattern data

        modEntry->currOrder     = 0;
        modEntry->currPattern   = 0;
        editor.currPatternDisp   = &modEntry->head.order[0];
        editor.currPosEdPattDisp = &modEntry->head.order[0];

        modSetTempo(editor.initialTempo);
        modSetSpeed(editor.initialSpeed);

        setLEDFilter(false); // real PT doesn't do this there, but that's insane

        editor.ui.updateSongSize = true;
        updateWindowTitle(MOD_IS_MODIFIED);
    }
}

void clearSamples(void)
{
    uint8_t i;
    moduleSample_t *s;

    if (modEntry != NULL)
    {
        for (i = 0; i < MOD_SAMPLES; ++i)
        {
            s = &modEntry->samples[i];

            s->fineTune   = 0;
            s->length     = 0;
            s->loopLength = 2;
            s->loopStart  = 0;
            s->volume     = 0;

            memset(s->text, 0, sizeof (s->text));
        }

        memset(modEntry->sampleData, 0, MOD_SAMPLES * MAX_SAMPLE_LEN);

        editor.currSample           = 0;
        editor.keypadSampleOffset   = 0;
        editor.sampleZero           = false;
        editor.ui.editOpScreenShown = false;
        editor.ui.aboutScreenShown  = false;
        editor.blockMarkFlag        = false;

        editor.samplePos = 0;
        updateCurrSample();

        updateWindowTitle(MOD_IS_MODIFIED);
    }
}

void clearAll(void)
{
    if (modEntry != NULL)
    {
        clearSamples();
        clearSong();
    }
}

void modFree(void)
{
    uint8_t i;

    if (modEntry != NULL)
    {
        for (i = 0; i < MAX_PATTERNS; ++i)
        {
            if (modEntry->patterns[i] != NULL)
                free(modEntry->patterns[i]);
        }

        if (modEntry->sampleData != NULL)
            free(modEntry->sampleData);

        free(modEntry);
        modEntry = NULL;
    }
}

uint8_t getSongProgressInPercentage(void)
{
    return (uint8_t)((((float)(modEntry->rowsCounter) / modEntry->rowsInTotal) * 100.0f));
}

void restartSong(void) // for the beginning of MOD2WAV/PAT2SMP
{
    if (editor.songPlaying)
        modStop();

    editor.playMode = PLAY_MODE_NORMAL;
    editor.blockMarkFlag = false;
    forceMixerOff = true;

    modEntry->row = 0;
    modEntry->currRow = 0;
    modEntry->rowsCounter = 0;

    memset(editor.rowVisitTable, 0, MOD_ORDERS * MOD_ROWS); // for MOD2WAV

    if (editor.isSMPRendering)
    {
        modPlay(DONT_SET_PATTERN, DONT_SET_ORDER, DONT_SET_ROW);
    }
    else
    {
        modEntry->currSpeed = 6;
        modEntry->currBPM   = 125;
        modSetSpeed(6);
        modSetTempo(125);

        modPlay(DONT_SET_PATTERN, 0, 0);
    }
}

// this function is meant for the end of MOD2WAV/PAT2SMP
void resetSong(void) // only call this after storeTempVariables() has been called!
{
    uint8_t i;
    moduleChannel_t *ch;

    modStop();

    editor.songPlaying = false;
    editor.playMode    = PLAY_MODE_NORMAL;
    editor.currMode    = MODE_IDLE;

    turnOffVoices();

    memset((int8_t *)(editor.vuMeterVolumes),    0, sizeof (editor.vuMeterVolumes));
    memset((float  *)(editor.realVuMeterVolumes),0, sizeof (editor.realVuMeterVolumes));
    memset((int8_t *)(editor.spectrumVolumes),   0, sizeof (editor.spectrumVolumes));

    memset(modEntry->channels, 0, sizeof (modEntry->channels));
    for (i = 0; i < AMIGA_VOICES; ++i)
    {
        ch = &modEntry->channels[i];

        ch->n_chanindex = i;
        ch->n_start     = NULL;
        ch->n_wavestart = NULL;
        ch->n_loopstart = NULL;
    }

    modOrder   = oldOrder;
    modPattern = oldPattern;

    modEntry->row         = oldRow;
    modEntry->currRow     = oldRow;
    modEntry->currBPM     = oldBPM;
    modEntry->currOrder   = oldOrder;
    modEntry->currPattern = oldPattern;

    editor.currPosDisp         = &modEntry->currOrder;
    editor.currEditPatternDisp = &modEntry->currPattern;
    editor.currPatternDisp     = &modEntry->head.order[modEntry->currOrder];
    editor.currPosEdPattDisp   = &modEntry->head.order[modEntry->currOrder];

    modSetSpeed(oldSpeed);
    modSetTempo(oldBPM);

    doStopIt();

    editor.modTick   = 0;
    modHasBeenPlayed = false;
    forceMixerOff    = false;
}

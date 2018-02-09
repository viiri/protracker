#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h> // round()/roundf()
#include <ctype.h> // tolower()/toupper()
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "pt_header.h"
#include "pt_textout.h"
#include "pt_palette.h"
#include "pt_sampler.h"
#include "pt_audio.h"
#include "pt_sampleloader.h"
#include "pt_visuals.h"
#include "pt_helpers.h"
#include "pt_terminal.h"
#include "pt_unicode.h"

enum
{
    WAV_FORMAT_PCM        = 0x0001,
    WAV_FORMAT_IEEE_FLOAT = 0x0003
};

static int8_t loadWAVSample(UNICHAR *fileName, char *entryName, int8_t forceDownSampling);
static int8_t loadIFFSample(UNICHAR *fileName, char *entryName);
static int8_t loadRAWSample(UNICHAR *fileName, char *entryName);

void extLoadWAVSampleCallback(int8_t downsample)
{
    loadWAVSample(editor.fileNameTmp, editor.entryNameTmp, downsample);
}

int8_t loadWAVSample(UNICHAR *fileName, char *entryName, int8_t forceDownSampling)
{
    /*
    ** - Supports 8-bit, 16-bit, 24-bit, 32-bit, 32-bit float
    ** - Supports additional "INAM", "smpl" and "xtra" chunks
    ** - Stereo is downmixed to mono
    ** - >8-bit is quantized to 8-bit
    ** - pre-"2x downsampling" (if wanted by the user)
    */

    uint8_t *audioDataU8, wavSampleNameFound;
    int16_t *audioDataS16, tempVol, smp16;
    uint16_t audioFormat, numChannels, bitsPerSample;
    int32_t *audioDataS32, smp32, smp32_l, smp32_r;
    uint32_t *audioDataU32, i, nameLen, chunkID, chunkSize;
    uint32_t sampleLength, sampleRate, filesize, loopFlags;
    uint32_t loopStart, loopEnd, dataPtr, dataLen, fmtPtr, endOfChunk, bytesRead;
    uint32_t fmtLen, inamPtr, inamLen, smplPtr, smplLen, xtraPtr, xtraLen;
    float *audioDataFloat, smp_f;
    FILE *f;
    moduleSample_t *s;

    // zero out chunk pointers and lengths
    fmtPtr  = 0; fmtLen  = 0;
    dataPtr = 0; dataLen = 0;
    inamPtr = 0; inamLen = 0;
    xtraPtr = 0; xtraLen = 0;
    smplPtr = 0; smplLen = 0;

    wavSampleNameFound = false;

    s = &modEntry->samples[editor.currSample];

    if (forceDownSampling == -1)
    {
        // these two *must* be fully wiped, for outputting reasons
        memset(editor.fileNameTmp,  0, PATH_MAX_LEN);
        memset(editor.entryNameTmp, 0, PATH_MAX_LEN);

        UNICHAR_STRCPY(editor.fileNameTmp, fileName);
        strcpy(editor.entryNameTmp, entryName);
    }

    f = UNICHAR_FOPEN(fileName, "rb");
    if (f == NULL)
    {
        displayErrorMsg("FILE I/O ERROR !");
        terminalPrintf("WAV sample loading failed: file input/output error\n");

        return (false);
    }

    fseek(f, 0, SEEK_END);
    filesize = ftell(f);
    if (filesize == 0)
    {
        displayErrorMsg("NOT A WAV !");
        terminalPrintf("WAV sample loading failed: not a valid .WAV\n");

        fclose(f);

        return (false);
    }

    // look for wanted chunks and set up pointers + lengths
    fseek(f, 12, SEEK_SET);

    bytesRead = 0;
    while (!feof(f) && (bytesRead < (filesize - 12)))
    {
        fread(&chunkID,   4, 1, f); if (feof(f)) break;
        fread(&chunkSize, 4, 1, f); if (feof(f)) break;

        if (bigEndian) chunkID   = SWAP32(chunkID);
        if (bigEndian) chunkSize = SWAP32(chunkSize);

        endOfChunk = (ftell(f) + chunkSize) + (chunkSize & 1);
        switch (chunkID)
        {
            case 0x20746D66: // "fmt "
            {
                fmtPtr = ftell(f);
                fmtLen = chunkSize;
            }
            break;

            case 0x61746164: // "data"
            {
                dataPtr = ftell(f);
                dataLen = chunkSize;
            }
            break;

            case 0x5453494C: // "LIST"
            {
                if (chunkSize >= 4)
                {
                    fread(&chunkID, 4, 1, f); if (bigEndian) chunkID = SWAP32(chunkID);
                    if (chunkID == 0x4F464E49) // "INFO"
                    {
                        bytesRead = 0;
                        while (!feof(f) && (bytesRead < chunkSize))
                        {
                            fread(&chunkID,   4, 1, f); if (bigEndian) chunkID   = SWAP32(chunkID);
                            fread(&chunkSize, 4, 1, f); if (bigEndian) chunkSize = SWAP32(chunkSize);

                            switch (chunkID)
                            {
                                case 0x4D414E49: // "INAM"
                                {
                                    inamPtr = ftell(f);
                                    inamLen = chunkSize;
                                }
                                break;

                                default: break;
                            }

                            bytesRead += (chunkSize + (chunkSize & 1));
                        }
                    }
                }
            }
            break;

            case 0x61727478: // "xtra"
            {
                xtraPtr = ftell(f);
                xtraLen = chunkSize;
            }
            break;

            case 0x6C706D73: // "smpl"
            {
                smplPtr = ftell(f);
                smplLen = chunkSize;
            }
            break;

            default: break;
        }

        bytesRead += (chunkSize + (chunkSize & 1));
        fseek(f, endOfChunk, SEEK_SET);
    }

    // we need at least "fmt " and "data" - check if we found them sanely
    if (((fmtPtr == 0) || (fmtLen < 16)) || ((dataPtr == 0) || (dataLen == 0)))
    {
        displayErrorMsg("NOT A WAV !");
        terminalPrintf("WAV sample loading failed: not a valid .WAV\n");

        fclose(f);

        return (false);
    }

    // ---- READ "fmt " CHUNK ----
    fseek(f, fmtPtr, SEEK_SET);
    fread(&audioFormat, 2, 1, f); if (bigEndian) audioFormat = SWAP16(audioFormat);
    fread(&numChannels, 2, 1, f); if (bigEndian) numChannels = SWAP16(numChannels);
    fread(&sampleRate,  4, 1, f); if (bigEndian) sampleRate  = SWAP32(sampleRate);
    fseek(f, 6, SEEK_CUR); // unneeded
    fread(&bitsPerSample, 2, 1, f); if (bigEndian) bitsPerSample = SWAP16(bitsPerSample);
    sampleLength = dataLen;
    // ---------------------------

    if ((sampleRate == 0) || (sampleLength == 0) || (sampleLength >= (filesize * (bitsPerSample / 8))))
    {
        displayErrorMsg("WAV CORRUPT !");
        terminalPrintf("WAV sample loading failed: corrupt WAV file\n");
        fclose(f);

        return (false);
    }

    if ((audioFormat != WAV_FORMAT_PCM) && (audioFormat != WAV_FORMAT_IEEE_FLOAT))
    {
        displayErrorMsg("WAV UNSUPPORTED !");
        terminalPrintf("WAV sample loading failed: unsupported type (not PCM integer or PCM float)\n");
        fclose(f);

        return (false);
    }

    if ((numChannels == 0) || (numChannels > 2))
    {
        displayErrorMsg("WAV UNSUPPORTED !");
        terminalPrintf("WAV sample loading failed: unsupported type (doesn't have 1 or 2 channels)\n");

        fclose(f);

        return (false);
    }

    if ((audioFormat == WAV_FORMAT_IEEE_FLOAT) && (bitsPerSample != 32))
    {
        displayErrorMsg("WAV UNSUPPORTED !");
        terminalPrintf("WAV sample loading failed: unsupported type (not 8-bit, 16-bit, 24-bit, 32-bit or 32-bit float)\n");

        fclose(f);

        return (false);
    }

    if ((bitsPerSample != 8) && (bitsPerSample != 16) && (bitsPerSample != 24) && (bitsPerSample != 32))
    {
        displayErrorMsg("WAV UNSUPPORTED !");
        terminalPrintf("WAV sample loading failed: unsupported type (not 8-bit, 16-bit, 24-bit, 32-bit or 32-bit float)\n");

        fclose(f);

        return (false);
    }

    if (sampleRate > 22050)
    {
        if (forceDownSampling == -1)
        {
            editor.ui.askScreenShown = true;
            editor.ui.askScreenType  = ASK_DOWNSAMPLING;

            pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
            setStatusMessage("2X DOWNSAMPLING ?", NO_CARRY);
            renderAskDialog();

            fclose(f);

            return (true);
        }
    }
    else
    {
        forceDownSampling = false;
    }

    // ---- READ SAMPLE DATA ----
    fseek(f, dataPtr, SEEK_SET);

    if (bitsPerSample == 8) // 8-BIT INTEGER SAMPLE
    {
        if (sampleLength > (MAX_SAMPLE_LEN * 4))
            sampleLength =  MAX_SAMPLE_LEN * 4;

        audioDataU8 = (uint8_t *)(malloc(sampleLength * sizeof (uint8_t)));
        if (audioDataU8 == NULL)
        {
            fclose(f);
            displayErrorMsg(editor.outOfMemoryText);
            terminalPrintf("WAV sample loading failed: out of memory!\n");

            return (false);
        }

        // read sample data
        if (fread(audioDataU8, 1, sampleLength, f) != sampleLength)
        {
            fclose(f);
            free(audioDataU8);
            displayErrorMsg("I/O ERROR !");
            terminalPrintf("WAV sample loading failed: I/O error!\n");

            return (false);
        }

        // convert from stereo to mono (if needed)
        if (numChannels == 2)
        {
            sampleLength /= 2;

            // add right channel to left channel
            for (i = 0; i < (sampleLength - 1); i++)
            {
                smp16 = (audioDataU8[(i * 2) + 0] - 128) + (audioDataU8[(i * 2) + 1] - 128);
                smp16 = 128 + SAR16(smp16, 1);

                audioDataU8[i] = (uint8_t)(smp16);
            }
        }

        // 2x downsampling - remove every other sample (if needed)
        if (forceDownSampling)
        {
            sampleLength /= 2;
            for (i = 1; i < sampleLength; i++)
                audioDataU8[i] = audioDataU8[i * 2];
        }

        if (sampleLength > MAX_SAMPLE_LEN)
            sampleLength = MAX_SAMPLE_LEN;

        turnOffVoices();
        for (i = 0; i < MAX_SAMPLE_LEN; ++i)
        {
            if (i <= (sampleLength & 0xFFFFFFFE))
                modEntry->sampleData[(editor.currSample * MAX_SAMPLE_LEN) + i] = audioDataU8[i] - 128;
            else
                modEntry->sampleData[(editor.currSample * MAX_SAMPLE_LEN) + i] = 0;
        }

        free(audioDataU8);
    }
    else if (bitsPerSample == 16) // 16-BIT INTEGER SAMPLE
    {
        sampleLength /= 2;
        if (sampleLength > (MAX_SAMPLE_LEN * 4))
            sampleLength =  MAX_SAMPLE_LEN * 4;

        audioDataS16 = (int16_t *)(malloc(sampleLength * sizeof (int16_t)));
        if (audioDataS16 == NULL)
        {
            fclose(f);
            displayErrorMsg(editor.outOfMemoryText);
            terminalPrintf("WAV sample loading failed: out of memory!\n");

            return (false);
        }

        // read sample data
        if (fread(audioDataS16, 2, sampleLength, f) != sampleLength)
        {
            fclose(f);
            free(audioDataS16);
            displayErrorMsg("I/O ERROR !");
            terminalPrintf("WAV sample loading failed: I/O error!\n");

            return (false);
        }

        // convert endianness (if needed)
        if (bigEndian)
        {
            for (i = 0; i < sampleLength; ++i)
                audioDataS16[i] = SWAP16(audioDataS16[i]);
        }

        // convert from stereo to mono (if needed)
        if (numChannels == 2)
        {
            sampleLength /= 2;

            // add right channel to left channel
            for (i = 0; i < (sampleLength - 1); i++)
            {
                smp32 = audioDataS16[(i * 2) + 0] + audioDataS16[(i * 2) + 1];
                smp32 = SAR32_1(smp32);

                audioDataS16[i] = (int16_t)(smp32);
            }
        }

        // 2x downsampling - remove every other sample (if needed)
        if (forceDownSampling)
        {
            sampleLength /= 2;
            for (i = 1; i < sampleLength; i++)
                audioDataS16[i] = audioDataS16[i * 2];
        }

        if (sampleLength > MAX_SAMPLE_LEN)
            sampleLength = MAX_SAMPLE_LEN;

        normalize16bitSigned(audioDataS16, sampleLength);

        turnOffVoices();
        for (i = 0; i < MAX_SAMPLE_LEN; ++i)
        {
            if (i <= (sampleLength & 0xFFFFFFFE))
                modEntry->sampleData[(editor.currSample * MAX_SAMPLE_LEN) + i] = quantize16bitTo8bit(audioDataS16[i]);
            else
                modEntry->sampleData[(editor.currSample * MAX_SAMPLE_LEN) + i] = 0;
        }

        free(audioDataS16);
    }
    else if (bitsPerSample == 24) // 24-BIT INTEGER SAMPLE
    {
        sampleLength /= (4 - 1);
        if (sampleLength > (MAX_SAMPLE_LEN * 4))
            sampleLength =  MAX_SAMPLE_LEN * 4;

        audioDataS32 = (int32_t *)(malloc(sampleLength * sizeof (int32_t)));
        if (audioDataS32 == NULL)
        {
            fclose(f);
            displayErrorMsg(editor.outOfMemoryText);
            terminalPrintf("WAV sample loading failed: out of memory!\n");

            return (false);
        }

        // read sample data
        audioDataU8 = (uint8_t *)(audioDataS32);
        for (i = 0; i < sampleLength; i++)
        {
            audioDataU8[0] = 0;
            fread(&audioDataU8[1], 3, 1, f);
            audioDataU8 += sizeof (int32_t);
        }

        // convert endianness (if needed)
        if (bigEndian)
        {
            for (i = 0; i < sampleLength; ++i)
                audioDataS32[i] = SWAP32(audioDataS32[i]);
        }

        // convert from stereo to mono (if needed)
        if (numChannels == 2)
        {
            sampleLength /= 2;

            // add right channel to left channel
            for (i = 0; i < (sampleLength - 1); i++)
            {
                smp32_l = audioDataS32[(i * 2) + 0];
                smp32_r = audioDataS32[(i * 2) + 1];

                smp32_l = SAR32_1(smp32_l);
                smp32_r = SAR32_1(smp32_r);

                audioDataS32[i] = smp32_l + smp32_r;
            }
        }

        // 2x downsampling - remove every other sample (if needed)
        if (forceDownSampling)
        {
            sampleLength /= 2;
            for (i = 1; i < sampleLength; i++)
                audioDataS32[i] = audioDataS32[i * 2];
        }

        if (sampleLength > MAX_SAMPLE_LEN)
            sampleLength = MAX_SAMPLE_LEN;

        normalize24bitSigned(audioDataS32, sampleLength);

        turnOffVoices();
        for (i = 0; i < MAX_SAMPLE_LEN; ++i)
        {
            if (i <= (sampleLength & 0xFFFFFFFE))
                modEntry->sampleData[(editor.currSample * MAX_SAMPLE_LEN) + i] = quantize24bitTo8bit(audioDataS32[i]);
            else
                modEntry->sampleData[(editor.currSample * MAX_SAMPLE_LEN) + i] = 0;
        }

        free(audioDataS32);
    }
    else if ((audioFormat == WAV_FORMAT_PCM) && (bitsPerSample == 32)) // 32-BIT INTEGER SAMPLE
    {
        sampleLength /= 4;
        if (sampleLength > (MAX_SAMPLE_LEN * 4))
            sampleLength =  MAX_SAMPLE_LEN * 4;

        audioDataS32 = (int32_t *)(malloc(sampleLength * sizeof (int32_t)));
        if (audioDataS32 == NULL)
        {
            fclose(f);
            displayErrorMsg(editor.outOfMemoryText);
            terminalPrintf("WAV sample loading failed: out of memory!\n");

            return (false);
        }

        // read sample data
        if (fread(audioDataS32, 4, sampleLength, f) != sampleLength)
        {
            fclose(f);
            free(audioDataS32);
            displayErrorMsg("I/O ERROR !");
            terminalPrintf("WAV sample loading failed: I/O error!\n");

            return (false);
        }

        // convert endianness (if needed)
        if (bigEndian)
        {
            for (i = 0; i < sampleLength; ++i)
                audioDataS32[i] = SWAP32(audioDataS32[i]);
        }

        // convert from stereo to mono (if needed)
        if (numChannels == 2)
        {
            sampleLength /= 2;

            // add right channel to left channel
            for (i = 0; i < (sampleLength - 1); i++)
            {
                smp32_l = audioDataS32[(i * 2) + 0];
                smp32_r = audioDataS32[(i * 2) + 1];

                smp32_l = SAR32_1(smp32_l);
                smp32_r = SAR32_1(smp32_r);

                audioDataS32[i] = smp32_l + smp32_r;
            }
        }

        // 2x downsampling - remove every other sample (if needed)
        if (forceDownSampling)
        {
            sampleLength /= 2;
            for (i = 1; i < sampleLength; i++)
                audioDataS32[i] = audioDataS32[i * 2];
        }

        if (sampleLength > MAX_SAMPLE_LEN)
            sampleLength = MAX_SAMPLE_LEN;

        normalize32bitSigned(audioDataS32, sampleLength);

        turnOffVoices();
        for (i = 0; i < MAX_SAMPLE_LEN; ++i)
        {
            if (i <= (sampleLength & 0xFFFFFFFE))
                modEntry->sampleData[(editor.currSample * MAX_SAMPLE_LEN) + i] = quantize32bitTo8bit(audioDataS32[i]);
            else
                modEntry->sampleData[(editor.currSample * MAX_SAMPLE_LEN) + i] = 0;
        }

        free(audioDataS32);
    }
    else if ((audioFormat == WAV_FORMAT_IEEE_FLOAT) && (bitsPerSample == 32)) // 32-BIT FLOAT SAMPLE
    {
        sampleLength /= 4;
        if (sampleLength > (MAX_SAMPLE_LEN * 4))
            sampleLength =  MAX_SAMPLE_LEN * 4;

        audioDataU32 = (uint32_t *)(malloc(sampleLength * sizeof (uint32_t)));
        if (audioDataU32 == NULL)
        {
            fclose(f);
            displayErrorMsg(editor.outOfMemoryText);
            terminalPrintf("WAV sample loading failed: out of memory!\n");

            return (false);
        }

        // read sample data
        if (fread(audioDataU32, 4, sampleLength, f) != sampleLength)
        {
            fclose(f);
            free(audioDataU32);
            displayErrorMsg("I/O ERROR !");
            terminalPrintf("WAV sample loading failed: I/O error!\n");

            return (false);
        }

        // convert endianness (if needed)
        if (bigEndian)
        {
            for (i = 0; i < sampleLength; ++i)
                audioDataU32[i] = SWAP32(audioDataU32[i]);
        }

        audioDataFloat = (float *)(audioDataU32);

        // convert from stereo to mono (if needed)
        if (numChannels == 2)
        {
            sampleLength /= 2;

            // add right channel to left channel (this is uncorrect, but Good Enough)
            for (i = 0; i < (sampleLength - 1); i++)
            {
                smp_f = (audioDataFloat[(i * 2) + 0] / 2.0f) + (audioDataFloat[(i * 2) + 1] / 2.0f);
                audioDataFloat[i] = CLAMP(smp_f, -1.0f, 1.0f);
            }
        }

        // 2x downsampling - remove every other sample (if needed)
        if (forceDownSampling)
        {
            sampleLength /= 2;
            for (i = 1; i < sampleLength; i++)
                audioDataFloat[i] = audioDataFloat[i * 2];
        }

        if (sampleLength > MAX_SAMPLE_LEN)
            sampleLength = MAX_SAMPLE_LEN;

        normalize8bitFloatSigned(audioDataFloat, sampleLength);

        turnOffVoices();
        for (i = 0; i < MAX_SAMPLE_LEN; ++i)
        {
            if (i <= (sampleLength & 0xFFFFFFFE))
                modEntry->sampleData[(editor.currSample * MAX_SAMPLE_LEN) + i] = quantizeFloatTo8bit(audioDataFloat[i]);
            else
                modEntry->sampleData[(editor.currSample * MAX_SAMPLE_LEN) + i] = 0;
        }

        free(audioDataU32);
    }

    // set sample length
    if (sampleLength & 1)
    {
        if (++sampleLength > MAX_SAMPLE_LEN)
              sampleLength = MAX_SAMPLE_LEN;
    }

    s->length     = sampleLength;
    s->fineTune   = 0;
    s->volume     = 64;
    s->loopStart  = 0;
    s->loopLength = 2;

    // ---- READ "smpl" chunk ----
    if ((smplPtr != 0) && (smplLen > 52))
    {
        fseek(f, smplPtr + 28, SEEK_SET); // seek to first wanted byte

        fread(&loopFlags, 4, 1, f); if (bigEndian) loopFlags = SWAP32(loopFlags);
        fseek(f, 12, SEEK_CUR);
        fread(&loopStart, 4, 1, f); if (bigEndian) loopStart = SWAP32(loopStart);
        fread(&loopEnd,   4, 1, f); if (bigEndian) loopEnd   = SWAP32(loopEnd);
        loopEnd++;

        if (forceDownSampling)
        {
            // we already downsampled 2x, so we're half the original length
            loopStart /= 2;
            loopEnd   /= 2;
        }

        loopStart &= 0xFFFFFFFE;
        loopEnd   &= 0xFFFFFFFE;

        if (loopFlags)
        {
            if (loopEnd <= sampleLength)
            {
                s->loopStart  = loopStart;
                s->loopLength = loopEnd - loopStart;
            }
        }
    }
    // ---------------------------

    // ---- READ "xtra" chunk ----
    if ((xtraPtr != 0) && (xtraLen >= 8))
    {
        fseek(f, xtraPtr + 4, SEEK_SET); // seek to first wanted byte

        // volume (0..256)
        fseek(f, 2, SEEK_CUR);
        fread(&tempVol, 2, 1, f); if (bigEndian) tempVol = SWAP16(tempVol);
        s->volume = (int8_t)((CLAMP(tempVol, 0, 256) * (64.0f / 256.0f)) + 0.5f);
    }
    // ---------------------------

    // ---- READ "INAM" chunk ----
    if ((inamPtr != 0) && (inamLen > 0))
    {
        fseek(f, inamPtr, SEEK_SET); // seek to first wanted byte

        for (i = 0; i < 21; ++i)
        {
            if (i < inamLen)
                s->text[i] = (char)(toupper(fgetc(f)));
            else
                s->text[i] = '\0';
        }

        s->text[21] = '\0';
        s->text[22] = '\0';

        wavSampleNameFound = true;
    }
    // ---------------------------

    fclose(f);

    // copy over sample name
    if (!wavSampleNameFound)
    {
        nameLen = strlen(entryName);
        for (i = 0; i < 21; ++i)
           s->text[i] = (i < nameLen) ? toupper(entryName[i]) : '\0';

        s->text[21] = '\0';
        s->text[22] = '\0';
    }

    // remove .wav from end of sample name (if present)
    nameLen = strlen(s->text);
    if ((nameLen >= 4) && !strncmp(&s->text[nameLen - 4], ".WAV", 4))
          memset(&s->text[nameLen - 4], '\0',   4);

    editor.sampleZero = false;
    editor.samplePos  = 0;

    fixSampleBeep(s);
    updateCurrSample();
    fillSampleRedoBuffer(editor.currSample);

    terminalPrintf("WAV sample \"%s\" loaded to slot %02x\n", s->text, editor.currSample + 1);

    updateWindowTitle(MOD_IS_MODIFIED);
    return (true);
}

int8_t loadIFFSample(UNICHAR *fileName, char *entryName)
{
    char tmpCharBuf[23];
    int8_t *sampleData;
    uint8_t nameFound, is16Bit;
    int16_t sample16, *ptr16;
    int32_t filesize;
    uint32_t i, sampleLength, sampleLoopStart, sampleLoopLength;
    uint32_t sampleVolume, blockName, blockSize;
    uint32_t vhdrPtr, vhdrLen, bodyPtr, bodyLen, namePtr, nameLen;
    FILE *f;
    moduleSample_t *s;

    s = &modEntry->samples[editor.currSample];

    vhdrPtr = 0; vhdrLen = 0;
    bodyPtr = 0; bodyLen = 0;
    namePtr = 0; nameLen = 0;

    f = UNICHAR_FOPEN(fileName, "rb");
    if (f == NULL)
    {
        displayErrorMsg("FILE I/O ERROR !");
        terminalPrintf("IFF sample loading failed: file input/output error\n");

        return (false);
    }

    fseek(f, 0, SEEK_END);
    filesize = ftell(f);
    if (filesize == 0)
    {
        displayErrorMsg("IFF IS CORRUPT !");
        terminalPrintf("IFF sample loading failed: not a valid .IFF\n");

        return (false);
    }

    fseek(f, 8, SEEK_SET);
    fread(tmpCharBuf, 1, 4, f);
    is16Bit = !strncmp(tmpCharBuf, "16SV", 4);

    sampleLength   = 0;
    nameFound      = false;
    sampleVolume   = 65536; // max volume

    fseek(f, 12, SEEK_SET);
    while (!feof(f) && (ftell(f) < (filesize - 12)))
    {
        fread(&blockName, 4, 1, f); if (feof(f)) break;
        fread(&blockSize, 4, 1, f); if (feof(f)) break;

        if (!bigEndian) blockName = SWAP32(blockName);
        if (!bigEndian) blockSize = SWAP32(blockSize);

        switch (blockName)
        {
            case 0x56484452: // VHDR
            {
                vhdrPtr = ftell(f);
                vhdrLen = blockSize;
            }
            break;

            case 0x4E414D45: // NAME
            {
                namePtr = ftell(f);
                nameLen = blockSize;
            }
            break;

            case 0x424F4459: // BODY
            {
                bodyPtr = ftell(f);
                bodyLen = blockSize;
            }
            break;

            default: break;
        }

        fseek(f, blockSize + (blockSize & 1), SEEK_CUR);
    }

    if ((vhdrPtr == 0) || (vhdrLen < 20) || (bodyPtr == 0) || (bodyLen == 0))
    {
        fclose(f);

        displayErrorMsg("NOT A VALID IFF !");
        terminalPrintf("IFF sample loading failed: not a valid .IFF\n");

        return (false);
    }

    fseek(f, vhdrPtr, SEEK_SET);
    fread(&sampleLoopStart,  4, 1, f); if (!bigEndian) sampleLoopStart  = SWAP32(sampleLoopStart);
    fread(&sampleLoopLength, 4, 1, f); if (!bigEndian) sampleLoopLength = SWAP32(sampleLoopLength);

    fseek(f, 4 + 2 + 1, SEEK_CUR);

    if (fgetc(f) != 0) // sample type
    {
        fclose(f);

        displayErrorMsg("UNSUPPORTED IFF !");
        terminalPrintf("IFF sample loading failed: unsupported .IFF\n");

        return (false);
    }

    fread(&sampleVolume, 4, 1, f); if (!bigEndian) sampleVolume = SWAP32(sampleVolume);
    if (sampleVolume > 65536)
        sampleVolume = 65536;
    sampleVolume = (uint32_t)((sampleVolume / 1024.0f) + 0.5f);

    sampleLength = bodyLen;
    if (is16Bit)
    {
        if (sampleLength > (2 * MAX_SAMPLE_LEN))
            sampleLength =  2 * MAX_SAMPLE_LEN;
    }
    else
    {
         if (sampleLength > MAX_SAMPLE_LEN)
            sampleLength  = MAX_SAMPLE_LEN;
    }

    if (sampleLength == 0)
    {
        fclose(f);

        displayErrorMsg("NOT A VALID IFF !");
        terminalPrintf("IFF sample loading failed: not a valid .IFF\n");

        return (false);
    }

    sampleData = (int8_t *)(malloc(sampleLength));
    if (sampleData == NULL)
    {
        fclose(f);

        displayErrorMsg(editor.outOfMemoryText);
        terminalPrintf("IFF sample loading failed: out of memory!\n");

        return (false);
    }

    if (is16Bit)
    {
        sampleLength     /= 2;
        sampleLoopStart  /= 2;
        sampleLoopLength /= 2;
    }

    sampleLength     &= 0xFFFFFFFE;
    sampleLoopStart  &= 0xFFFFFFFE;
    sampleLoopLength &= 0xFFFFFFFE;

    if (sampleLength > MAX_SAMPLE_LEN)
        sampleLength = MAX_SAMPLE_LEN;

    if (sampleLoopLength < 2)
    {
        sampleLoopStart  = 0;
        sampleLoopLength = 2;
    }

    if ((sampleLoopStart >= MAX_SAMPLE_LEN) || (sampleLoopLength > MAX_SAMPLE_LEN))
    {
        sampleLoopStart  = 0;
        sampleLoopLength = 2;
    }

    if ((sampleLoopStart + sampleLoopLength) > sampleLength)
    {
        sampleLoopStart  = 0;
        sampleLoopLength = 2;
    }

    if (sampleLoopStart > (sampleLength - 2))
    {
        sampleLoopStart  = 0;
        sampleLoopLength = 2;
    }

    turnOffVoices();
    memset(modEntry->sampleData + s->offset, 0, MAX_SAMPLE_LEN);

    fseek(f, bodyPtr, SEEK_SET);
    if (is16Bit) // FT2 specific 16SV format (little-endian samples)
    {
        fread(sampleData, 1, 2 * sampleLength, f);

        ptr16 = (int16_t *)(sampleData);
        for (i = 0; i < sampleLength; ++i)
        {
            sample16 = ptr16[i]; if (bigEndian) sample16 = SWAP16(sample16);
            modEntry->sampleData[s->offset + i] = quantize16bitTo8bit(sample16);
        }
    }
    else
    {
        fread(sampleData, 1, sampleLength, f);
        memcpy(modEntry->sampleData + s->offset, sampleData, sampleLength);
    }

    free(sampleData);

    // set sample attributes
    s->volume     = sampleVolume;
    s->fineTune   = 0;
    s->length     = sampleLength;
    s->loopStart  = sampleLoopStart;
    s->loopLength = sampleLoopLength;

    // read name
    if ((namePtr != 0) && (nameLen > 0))
    {
        fseek(f, namePtr, SEEK_SET);
        memset(tmpCharBuf, 0, sizeof (tmpCharBuf));

        if (nameLen > 21)
        {
            fread(tmpCharBuf, 1, 21, f);
            fseek(f, nameLen - 21, SEEK_CUR);
        }
        else
        {
            fread(tmpCharBuf, 1, nameLen, f);
        }

        nameFound = true;
    }

    fclose(f);

    // copy over sample name
    memset(s->text, '\0', sizeof (s->text));

    if (nameFound)
    {
        nameLen = strlen(tmpCharBuf);
        if (nameLen > 21)
            nameLen = 21;

        for (i = 0; i < nameLen; ++i)
            s->text[i] = toupper(tmpCharBuf[i]);
    }
    else
    {
        nameLen = strlen(entryName);
        if (nameLen > 21)
            nameLen = 21;

        for (i = 0; i < nameLen; ++i)
            s->text[i] = toupper(entryName[i]);
    }

    // remove .iff from end of sample name (if present)
    nameLen = strlen(s->text);
    if ((nameLen >= 4) && !strncmp(&s->text[nameLen - 4], ".IFF", 4))
          memset(&s->text[nameLen - 4], '\0', 4);

    editor.sampleZero = false;
    editor.samplePos  = 0;

    fixSampleBeep(s);
    updateCurrSample();
    fillSampleRedoBuffer(editor.currSample);

    terminalPrintf("IFF sample \"%s\" loaded into sample slot %02x\n", s->text, editor.currSample + 1);

    updateWindowTitle(MOD_IS_MODIFIED);
    return (false);
}

int8_t loadRAWSample(UNICHAR *fileName, char *entryName)
{
    uint8_t i;
    uint32_t nameLen, fileSize;
    FILE *f;
    moduleSample_t *s;

    s = &modEntry->samples[editor.currSample];

    f = UNICHAR_FOPEN(fileName, "rb");
    if (f == NULL)
    {
        displayErrorMsg("FILE I/O ERROR !");
        terminalPrintf("RAW sample loading failed: file input/output error\n");

        return (false);
    }

    fseek(f, 0, SEEK_END);
    fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    fileSize &= 0xFFFFFFFE;
    if (fileSize > MAX_SAMPLE_LEN)
        fileSize = MAX_SAMPLE_LEN;

    turnOffVoices();

    memset(modEntry->sampleData + s->offset, 0, MAX_SAMPLE_LEN);
    fread(modEntry->sampleData + s->offset, 1, fileSize, f);
    fclose(f);

    // set sample attributes
    s->volume     = 64;
    s->fineTune   = 0;
    s->length     = fileSize;
    s->loopStart  = 0;
    s->loopLength = 2;

    // copy over sample name
    nameLen = strlen(entryName);
    for (i = 0; i < 21; ++i)
        s->text[i] = (i < nameLen) ? toupper(entryName[i]) : '\0';

    s->text[21] = '\0';
    s->text[22] = '\0';

    editor.sampleZero = false;
    editor.samplePos  = 0;

    fixSampleBeep(s);
    updateCurrSample();
    fillSampleRedoBuffer(editor.currSample);

    terminalPrintf("RAW sample \"%s\" loaded into sample slot %02x\n", s->text, editor.currSample + 1);

    updateWindowTitle(MOD_IS_MODIFIED);
    return (true);
}

int8_t loadSample(UNICHAR *fileName, char *entryName)
{
    uint32_t fileSize, ID;
    FILE *f;

    f = UNICHAR_FOPEN(fileName, "rb");
    if (f == NULL)
    {
        displayErrorMsg("FILE I/O ERROR !");
        terminalPrintf("Sample loading failed: file input/output error\n");

        return (false);
    }

    fseek(f, 0, SEEK_END);
    fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    // first, check heades before we eventually load as RAW
    if (fileSize > 16)
    {
        fread(&ID, 4, 1, f); if (bigEndian) ID = SWAP32(ID);

        // check if it's actually a WAV sample
        if (ID == 0x46464952) // "RIFF"
        {
            fseek(f, 4, SEEK_CUR);
            fread(&ID, 4, 1, f); if (bigEndian) ID = SWAP32(ID);

            if (ID == 0x45564157) // "WAVE"
            {
                fread(&ID, 4, 1, f); if (bigEndian) ID = SWAP32(ID);

                if (ID == 0x20746D66) // "fmt "
                {
                    fclose(f);
                    return (loadWAVSample(fileName, entryName, -1));
                }
            }
        }

        // check if it's an Amiga IFF sample
        else if (ID == 0x4D524F46) // "FORM"
        {
            fseek(f, 4, SEEK_CUR);
            fread(&ID, 4, 1, f); if (bigEndian) ID = SWAP32(ID);

            if ((ID == 0x58565338) || (ID == 0x56533631)) // "8SVX" (normal) and "16SV" (FT2 sample)
            {
                fclose(f);
                return (loadIFFSample(fileName, entryName));
            }
        }
    }

    // nope, continue loading as RAW
    fclose(f);

    return (loadRAWSample(fileName, entryName));
}

int8_t saveSample(int8_t checkIfFileExist, int8_t giveNewFreeFilename)
{
    char fileName[48];
    uint8_t smp;
    uint16_t j;
    int32_t i, sampleTextLen;
    uint32_t sampleSize, iffSize, iffSampleSize;
    uint32_t loopStart, loopLen, tmp32;
    FILE *f;
    struct stat statBuffer;
    moduleSample_t *s;
    wavHeader_t wavHeader;
    samplerChunk_t samplerChunk;

    if (modEntry->samples[editor.currSample].length == 0)
    {
        displayErrorMsg("SAMPLE IS EMPTY");
        terminalPrintf("Sample saving failed: sample data is empty\n");

        return (false);
    }

    memset(fileName, 0, sizeof (fileName));

    if (*modEntry->samples[editor.currSample].text == '\0')
    {
        strcpy(fileName, "untitled");
    }
    else
    {
        for (i = 0; i < 22; ++i)
        {
            fileName[i] = (char)(tolower(modEntry->samples[editor.currSample].text[i]));
            if (fileName[i] == '\0')
                break;

            // convert illegal file name characters to spaces
                 if (fileName[i] ==  '<') fileName[i] = ' ';
            else if (fileName[i] ==  '>') fileName[i] = ' ';
            else if (fileName[i] ==  ':') fileName[i] = ' ';
            else if (fileName[i] ==  '"') fileName[i] = ' ';
            else if (fileName[i] ==  '/') fileName[i] = ' ';
            else if (fileName[i] == '\\') fileName[i] = ' ';
            else if (fileName[i] ==  '|') fileName[i] = ' ';
            else if (fileName[i] ==  '?') fileName[i] = ' ';
            else if (fileName[i] ==  '*') fileName[i] = ' ';
        }
    }

    // remove .wav/.iff from end of sample name (if present)
    if (!strncmp(fileName + (strlen(fileName) - 4), ".wav", 4) || !strncmp(fileName + (strlen(fileName) - 4), ".iff", 4))
        fileName[strlen(fileName) - 4] = '\0';

    switch (editor.diskop.smpSaveType)
    {
        case DISKOP_SMP_WAV: strcat(fileName, ".wav"); break;
        case DISKOP_SMP_IFF: strcat(fileName, ".iff"); break;
        case DISKOP_SMP_RAW:                           break;

        default: return (false); // make compiler happy
    }

    if (giveNewFreeFilename)
    {
        if (stat(fileName, &statBuffer) == 0)
        {
            for (j = 1; j <= 9999; ++j) // This number should satisfy all! ;)
            {
                memset(fileName, 0, sizeof (fileName));

                if (*modEntry->samples[editor.currSample].text == '\0')
                {
                    sprintf(fileName, "untitled-%d", j);
                }
                else
                {
                    for (i = 0; i < 22; ++i)
                    {
                        fileName[i] = (char)(tolower(modEntry->samples[editor.currSample].text[i]));
                        if (fileName[i] == '\0')
                            break;

                        // convert illegal file name characters to spaces
                             if (fileName[i] ==  '<') fileName[i] = ' ';
                        else if (fileName[i] ==  '>') fileName[i] = ' ';
                        else if (fileName[i] ==  ':') fileName[i] = ' ';
                        else if (fileName[i] ==  '"') fileName[i] = ' ';
                        else if (fileName[i] ==  '/') fileName[i] = ' ';
                        else if (fileName[i] == '\\') fileName[i] = ' ';
                        else if (fileName[i] ==  '|') fileName[i] = ' ';
                        else if (fileName[i] ==  '?') fileName[i] = ' ';
                        else if (fileName[i] ==  '*') fileName[i] = ' ';
                    }

                    // remove .wav/.iff from end of sample name (if present)
                    if (!strncmp(fileName + (strlen(fileName) - 4), ".wav", 4) || !strncmp(fileName + (strlen(fileName) - 4), ".iff", 4))
                        fileName[strlen(fileName) - 4] = '\0';

                    sprintf(fileName, "%s-%d", fileName, j);
                }

                switch (editor.diskop.smpSaveType)
                {
                    case DISKOP_SMP_WAV: strcat(fileName, ".wav"); break;
                    case DISKOP_SMP_IFF: strcat(fileName, ".iff"); break;
                    case DISKOP_SMP_RAW:                           break;

                    default: return (false);  // make compiler happy
                }

                if (stat(fileName, &statBuffer) != 0)
                    break;
            }
        }
    }

    if (checkIfFileExist)
    {
        if (stat(fileName, &statBuffer) == 0)
        {
            editor.ui.askScreenShown = true;
            editor.ui.askScreenType  = ASK_SAVESMP_OVERWRITE;

            pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
            setStatusMessage("OVERWRITE FILE ?", NO_CARRY);
            renderAskDialog();

            return (-1);
        }
    }

    if (editor.ui.askScreenShown)
    {
        editor.ui.answerNo       = false;
        editor.ui.answerYes      = false;
        editor.ui.askScreenShown = false;
    }

    f = fopen(fileName, "wb");
    if (f == NULL)
    {
        displayErrorMsg("FILE I/O ERROR !");
        terminalPrintf("Sample saving failed: file input/output error\n");

        return (false);
    }

    sampleSize = modEntry->samples[editor.currSample].length;

    switch (editor.diskop.smpSaveType)
    {
        case DISKOP_SMP_WAV:
        {
            s = &modEntry->samples[editor.currSample];

            wavHeader.format        = bigEndian ? SWAP32(0x45564157) : 0x45564157; // "WAVE"
            wavHeader.chunkID       = bigEndian ? SWAP32(0x46464952) : 0x46464952; // "RIFF"
            wavHeader.subchunk1ID   = bigEndian ? SWAP32(0x20746D66) : 0x20746D66; // "fmt "
            wavHeader.subchunk2ID   = bigEndian ? SWAP32(0x61746164) : 0x61746164; // "data"
            wavHeader.subchunk1Size = bigEndian ? SWAP32(16) : 16;
            wavHeader.subchunk2Size = bigEndian ? SWAP32(sampleSize) : sampleSize;
            wavHeader.chunkSize     = bigEndian ? SWAP32(wavHeader.subchunk2Size + 36) : (wavHeader.subchunk2Size + 36);
            wavHeader.audioFormat   = bigEndian ? SWAP16(1) : 1;
            wavHeader.numChannels   = bigEndian ? SWAP16(1) : 1;
            wavHeader.bitsPerSample = bigEndian ? SWAP16(8) : 8;
            wavHeader.sampleRate    = bigEndian ? SWAP32(16574) : 16574;
            wavHeader.byteRate      = bigEndian ? SWAP32(wavHeader.sampleRate * wavHeader.numChannels * wavHeader.bitsPerSample / 8)
                                    : (wavHeader.sampleRate * wavHeader.numChannels * wavHeader.bitsPerSample / 8);
            wavHeader.blockAlign    = bigEndian ? SWAP16(wavHeader.numChannels * wavHeader.bitsPerSample / 8)
                                    : (wavHeader.numChannels * wavHeader.bitsPerSample / 8);

            if (s->loopLength > 2)
            {
                wavHeader.chunkSize += sizeof (samplerChunk_t);

                memset(&samplerChunk, 0, sizeof (samplerChunk_t));

                samplerChunk.chunkID         = bigEndian ? SWAP32(0x6C706D73) : 0x6C706D73; // "smpl"
                samplerChunk.chunkSize       = bigEndian ? SWAP32(60) : 60;
                samplerChunk.dwSamplePeriod  = bigEndian ? SWAP32(1000000000U / 16574) : (1000000000U / 16574);
                samplerChunk.dwMIDIUnityNote = bigEndian ? SWAP32(60) : (60); // 60 = C-4
                samplerChunk.cSampleLoops    = bigEndian ? SWAP32(1) : 1;
                samplerChunk.loop.dwStart    = bigEndian ? (uint32_t)(SWAP32(s->loopStart)) : (uint32_t)(s->loopStart);
                samplerChunk.loop.dwEnd      = bigEndian ? (uint32_t)(SWAP32((s->loopStart + s->loopLength) - 1)) :
                                                                  (uint32_t)((s->loopStart + s->loopLength) - 1);
            }

            fwrite(&wavHeader, sizeof (wavHeader_t), 1, f);

            for (i = 0; i < (int32_t)(sampleSize); ++i)
            {
                smp = modEntry->sampleData[modEntry->samples[editor.currSample].offset + i] + 128;
                fputc(smp, f);
            }

            if (sampleSize & 1)
                fputc(0, f); // pad align byte

            if (s->loopLength > 2)
                fwrite(&samplerChunk, sizeof (samplerChunk), 1, f);

            if (sampleNameIsEmpty(modEntry->samples[editor.currSample].text))
            {
                terminalPrintf("Sample %02x \"untitled\" saved as .WAV\n", editor.currSample + 1);
            }
            else
            {
                terminalPrintf("Sample %02x \"", editor.currSample + 1);

                sampleTextLen = 22;
                for (i = 21; i >= 0; --i)
                {
                    if (modEntry->samples[editor.currSample].text[i] == '\0')
                        sampleTextLen--;
                    else
                        break;
                }

                for (i = 0; i < sampleTextLen; ++i)
                {
                    if (modEntry->samples[editor.currSample].text[i] != '\0')
                        teriminalPutChar(tolower(modEntry->samples[editor.currSample].text[i]));
                    else
                        teriminalPutChar(' ');
                }

                terminalPrintf("\" saved as .WAV\n");
            }
        }
        break;

        case DISKOP_SMP_IFF:
        {
            // dwords are big-endian in IFF
            loopStart = modEntry->samples[editor.currSample].loopStart  & 0xFFFFFFFE;
            loopLen   = modEntry->samples[editor.currSample].loopLength & 0xFFFFFFFE;

            if (!bigEndian) loopStart = SWAP32(loopStart);
            if (!bigEndian) loopLen   = SWAP32(loopLen);

            iffSize = bigEndian ? (sampleSize + 100) : SWAP32(sampleSize + 100);
            iffSampleSize = bigEndian ? sampleSize : SWAP32(sampleSize);

            fputc(0x46, f);fputc(0x4F, f);fputc(0x52, f);fputc(0x4D, f);    // "FORM"
            fwrite(&iffSize, 4, 1, f);

            fputc(0x38, f);fputc(0x53, f);fputc(0x56, f);fputc(0x58, f);    // "8SVX"
            fputc(0x56, f);fputc(0x48, f);fputc(0x44, f);fputc(0x52, f);    // "VHDR"
            fputc(0x00, f);fputc(0x00, f);fputc(0x00, f);fputc(0x14, f);    // 0x00000014

            if (modEntry->samples[editor.currSample].loopLength > 2)
            {
                fwrite(&loopStart, 4, 1, f);
                fwrite(&loopLen,   4, 1, f);
            }
            else
            {
                fwrite(&iffSampleSize, 4, 1, f);
                fputc(0x00, f);fputc(0x00, f);fputc(0x00, f);fputc(0x00, f);// 0x00000000
            }

            fputc(0x00, f);fputc(0x00, f);fputc(0x00, f);fputc(0x00, f);// 0x00000000

            fputc(0x41, f);fputc(0x56, f); // 16726 (rate)
            fputc(0x01, f);fputc(0x00, f); // numSamples and compression

            tmp32 = modEntry->samples[editor.currSample].volume * 1024;
            if (!bigEndian) tmp32 = SWAP32(tmp32);
            fwrite(&tmp32, 4, 1, f);

            fputc(0x4E, f);fputc(0x41, f);fputc(0x4D, f);fputc(0x45, f);    // "NAME"
            fputc(0x00, f);fputc(0x00, f);fputc(0x00, f);fputc(0x16, f);    // 0x00000016

            for (i = 0; i < 22; ++i)
                fputc(tolower(modEntry->samples[editor.currSample].text[i]), f);

            fputc(0x41, f);fputc(0x4E, f);fputc(0x4E, f);fputc(0x4F, f);    // "ANNO"
            fputc(0x00, f);fputc(0x00, f);fputc(0x00, f);fputc(0x15, f);    // 0x00000015
            fprintf(f, "ProTracker 2.3D clone");
            fputc(0x00, f); // even padding

            fputc(0x42, f);fputc(0x4F, f);fputc(0x44, f);fputc(0x59, f);    // "BODY"
            fwrite(&iffSampleSize, 4, 1, f);
            fwrite(modEntry->sampleData + modEntry->samples[editor.currSample].offset, 1, sampleSize, f);

            // shouldn't happen, but in just case: safety even padding
            if (sampleSize & 1)
                fputc(0x00, f);

            if (sampleNameIsEmpty(modEntry->samples[editor.currSample].text))
            {
                terminalPrintf("Sample %02x \"untitled\" saved as .IFF\n", editor.currSample + 1);
            }
            else
            {
                terminalPrintf("Sample %02x \"", editor.currSample + 1);

                sampleTextLen = 22;
                for (i = 21; i >= 0; --i)
                {
                    if (modEntry->samples[editor.currSample].text[i] == '\0')
                        sampleTextLen--;
                    else
                        break;
                }

                for (i = 0; i < sampleTextLen; ++i)
                {
                    if (modEntry->samples[editor.currSample].text[i] != '\0')
                        teriminalPutChar(tolower(modEntry->samples[editor.currSample].text[i]));
                    else
                        teriminalPutChar(' ');
                }

                terminalPrintf("\" saved as .IFF\n");
            }
        }
        break;

        case DISKOP_SMP_RAW:
        {
            fwrite(modEntry->sampleData + modEntry->samples[editor.currSample].offset, 1, sampleSize, f);

            if (sampleNameIsEmpty(modEntry->samples[editor.currSample].text))
            {
                terminalPrintf("Sample %02x \"untitled\" saved as raw file\n", editor.currSample + 1);
            }
            else
            {
                terminalPrintf("Sample %02x \"", editor.currSample + 1);

                sampleTextLen = 22;
                for (i = 21; i >= 0; --i)
                {
                    if (modEntry->samples[editor.currSample].text[i] == '\0')
                        sampleTextLen--;
                    else
                        break;
                }

                for (i = 0; i < sampleTextLen; ++i)
                {
                    if (modEntry->samples[editor.currSample].text[i] != '\0')
                        teriminalPutChar(tolower(modEntry->samples[editor.currSample].text[i]));
                    else
                        teriminalPutChar(' ');
                }

                terminalPrintf("\" saved as raw file\n");
            }
        }
        break;

        default: return (false); break;  // make compiler happy
    }

    fclose(f);

    editor.diskop.cached = false;
    if (editor.ui.diskOpScreenShown)
        editor.ui.updateDiskOpFileList = true;

    displayMsg("SAMPLE SAVED !");
    setMsgPointer();

    return (true);
}

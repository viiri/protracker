/* The low-pass/high-pass routines were coded by aciddose, and he agreed on 
** using the WTFPL license for the code.
*/

#ifndef __PT_AUDIO_H
#define __PT_AUDIO_H

#include <stdint.h>

typedef struct lossyIntegrator_t
{
    float buffer[2], coeff[2];
} lossyIntegrator_t;

void lossyIntegrator(lossyIntegrator_t *filter, float *in, float *out);
void lossyIntegratorHighPass(lossyIntegrator_t *filter, float *in, float *out);

int8_t quantizeFloatTo8bit(float smpFloat);
int8_t quantize32bitTo8bit(int32_t smp32);
int8_t quantize24bitTo8bit(int32_t smp32);
int8_t quantize16bitTo8bit(int16_t smp16);
void normalize32bitSigned(int32_t *sampleData, uint32_t sampleLength);
void normalize24bitSigned(int32_t *sampleData, uint32_t sampleLength);
void normalize16bitSigned(int16_t *sampleData, uint32_t sampleLength);
void normalize8bitFloatSigned(float *sampleData, uint32_t sampleLength);
void setLEDFilter(uint8_t state);
void toggleLEDFilter(void);
int8_t renderToWav(char *fileName, int8_t checkIfFileExist);
void toggleAmigaPanMode(void);
void toggleLowPassFilter(void);

void paulaRestartDMA(uint8_t ch);
void paulaSetPeriod(uint8_t ch, uint16_t period);
void paulaSetVolume(uint8_t ch, uint16_t vol);
void paulaSetLength(uint8_t ch, uint32_t len);
void paulaSetData(uint8_t ch, const int8_t *src);

void clearPaulaAndScopes(void);
void mixerUpdateLoops(void);
void mixerKillVoice(uint8_t ch);
void turnOffVoices(void);
void mixerCalcVoicePans(uint8_t stereoSeparation);
void mixerSetSamplesPerTick(int32_t val);
void mixerClearSampleCounter(void);
void handleExternalVoiceUpdates(void);
void outputAudio(int16_t *target, int32_t numSamples);

#endif

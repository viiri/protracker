#ifndef __PT_CONFIG_H
#define __PT_CONFIG_H

#include <stdint.h>

struct ptConfig_t
{
    char *defaultDiskOpDir;
    int8_t dottedCenterFlag, pattDots, a500LowPassFilter, compoMode;
    int8_t stereoSeparation, videoScaleFactor, blepSynthesis, transDel;
    int8_t modDot, accidental, blankZeroFlag, realVuMeters;
    int16_t quantizeValue;
    uint32_t soundFrequency;
} ptConfig;

int8_t loadConfig();

#endif

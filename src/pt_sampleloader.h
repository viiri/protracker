#ifndef __PT_SAMPLELOADER_H
#define __PT_SAMPLELOADER_H

#include <stdint.h>
#include "pt_unicode.h"

void extLoadWAVSampleCallback(int8_t downSample);
int8_t saveSample(int8_t checkIfFileExist, int8_t giveNewFreeFilename);
int8_t loadSample(UNICHAR *fileName, char *entryName);

#endif

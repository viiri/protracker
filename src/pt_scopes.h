#ifndef __PT_SCOPES_H
#define __PT_SCOPES_H

#include <stdint.h>

typedef struct scopeChannel_t
{
    // stuff updated by other threads
    volatile uint8_t active, updateLength, updateLoopFlag, updateData, updatePhase, updateActive;
    int8_t volume, loopFlag, newLoopFlag;
    int32_t length, newLength, phase;
    const int8_t *newData, *data;
    float delta_f;

    // internal stuff ONLY read/written by scope thread
    int8_t _active, _loopFlag, _newLoopFlag;
    int32_t _length, _newLength;
    const int8_t *_newData, *_data;
    float _phase_f;
} scopeChannel_t;

extern scopeChannel_t scope[4];

void updateScopes(void);
void drawScopes(void);
int32_t scopeThreadFunc(void *ptr);
uint8_t initScopes(void);

#endif

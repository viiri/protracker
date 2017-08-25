#ifndef __PT_SCOPES_H
#define __PT_SCOPES_H

#include <stdint.h>

typedef struct scopeChannel_t
{
    volatile uint8_t active, retriggered;
    int8_t volume, loopFlag, newLoopFlag;
    int32_t length, newLength, phase;
    const int8_t *newData, *data;
    float phase_f, delta_f;
} scopeChannel_t;

extern scopeChannel_t scope[4];

void updateScopes(void);
void drawScopes(void);
int32_t scopeThreadFunc(void *ptr);
uint8_t initScopes(void);

#endif

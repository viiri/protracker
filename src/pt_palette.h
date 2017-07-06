#ifndef __PT_PALETTE_H
#define __PT_PALETTE_H

#include <stdint.h>

enum
{
    // -----------------------------
    PAL_BACKGRD   = 0,
    PAL_BORDER    = 1,
    PAL_GENBKG    = 2,
    PAL_GENBKG2   = 3,
    PAL_QADSCP    = 4,
    PAL_PATCURSOR = 5,
    PAL_GENTXT    = 6,
    PAL_PATTXT    = 7,
    PAL_SMPMRK    = 8,
    // -----------------------------
    PAL_SAMPLLINE  = 9,
    PAL_LOOPPIN    = 10,
    PAL_TEXTMARK   = 11,
    PAL_MOUSE_1    = 12,
    PAL_MOUSE_2    = 13,
    PAL_MOUSE_3    = 14,
    // -----------------------------
    PAL_COLORKEY = 15,
    // -----------------------------
    PALETTE_NUM,
    // -----------------------------

    POINTER_MODE_IDLE      = 0,
    POINTER_MODE_EDIT      = 1,
    POINTER_MODE_PLAY      = 2,
    POINTER_MODE_MSG1      = 3,
    POINTER_MODE_LOAD      = 4,
    POINTER_MODE_EDIT_PLAY = 5,
    POINTER_MODE_READ_DIR  = 6
};

void setMsgPointer(void);
void pointerErrorMode(void);
void pointerSetMode(int8_t pointerMode, uint8_t carry);
void pointerSetPreviousMode(void);

#endif

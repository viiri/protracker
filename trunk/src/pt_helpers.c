#include <SDL2/SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h> // toupper()
#ifndef _WIN32
#include <unistd.h>
#endif
#include "pt_helpers.h"
#include "pt_header.h"
#include "pt_tables.h"
#include "pt_palette.h"

extern SDL_Window *window; // pt_main.c

int32_t my_strnicmp(const char *s1, const char *s2, size_t n)
{
    const char *s2end = s2 + n;

    while ((s2 < s2end) && (*s2 != 0) && (toupper(*s1) == toupper(*s2)))
    {
        ++s1;
        ++s2;
    }

    if (s2end == s2)
        return (0);

    return (int32_t)(toupper(*s1) - toupper(*s2));
}

int32_t my_stricmp(const char *s1, const char *s2)
{
    while ((*s2 != '\0') && (toupper(*s1) == toupper(*s2)))
    {
        ++s1;
        ++s2;
    }

    return (int32_t)(toupper(*s1) - toupper(*s2));
}

void showErrorMsgBox(const char *fmt, ...)
{
    char strBuf[1024];
    va_list args;

    // format the text string
    va_start(args, fmt);
    vsnprintf(strBuf, sizeof (strBuf), fmt, args);
    va_end(args);

    // window can be NULL here, no problem...
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Critical Error", strBuf, window);
}

#ifndef _WIN32
int8_t changePathToHome(void)
{
    char *homePath;

    homePath = getenv("HOME");
    if ((homePath != NULL) && (chdir(homePath) == 0))
        return (true);

    return (false);
}
#endif

int8_t sampleNameIsEmpty(char *name)
{
    uint8_t i, n;

    if (name == NULL)
        return (true);

    n = 0;
    for (i = 0; i < 22; ++i)
    {
        if (name[i] == '\0')
            ++n;
    }

    return (n == 22);
}

int8_t moduleNameIsEmpty(char *name)
{
    uint8_t i, n;

    if (name == NULL)
        return (true);

    n = 0;
    for (i = 0; i < 20; ++i)
    {
        if (name[i] == '\0')
            ++n;
    }

    return (n == 20);
}

void updateWindowTitle(int8_t modified)
{
    char titleTemp[128];

    if (modified)
        modEntry->modified = true;

    if (modEntry->head.moduleTitle[0] != '\0')
    {
        if (modified)
        {
            if (editor.diskop.modDot)
                sprintf(titleTemp, "ProTracker v2.3D clone *(mod.%s) - compiled %s", modEntry->head.moduleTitle, __DATE__);
            else
                sprintf(titleTemp, "ProTracker v2.3D clone *(%s.mod) - compiled %s", modEntry->head.moduleTitle, __DATE__);
        }
        else
        {
            if (editor.diskop.modDot)
                sprintf(titleTemp, "ProTracker v2.3D clone (mod.%s) - compiled %s", modEntry->head.moduleTitle, __DATE__);
            else
                sprintf(titleTemp, "ProTracker v2.3D clone (%s.mod) - compiled %s", modEntry->head.moduleTitle, __DATE__);
        }
    }
    else
    {
        if (modified)
        {
            if (editor.diskop.modDot)
                sprintf(titleTemp, "ProTracker v2.3D clone *(mod.untitled) - compiled %s", __DATE__);
            else
                sprintf(titleTemp, "ProTracker v2.3D clone *(untitled.mod) - compiled %s", __DATE__);
        }
        else
        {
            if (editor.diskop.modDot)
                sprintf(titleTemp, "ProTracker v2.3D clone (mod.untitled) - compiled %s", __DATE__);
            else
                sprintf(titleTemp, "ProTracker v2.3D clone (untitled.mod) - compiled %s", __DATE__);
        }
    }

     SDL_SetWindowTitle(window, titleTemp);
}

void recalcChordLength(void)
{
    int8_t note;
    moduleSample_t *s;

    s = &modEntry->samples[editor.currSample];

    if (editor.chordLengthMin)
    {
        note = MAX(MAX((editor.note1 == 36) ? -1 : editor.note1,
                       (editor.note2 == 36) ? -1 : editor.note2),
                   MAX((editor.note3 == 36) ? -1 : editor.note3,
                       (editor.note4 == 36) ? -1 : editor.note4));
    }
    else
    {
        note = MIN(MIN(editor.note1, editor.note2), MIN(editor.note3, editor.note4));
    }

    if ((note < 0) || (note > 35))
    {
        editor.chordLength = 0;
    }
    else
    {
        PT_ASSERT(editor.tuningNote < 36);

        if (editor.tuningNote < 36)
        {
            editor.chordLength = ((s->length * periodTable[(37 * s->fineTune) + note]) / periodTable[editor.tuningNote]) & 0xFFFFFFFE;
            if (editor.chordLength > MAX_SAMPLE_LEN)
                editor.chordLength = MAX_SAMPLE_LEN;
        }
    }

    if (editor.ui.editOpScreenShown && (editor.ui.editOpScreen == 3))
        editor.ui.updateLengthText = true;
}

uint8_t hexToInteger2(char *ptr)
{
    char lo, hi;

    // This routine must ONLY be used on an address
    // where two bytes can be read. It will mess up
    // if the ASCII values are not '0 .. 'F'

    hi = ptr[0];
    lo = ptr[1];

    // high nybble
    if (hi >= 'a')
        hi -= ' ';

    hi -= '0';
    if (hi > 9)
        hi -= 7;

    // low nybble
    if (lo >= 'a')
        lo -= ' ';

    lo -= '0';
    if (lo > 9)
        lo -= 7;

    return ((hi << 4) | lo);
}

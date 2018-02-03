#include <stdio.h>
#include <stdint.h>
#include <ctype.h> // tolower()
#include <SDL2/SDL.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include "pt_textout.h"
#include "pt_helpers.h"
#include "pt_visuals.h"
#include "pt_palette.h"
#include "pt_diskop.h"
#include "pt_edit.h"
#include "pt_sampler.h"
#include "pt_audio.h"
#include "pt_keyboard.h"
#include "pt_tables.h"
#include "pt_modloader.h"
#include "pt_mouse.h"
#include "pt_terminal.h"

void sampleUpButton(void);   // pt_mouse.c
void sampleDownButton(void); // pt_mouse.c

#ifdef _WIN32
extern uint8_t windowsKeyIsDown;
extern HHOOK g_hKeyboardHook;
#endif

void movePatCurPrevCh(void);
void movePatCurNextCh(void);
void movePatCurRight(void);
void movePatCurLeft(void);

int8_t handleGeneralModes(SDL_Scancode keyEntry);
int8_t handleTextEditMode(SDL_Scancode keyEntry);

extern SDL_Window *window;

char scanCodeToUSKey(SDL_Scancode key)
{
    int32_t keyOut;

    switch (key)
    {
        case SDL_SCANCODE_MINUS:          return (SDLK_MINUS);
        case SDL_SCANCODE_EQUALS:         return (SDLK_EQUALS);
        case SDL_SCANCODE_LEFTBRACKET:    return (SDLK_LEFTBRACKET);
        case SDL_SCANCODE_RIGHTBRACKET:   return (SDLK_RIGHTBRACKET);
        case SDL_SCANCODE_BACKSLASH:      return (SDLK_BACKSLASH);
        case SDL_SCANCODE_SEMICOLON:      return (SDLK_SEMICOLON);
        case SDL_SCANCODE_APOSTROPHE:     return (SDLK_QUOTE);
        case SDL_SCANCODE_GRAVE:          return (SDLK_BACKQUOTE);
        case SDL_SCANCODE_COMMA:          return (SDLK_COMMA);
        case SDL_SCANCODE_PERIOD:         return (SDLK_PERIOD);
        case SDL_SCANCODE_SLASH:          return (SDLK_SLASH);
        case SDL_SCANCODE_NONUSBACKSLASH: return (SDLK_LESS);
        default: break;
    }

    keyOut = SDL_GetKeyFromScancode(key);
    if ((keyOut < -128) || (keyOut > 127))
        return (SDLK_UNKNOWN);

    return ((char)(keyOut));
}

void updateKeyModifiers(void)
{
    uint32_t modState;

    modState = SDL_GetModState();

    input.keyb.leftCtrlKeyDown = (modState & KMOD_LCTRL)  ? true : false;
    input.keyb.leftAltKeyDown  = (modState & KMOD_LALT)   ? true : false;
    input.keyb.shiftKeyDown    = (modState & (KMOD_LSHIFT | KMOD_RSHIFT)) ? true : false;

#ifndef _WIN32 // MS Windows: handled in lowLevelKeyboardProc
    input.keyb.leftAmigaKeyDown = (modState & KMOD_LGUI) ? true : false;
#endif
}

#ifdef _WIN32
// for taking control over windows key and numlock on keyboard if app has focus
LRESULT CALLBACK lowLevelKeyboardProc(int32_t nCode, WPARAM wParam, LPARAM lParam)
{
    uint8_t bEatKeystroke;
    KBDLLHOOKSTRUCT *p;
    SDL_Event inputEvent;

    if ((nCode < 0) || (nCode != HC_ACTION)) // do not process message
        return (CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam));

    bEatKeystroke = false;
    p = (KBDLLHOOKSTRUCT *)(lParam);

    switch (wParam)
    {
        case WM_KEYUP:
        case WM_KEYDOWN:
        {
            bEatKeystroke = (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) && ((p->vkCode == VK_LWIN) || (p->vkCode == VK_NUMLOCK));

            if (bEatKeystroke)
            {
                if (wParam == WM_KEYDOWN)
                {
                    if (p->vkCode == VK_NUMLOCK)
                    {
                        memset(&inputEvent, 0, sizeof (SDL_Event));
                        inputEvent.type = SDL_KEYDOWN;
                        inputEvent.key.type = SDL_KEYDOWN;
                        inputEvent.key.state = 1;
                        inputEvent.key.keysym.scancode = (SDL_Scancode)(69);
                        inputEvent.key.keysym.mod = KMOD_NUM;
                        inputEvent.key.keysym.scancode = SDL_SCANCODE_NUMLOCKCLEAR;

                        SDL_PushEvent(&inputEvent);
                    }
                    else if (!windowsKeyIsDown)
                    {
                        windowsKeyIsDown = true;
                        input.keyb.leftAmigaKeyDown = true;

                        memset(&inputEvent, 0, sizeof (SDL_Event));
                        inputEvent.type = SDL_KEYDOWN;
                        inputEvent.key.type = SDL_KEYDOWN;
                        inputEvent.key.state = 1;
                        inputEvent.key.keysym.scancode = (SDL_Scancode)(91);
                        inputEvent.key.keysym.scancode = SDL_SCANCODE_LGUI;

                        SDL_PushEvent(&inputEvent);
                    }
                }
                else if (wParam == WM_KEYUP)
                {
                    if (p->vkCode == VK_NUMLOCK)
                    {
                        memset(&inputEvent, 0, sizeof (SDL_Event));
                        inputEvent.type = SDL_KEYUP;
                        inputEvent.key.type = SDL_KEYUP;
                        inputEvent.key.keysym.scancode = (SDL_Scancode)(69);
                        inputEvent.key.keysym.scancode = SDL_SCANCODE_NUMLOCKCLEAR;

                        SDL_PushEvent(&inputEvent);
                    }
                    else
                    {
                        windowsKeyIsDown = false;
                        input.keyb.leftAmigaKeyDown = false;

                        memset(&inputEvent, 0, sizeof (SDL_Event));
                        inputEvent.type = SDL_KEYUP;
                        inputEvent.key.type = SDL_KEYUP;
                        inputEvent.key.keysym.scancode = (SDL_Scancode)(91);
                        inputEvent.key.keysym.scancode = SDL_SCANCODE_LGUI;

                        SDL_PushEvent(&inputEvent);
                    }
                }
            }
            break;
        }

        default: break;
    }

    return (bEatKeystroke ? true : (CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam)));
}
#endif

// these four functions are for the text edit cursor
void textMarkerMoveLeft(void)
{
    if (editor.ui.dstPos > 0)
    {
        removeTextEditMarker();
        editor.ui.dstPos--;
        editor.ui.lineCurX -= FONT_CHAR_W;
        renderTextEditMarker();
    }
    else
    {
        if (editor.ui.dstOffset != NULL)
        {
            (*editor.ui.dstOffset)--;

            if (editor.ui.editObject == PTB_DO_DATAPATH)
                editor.ui.updateDiskOpPathText = true;
            else if (editor.ui.editObject == PTB_PE_PATTNAME)
                editor.ui.updatePosEd = true;
        }
    }
}

void textMarkerMoveRight(void)
{
    if (editor.ui.editTextType == TEXT_EDIT_STRING)
    {
        if (editor.ui.dstPos < (editor.ui.textLength - 1))
        {
            removeTextEditMarker();
            editor.ui.dstPos++;
            editor.ui.lineCurX += FONT_CHAR_W;
            renderTextEditMarker();
        }
        else
        {
            if (editor.ui.dstOffset != NULL)
            {
                (*editor.ui.dstOffset)++;

                if (editor.ui.editObject == PTB_DO_DATAPATH)
                    editor.ui.updateDiskOpPathText = true;
                else if (editor.ui.editObject == PTB_PE_PATTNAME)
                    editor.ui.updatePosEd = true;
            }
        }
    }
    else
    {
        // we end up here when entering a number/hex digit

        if (editor.ui.dstPos < editor.ui.numLen)
            removeTextEditMarker();

        editor.ui.dstPos++;
        editor.ui.lineCurX += FONT_CHAR_W;

        if (editor.ui.dstPos < editor.ui.numLen)
            renderTextEditMarker();

        // don't clamp, dstPos is tested elsewhere to check if done editing a number
    }
}

void textCharPrevious(void)
{
    if (editor.ui.editTextType != TEXT_EDIT_STRING)
    {
        if (editor.ui.dstPos > 0)
        {
            removeTextEditMarker();
            editor.ui.dstPos--;
            editor.ui.lineCurX -= FONT_CHAR_W;
            renderTextEditMarker();
        }

        return;
    }

    if (editor.mixFlag && (editor.ui.dstPos <= 4))
        return;

    if (editor.ui.editPos > editor.ui.showTextPtr)
    {
        removeTextEditMarker();

        editor.ui.editPos--;
        textMarkerMoveLeft();

        if (editor.mixFlag)
        {
            if (editor.ui.dstPos == 12)
            {
                editor.ui.editPos--;
                textMarkerMoveLeft();
                editor.ui.editPos--;
                textMarkerMoveLeft();
                editor.ui.editPos--;
                textMarkerMoveLeft();
                editor.ui.editPos--;
                textMarkerMoveLeft();
            }
            else if (editor.ui.dstPos == 6)
            {
                editor.ui.editPos--;
                textMarkerMoveLeft();
            }
        }

        renderTextEditMarker();
    }

    editor.ui.dstOffsetEnd = false;
}

void textCharNext(void)
{
    if (editor.ui.editTextType != TEXT_EDIT_STRING)
    {
        if (editor.ui.dstPos < (editor.ui.numLen - 1))
        {
            removeTextEditMarker();
            editor.ui.dstPos++;
            editor.ui.lineCurX += FONT_CHAR_W;
            renderTextEditMarker();
        }

        return;
    }

    if (editor.mixFlag && (editor.ui.dstPos >= 14))
        return;

    if (editor.ui.editPos < editor.ui.textEndPtr)
    {
        if (*editor.ui.editPos != '\0')
        {
            removeTextEditMarker();

            editor.ui.editPos++;
            textMarkerMoveRight();

            if (editor.mixFlag)
            {
                if (editor.ui.dstPos == 9)
                {
                    editor.ui.editPos++;
                    textMarkerMoveRight();
                    editor.ui.editPos++;
                    textMarkerMoveRight();
                    editor.ui.editPos++;
                    textMarkerMoveRight();
                    editor.ui.editPos++;
                    textMarkerMoveRight();
                    
                }
                else if (editor.ui.dstPos == 6)
                {
                    editor.ui.editPos++;
                    textMarkerMoveRight();
                }
            }

            renderTextEditMarker();
        }
        else
        {
            editor.ui.dstOffsetEnd = true;
        }
    }
    else
    {
        editor.ui.dstOffsetEnd = true;
    }
}
// --------------------------------

void keyUpHandler(SDL_Scancode keyEntry)
{
    if (keyEntry == SDL_SCANCODE_KP_PLUS)
    {
        input.keyb.keypadEnterKeyDown = false;
    }
    else if (keyEntry == SDL_SCANCODE_CAPSLOCK)
    {
        editor.repeatKeyFlag ^= 1;
        input.keyb.lastRepKey = SDL_SCANCODE_UNKNOWN;
    }

    if (keyEntry == input.keyb.lastRepKey)
        input.keyb.lastRepKey = SDL_SCANCODE_UNKNOWN;

    switch (keyEntry)
    {
        // modifiers shouldn't reset keyb repeat/delay flags & counters
        case SDL_SCANCODE_LCTRL:
        case SDL_SCANCODE_RCTRL:
        case SDL_SCANCODE_LSHIFT:
        case SDL_SCANCODE_RSHIFT:
        case SDL_SCANCODE_LALT:
        case SDL_SCANCODE_RALT:
        case SDL_SCANCODE_LGUI:
        case SDL_SCANCODE_RGUI:
        case SDL_SCANCODE_MENU:
        case SDL_SCANCODE_MODE:
        case SDL_SCANCODE_CAPSLOCK:
        break;

        default:
        {
            input.keyb.repeatKey     = false;
            input.keyb.delayKey      = false;
            input.keyb.repeatCounter = 0;
            input.keyb.delayCounter  = 0;
        }
        break;
    }
}

void handleTerminalKeys(SDL_Scancode keyEntry);

void keyDownHandler(SDL_Scancode keyEntry, SDL_Keycode keyCode)
{
    int8_t chTmp;
    uint8_t blockFrom, blockTo;
    int16_t i, j;
    note_t *noteSrc, *noteDst, noteTmp;
    moduleSample_t *s;

    if (keyEntry == SDL_SCANCODE_KP_PLUS)
        input.keyb.keypadEnterKeyDown = true;

    // TOGGLE FULLSCREEN (should always react)
    if (!input.keyb.leftAltKeyDown && (keyEntry == SDL_SCANCODE_F11))
    {
        toggleFullscreen();
        return;
    }

    // don't handle input if an error message wait is active or if an unknown key is passed
    if ((editor.errorMsgActive && editor.errorMsgBlock) || (keyEntry == SDL_SCANCODE_UNKNOWN))
        return;

    // if no ALT/SHIFT/CTRL/AMIGA, update last key for repeat routine

    if ((keyEntry != SDL_SCANCODE_LALT)     && (keyEntry != SDL_SCANCODE_RALT)   &&
        (keyEntry != SDL_SCANCODE_LCTRL)    && (keyEntry != SDL_SCANCODE_RCTRL)  &&
        (keyEntry != SDL_SCANCODE_LSHIFT)   && (keyEntry != SDL_SCANCODE_RSHIFT) &&
        (keyEntry != SDL_SCANCODE_LGUI)     && (keyEntry != SDL_SCANCODE_RGUI)   &&
        (keyEntry != SDL_SCANCODE_MENU)     && (keyEntry != SDL_SCANCODE_MODE)   &&
        (keyEntry != SDL_SCANCODE_CAPSLOCK) && (keyEntry != SDL_SCANCODE_ESCAPE))
    {
        if (editor.repeatKeyFlag)
        {
            // if Repeat Flag, repeat all keys
            if (!input.keyb.repeatKey)
                input.keyb.delayCounter = 0;

            input.keyb.repeatKey = true;
            input.keyb.delayKey  = true;
        }

        input.keyb.repeatCounter = 0;
        input.keyb.lastRepKey = keyEntry;
    }

    // TERMINAL KEYS
    if (editor.ui.terminalShown)
    {
        handleTerminalKeys(keyEntry);
        return;
    }

    // ENTRY JUMPING IN DISK OP. FILELIST
    if (editor.ui.diskOpScreenShown && input.keyb.shiftKeyDown && !editor.ui.editTextFlag)
    {
        if ((keyCode >= 32) && (keyCode <= 126))
        {
            handleEntryJumping(keyCode);
            return;
        }
    }

    if (!handleGeneralModes(keyEntry)) return;
    if (!handleTextEditMode(keyEntry)) return;
    if (editor.ui.samplerVolBoxShown)  return;

    if (editor.ui.samplerFiltersBoxShown)
    {
        handleEditKeys(keyEntry, EDIT_NORMAL);
        return;
    }

    // GENERAL KEYS
    switch (keyEntry)
    {
        case SDL_SCANCODE_NONUSBACKSLASH: turnOffVoices(); break; // magic "kill all voices" button

        case SDL_SCANCODE_APOSTROPHE:
        {
            if (editor.autoInsFlag)
            {
                if (input.keyb.shiftKeyDown)
                    editor.autoInsSlot -= 4;
                else
                    editor.autoInsSlot--;

                if (editor.autoInsSlot < 0)
                    editor.autoInsSlot = 0;

                editor.ui.updateTrackerFlags = true;
            }
        }
        break;

        case SDL_SCANCODE_BACKSLASH:
        {
            if (input.keyb.leftAltKeyDown)
            {
                if (handleSpecialKeys(keyEntry))
                {
                    if (editor.currMode != MODE_RECORD)
                        modSetPos(DONT_SET_ORDER, (modEntry->currRow + editor.editMoveAdd) & 63);
                }
            }
            else
            {
                if (editor.autoInsFlag)
                {
                    if (input.keyb.shiftKeyDown)
                        editor.autoInsSlot += 4;
                    else
                        editor.autoInsSlot++;

                    if (editor.autoInsSlot > 9)
                        editor.autoInsSlot = 9;
                }
                else
                {
                    editor.pNoteFlag = (editor.pNoteFlag + 1) % 3;
                }

                editor.ui.updateTrackerFlags = true;
            }
        }
        break;

#ifdef __APPLE__
        case SDL_SCANCODE_RGUI:
#else
        case SDL_SCANCODE_RALT:
#endif
        {
            // right Amiga key on Amiga keyb

            if (!editor.ui.askScreenShown)
            {
                editor.playMode = PLAY_MODE_NORMAL;
                modPlay(DONT_SET_PATTERN, modEntry->currOrder, DONT_SET_ROW);

                editor.currMode = MODE_PLAY;
                pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
                setStatusMessage(editor.allRightText, DO_CARRY);
            }
        }
        break;

#ifdef __APPLE__
        case SDL_SCANCODE_RALT:
#else
        case SDL_SCANCODE_RCTRL:
#endif
        {
            // right alt on Amiga keyb

            if (!editor.ui.askScreenShown)
            {
                editor.playMode = PLAY_MODE_PATTERN;
                modPlay(modEntry->currPattern, DONT_SET_ORDER, DONT_SET_ROW);

                editor.currMode = MODE_PLAY;
                pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
                setStatusMessage(editor.allRightText, DO_CARRY);
            }
        }
        break;

        case SDL_SCANCODE_RSHIFT:
        {
            // right shift on Amiga keyb

            if (!editor.ui.samplerScreenShown && !editor.ui.askScreenShown)
            {
                editor.playMode = PLAY_MODE_PATTERN;
                modPlay(modEntry->currPattern, DONT_SET_ORDER, DONT_SET_ROW);

                editor.currMode = MODE_RECORD;
                pointerSetMode(POINTER_MODE_EDIT, DO_CARRY);
                setStatusMessage(editor.allRightText, DO_CARRY);
            }
        }
        break;

        case SDL_SCANCODE_ESCAPE:
        {
            if (editor.ui.posEdScreenShown)
            {
                editor.ui.posEdScreenShown = false;
                displayMainScreen();
            }
            else if (editor.ui.diskOpScreenShown)
            {
                editor.ui.diskOpScreenShown = false;
                displayMainScreen();
            }
            else if (editor.ui.samplerScreenShown)
            {
                exitFromSam();
            }
            else if (editor.ui.editOpScreenShown)
            {
                editor.ui.editOpScreenShown = false;
                displayMainScreen();
            }
            else
            {
                editor.ui.askScreenShown = true;
                editor.ui.askScreenType  = ASK_QUIT;

                pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
                setStatusMessage("REALLY QUIT ?", NO_CARRY);
                renderAskDialog();

                return;
            }

            pointerSetPreviousMode();
            setPrevStatusMessage();
        }
        break;

        case SDL_SCANCODE_INSERT:
        {
            if (editor.ui.samplerScreenShown)
            {
                samplerSamPaste();
                return;
            }
        }
        break;

        case SDL_SCANCODE_PAGEUP:
        {
            if (editor.ui.posEdScreenShown)
            {
                if (modEntry->currOrder > 0)
                {
                    if ((modEntry->currOrder - (POSED_LIST_SIZE - 1)) > 0)
                        modSetPos(modEntry->currOrder - (POSED_LIST_SIZE - 1), DONT_SET_ROW);
                    else
                        modSetPos(0, DONT_SET_ROW);
                }
            }
            else if (editor.ui.diskOpScreenShown)
            {
                editor.diskop.scrollOffset -= (DISKOP_LIST_SIZE - 1);
                if (editor.diskop.scrollOffset < 0)
                    editor.diskop.scrollOffset = 0;

                editor.ui.updateDiskOpFileList = true;
            }
            else
            {
                if ((editor.currMode == MODE_IDLE) || (editor.currMode == MODE_EDIT))
                {
                         if (modEntry->currRow == 63) modSetPos(DONT_SET_ORDER, modEntry->currRow - 15);
                    else if (modEntry->currRow == 15) modSetPos(DONT_SET_ORDER, 0); // 15-16 would turn into -1, which is "DON'T SET ROW" flag
                    else                              modSetPos(DONT_SET_ORDER, modEntry->currRow - 16);
                }
            }

            if (!input.keyb.repeatKey)
                input.keyb.delayCounter = 0;

            input.keyb.repeatKey = true;
            input.keyb.delayKey  = true;
        }
        break;

        case SDL_SCANCODE_PAGEDOWN:
        {
            if (editor.ui.posEdScreenShown)
            {
                if (modEntry->currOrder != (modEntry->head.orderCount - 1))
                {
                    if ((modEntry->currOrder + (POSED_LIST_SIZE - 1)) <= (modEntry->head.orderCount - 1))
                        modSetPos(modEntry->currOrder + (POSED_LIST_SIZE - 1), DONT_SET_ROW);
                    else
                        modSetPos(modEntry->head.orderCount - 1, DONT_SET_ROW);
                }
            }
            else if (editor.ui.diskOpScreenShown)
            {
                if (editor.diskop.numFiles > DISKOP_LIST_SIZE)
                {
                    editor.diskop.scrollOffset += (DISKOP_LIST_SIZE - 1);
                    if (editor.diskop.scrollOffset > (editor.diskop.numFiles - DISKOP_LIST_SIZE))
                        editor.diskop.scrollOffset =  editor.diskop.numFiles - DISKOP_LIST_SIZE;

                    editor.ui.updateDiskOpFileList = true;
                }
            }
            else
            {
                if ((editor.currMode == MODE_IDLE) || (editor.currMode == MODE_EDIT))
                    modSetPos(DONT_SET_ORDER, modEntry->currRow + 16);
            }

            if (!input.keyb.repeatKey)
                input.keyb.delayCounter = 0;

            input.keyb.repeatKey = true;
            input.keyb.delayKey  = true;
        }
        break;

        case SDL_SCANCODE_HOME:
        {
            if (editor.ui.posEdScreenShown)
            {
                if (modEntry->currOrder > 0)
                    modSetPos(0, DONT_SET_ROW);
            }
            else if (editor.ui.diskOpScreenShown)
            {
                if (editor.diskop.scrollOffset != 0)
                {
                    editor.diskop.scrollOffset = 0;
                    editor.ui.updateDiskOpFileList = true;
                }
            }
            else
            {
                if ((editor.currMode == MODE_IDLE) || (editor.currMode == MODE_EDIT))
                    modSetPos(DONT_SET_ORDER, 0);
            }
        }
        break;

        case SDL_SCANCODE_END:
        {
            if (editor.ui.posEdScreenShown)
            {
                modSetPos(modEntry->head.orderCount - 1, DONT_SET_ROW);
            }
            else if (editor.ui.diskOpScreenShown)
            {
                if (editor.diskop.numFiles > DISKOP_LIST_SIZE)
                {
                    editor.diskop.scrollOffset = editor.diskop.numFiles - DISKOP_LIST_SIZE;
                    editor.ui.updateDiskOpFileList = true;
                }
            }
            else
            {
                if ((editor.currMode == MODE_IDLE) || (editor.currMode == MODE_EDIT))
                    modSetPos(DONT_SET_ORDER, 63);
            }
        }
        break;

        case SDL_SCANCODE_DELETE:
        {
            if (editor.ui.samplerScreenShown)
                samplerSamDelete(NO_SAMPLE_CUT);
            else
                handleEditKeys(keyEntry, EDIT_NORMAL);
        }
        break;

        case SDL_SCANCODE_F12:
        {
            if (input.keyb.leftAltKeyDown)
            {
                editor.ui.terminalShown = true;
            }
            else if (input.keyb.leftCtrlKeyDown)
            {
                editor.timingMode ^= 1;
                if (editor.timingMode == TEMPO_MODE_VBLANK)
                {
                    editor.oldTempo = modEntry->currBPM;
                    modSetTempo(125);
                }
                else
                {
                    modSetTempo(editor.oldTempo);
                }

                editor.ui.updateSongTiming = true;
            }
            else if (input.keyb.shiftKeyDown)
            {
                toggleAmigaPanMode();
            }
            else
            {
                toggleLowPassFilter();
            }
        }
        break;

        case SDL_SCANCODE_RETURN:
        {
            if (editor.ui.askScreenShown)
            {
                editor.ui.answerNo       = false;
                editor.ui.answerYes      = true;
                editor.ui.askScreenShown = false;

                handleAskYes();
            }
            else
            {
                if (input.keyb.shiftKeyDown || input.keyb.leftAltKeyDown || input.keyb.leftCtrlKeyDown)
                {
                    saveUndo();

                    if (input.keyb.leftAltKeyDown && !input.keyb.leftCtrlKeyDown)
                    {
                        if (modEntry->currRow < 63)
                        {
                            for (i = 0; i < AMIGA_VOICES; ++i)
                            {
                                for (j = 62; j >= modEntry->currRow; --j)
                                {
                                    noteSrc = &modEntry->patterns[modEntry->currPattern][(j * AMIGA_VOICES) + i];
                                    modEntry->patterns[modEntry->currPattern][((j + 1) * AMIGA_VOICES) + i] = *noteSrc;
                                }

                                j = ((j + 1) * AMIGA_VOICES) + i;

                                modEntry->patterns[modEntry->currPattern][j].period  = 0;
                                modEntry->patterns[modEntry->currPattern][j].sample  = 0;
                                modEntry->patterns[modEntry->currPattern][j].command = 0;
                                modEntry->patterns[modEntry->currPattern][j].param   = 0;
                            }

                            modEntry->currRow++;

                            updateWindowTitle(MOD_IS_MODIFIED);
                            editor.ui.updatePatternData = true;
                        }
                    }
                    else
                    {
                        if (modEntry->currRow < 63)
                        {
                            for (i = 62; i >= modEntry->currRow; --i)
                            {
                                noteSrc = &modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel];

                                if (input.keyb.leftCtrlKeyDown)
                                {
                                    modEntry->patterns[modEntry->currPattern][((i + 1) * AMIGA_VOICES) + editor.cursor.channel].command = noteSrc->command;
                                    modEntry->patterns[modEntry->currPattern][((i + 1) * AMIGA_VOICES) + editor.cursor.channel].param   = noteSrc->param;
                                }
                                else
                                {
                                    modEntry->patterns[modEntry->currPattern][((i + 1) * AMIGA_VOICES) + editor.cursor.channel] = *noteSrc;
                                }
                            }

                            i = ((i + 1) * AMIGA_VOICES) + editor.cursor.channel;

                            if (!input.keyb.leftCtrlKeyDown)
                            {
                                modEntry->patterns[modEntry->currPattern][i].period = 0;
                                modEntry->patterns[modEntry->currPattern][i].sample = 0;
                            }

                            modEntry->patterns[modEntry->currPattern][i].command = 0;
                            modEntry->patterns[modEntry->currPattern][i].param   = 0;

                            modEntry->currRow++;

                            updateWindowTitle(MOD_IS_MODIFIED);
                            editor.ui.updatePatternData = true;
                        }
                    }
                }
                else
                {
                    editor.stepPlayEnabled   = true;
                    editor.stepPlayBackwards = false;

                    doStopIt();
                    playPattern(modEntry->currRow);
                }
            }
        }
        break;

        // toggle between IDLE and EDIT (IDLE if PLAY)
        case SDL_SCANCODE_SPACE:
        {
            if (editor.currMode == MODE_PLAY)
            {
                modStop();

                editor.currMode = MODE_IDLE;
                pointerSetMode(POINTER_MODE_IDLE, DO_CARRY);
                setStatusMessage(editor.allRightText, DO_CARRY);
            }
            else if ((editor.currMode == MODE_EDIT) || (editor.currMode == MODE_RECORD))
            {
                if (!editor.ui.samplerScreenShown)
                {
                    modStop();

                    editor.currMode = MODE_IDLE;
                    pointerSetMode(POINTER_MODE_IDLE, DO_CARRY);
                    setStatusMessage(editor.allRightText, DO_CARRY);
                }
            }
            else if (!editor.ui.samplerScreenShown)
            {
                modStop();

                editor.currMode = MODE_EDIT;
                pointerSetMode(POINTER_MODE_EDIT, DO_CARRY);
                setStatusMessage(editor.allRightText, DO_CARRY);
            }
        }
        break;

        case SDL_SCANCODE_F1: editor.keyOctave = OCTAVE_LOW;  break;
        case SDL_SCANCODE_F2: editor.keyOctave = OCTAVE_HIGH; break;

        case SDL_SCANCODE_F3:
        {
            if (editor.ui.samplerScreenShown)
            {
                samplerSamDelete(SAMPLE_CUT);
            }
            else
            {
                if (input.keyb.shiftKeyDown)
                {
                    // cut channel and put in buffer
                    saveUndo();

                    noteDst = editor.trackBuffer;
                    for (i = 0; i < MOD_ROWS; ++i)
                    {
                        *noteDst++ = modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel];

                        modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel].period  = 0;
                        modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel].sample  = 0;
                        modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel].command = 0;
                        modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel].param   = 0;
                    }

                    updateWindowTitle(MOD_IS_MODIFIED);
                    editor.ui.updatePatternData = true;
                }
                else if (input.keyb.leftAltKeyDown)
                {
                    // cut pattern and put in buffer
                    saveUndo();

                    memcpy(editor.patternBuffer, modEntry->patterns[modEntry->currPattern],
                        sizeof (note_t) * (AMIGA_VOICES * MOD_ROWS));

                    memset(modEntry->patterns[modEntry->currPattern], 0,
                        sizeof (note_t) * (AMIGA_VOICES * MOD_ROWS));

                    updateWindowTitle(MOD_IS_MODIFIED);
                    editor.ui.updatePatternData = true;
                }
                else if (input.keyb.leftCtrlKeyDown)
                {
                    // cut channel commands and put in buffer
                    saveUndo();

                    noteDst = editor.cmdsBuffer;
                    for (i = 0; i < MOD_ROWS; ++i)
                    {
                        *noteDst++ = modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel];

                        modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel].command = 0;
                        modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel].param   = 0;
                    }

                    updateWindowTitle(MOD_IS_MODIFIED);
                    editor.ui.updatePatternData = true;
                }
            }
        }
        break;

        case SDL_SCANCODE_F4:
        {
            if (editor.ui.samplerScreenShown)
            {
                samplerSamCopy();
            }
            else
            {
                if (input.keyb.shiftKeyDown)
                {
                    // copy channel to buffer

                    noteDst = editor.trackBuffer;
                    for (i = 0; i < MOD_ROWS; ++i)
                        *noteDst++ = modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel];
                }
                else if (input.keyb.leftAltKeyDown)
                {
                    // copy pattern to buffer

                    memcpy(editor.patternBuffer, modEntry->patterns[modEntry->currPattern],
                        sizeof (note_t) * (AMIGA_VOICES * MOD_ROWS));
                }
                else if (input.keyb.leftCtrlKeyDown)
                {
                    // copy channel commands to buffer

                    noteDst = editor.cmdsBuffer;
                    for (i = 0; i < MOD_ROWS; ++i)
                    {
                        noteDst->command = modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel].command;
                        noteDst->param   = modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel].param;

                        noteDst++;
                    }
                }
            }
        }
        break;

        case SDL_SCANCODE_F5:
        {
            if (editor.ui.samplerScreenShown)
            {
                samplerSamPaste();
            }
            else
            {
                if (input.keyb.shiftKeyDown)
                {
                    // paste channel buffer to channel
                    saveUndo();

                    noteSrc = editor.trackBuffer;

                    for (i = 0; i < MOD_ROWS; ++i)
                        modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel] = *noteSrc++;

                    updateWindowTitle(MOD_IS_MODIFIED);
                    editor.ui.updatePatternData = true;
                }
                else if (input.keyb.leftAltKeyDown)
                {
                    // paste pattern buffer to pattern
                    saveUndo();

                    memcpy(modEntry->patterns[modEntry->currPattern],
                        editor.patternBuffer, sizeof (note_t) * (AMIGA_VOICES * MOD_ROWS));

                    updateWindowTitle(MOD_IS_MODIFIED);
                    editor.ui.updatePatternData = true;
                }
                else if (input.keyb.leftCtrlKeyDown)
                {
                    // paste channel commands buffer to channel
                    saveUndo();

                    noteSrc = editor.cmdsBuffer;

                    for (i = 0; i < MOD_ROWS; ++i)
                    {
                        modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel].command = noteSrc->command;
                        modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel].param   = noteSrc->param;

                        noteSrc++;
                    }

                    updateWindowTitle(MOD_IS_MODIFIED);
                    editor.ui.updatePatternData = true;
                }
            }
        }
        break;

        case SDL_SCANCODE_F6:
        {
            if (input.keyb.shiftKeyDown)
            {
                editor.f6Pos = modEntry->currRow;
                displayMsg("POSITION SET");
            }
            else
            {
                if (input.keyb.leftAltKeyDown)
                {
                    editor.playMode = PLAY_MODE_PATTERN;
                    modPlay(modEntry->currPattern, DONT_SET_ORDER, editor.f6Pos);

                    editor.currMode = MODE_PLAY;
                    pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
                    setStatusMessage(editor.allRightText, DO_CARRY);
                }
                else if (input.keyb.leftCtrlKeyDown)
                {
                    if (!editor.ui.samplerScreenShown)
                    {
                        editor.playMode = PLAY_MODE_PATTERN;
                        modPlay(modEntry->currPattern, DONT_SET_ORDER, editor.f6Pos);

                        editor.currMode = MODE_RECORD;
                        pointerSetMode(POINTER_MODE_EDIT, DO_CARRY);
                        setStatusMessage(editor.allRightText, DO_CARRY);
                    }
                }
                else if (input.keyb.leftAmigaKeyDown)
                {
                    editor.playMode = PLAY_MODE_NORMAL;
                    modPlay(DONT_SET_PATTERN, modEntry->currOrder, editor.f6Pos);

                    editor.currMode = MODE_PLAY;
                    pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
                    setStatusMessage(editor.allRightText, DO_CARRY);
                }
                else if ((editor.currMode != MODE_PLAY) && (editor.currMode != MODE_RECORD))
                {
                    modSetPos(DONT_SET_ORDER, editor.f6Pos);
                }
            }
        }
        break;

        case SDL_SCANCODE_F7:
        {
            if (input.keyb.shiftKeyDown)
            {
                editor.f7Pos = modEntry->currRow;
                displayMsg("POSITION SET");
            }
            else
            {
                if (input.keyb.leftAltKeyDown)
                {
                    editor.playMode = PLAY_MODE_PATTERN;
                    modPlay(modEntry->currPattern, DONT_SET_ORDER, editor.f7Pos);

                    editor.currMode = MODE_PLAY;
                    pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
                    setStatusMessage(editor.allRightText, DO_CARRY);
                }
                else if (input.keyb.leftCtrlKeyDown)
                {
                    if (!editor.ui.samplerScreenShown)
                    {
                        editor.playMode = PLAY_MODE_PATTERN;
                        modPlay(modEntry->currPattern, DONT_SET_ORDER, editor.f7Pos);

                        editor.currMode = MODE_RECORD;
                        pointerSetMode(POINTER_MODE_EDIT, DO_CARRY);
                        setStatusMessage(editor.allRightText, DO_CARRY);
                    }
                }
                else if (input.keyb.leftAmigaKeyDown)
                {
                    editor.playMode = PLAY_MODE_NORMAL;
                    modPlay(DONT_SET_PATTERN, modEntry->currOrder, editor.f7Pos);

                    editor.currMode = MODE_PLAY;
                    pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
                    setStatusMessage(editor.allRightText, DO_CARRY);
                }
                else if ((editor.currMode != MODE_PLAY) && (editor.currMode != MODE_RECORD))
                {
                    modSetPos(DONT_SET_ORDER, editor.f7Pos);
                }
            }
        }
        break;

        case SDL_SCANCODE_F8:
        {
            if (input.keyb.shiftKeyDown)
            {
                editor.f8Pos = modEntry->currRow;
                displayMsg("POSITION SET");
            }
            else
            {
                if (input.keyb.leftAltKeyDown)
                {
                    editor.playMode = PLAY_MODE_PATTERN;
                    modPlay(modEntry->currPattern, DONT_SET_ORDER, editor.f8Pos);

                    editor.currMode = MODE_PLAY;
                    pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
                    setStatusMessage(editor.allRightText, DO_CARRY);
                }
                else if (input.keyb.leftCtrlKeyDown)
                {
                    if (!editor.ui.samplerScreenShown)
                    {
                        editor.playMode = PLAY_MODE_PATTERN;
                        modPlay(modEntry->currPattern, DONT_SET_ORDER, editor.f8Pos);

                        editor.currMode = MODE_RECORD;
                        pointerSetMode(POINTER_MODE_EDIT, DO_CARRY);
                        setStatusMessage(editor.allRightText, DO_CARRY);
                    }
                }
                else if (input.keyb.leftAmigaKeyDown)
                {
                    editor.playMode = PLAY_MODE_NORMAL;
                    modPlay(DONT_SET_PATTERN, modEntry->currOrder, editor.f8Pos);

                    editor.currMode = MODE_PLAY;
                    pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
                    setStatusMessage(editor.allRightText, DO_CARRY);
                }
                else if ((editor.currMode != MODE_PLAY) && (editor.currMode != MODE_RECORD))
                {
                    modSetPos(DONT_SET_ORDER, editor.f8Pos);
                }
            }
        }
        break;

        case SDL_SCANCODE_F9:
        {
            if (input.keyb.shiftKeyDown)
            {
                editor.f9Pos = modEntry->currRow;
                displayMsg("POSITION SET");
            }
            else
            {
                if (input.keyb.leftAltKeyDown)
                {
                    editor.playMode = PLAY_MODE_PATTERN;
                    modPlay(modEntry->currPattern, DONT_SET_ORDER, editor.f9Pos);

                    editor.currMode = MODE_PLAY;
                    pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
                    setStatusMessage(editor.allRightText, DO_CARRY);
                }
                else if (input.keyb.leftCtrlKeyDown)
                {
                    if (!editor.ui.samplerScreenShown)
                    {
                        editor.playMode = PLAY_MODE_PATTERN;
                        modPlay(modEntry->currPattern, DONT_SET_ORDER, editor.f9Pos);

                        editor.currMode = MODE_RECORD;
                        pointerSetMode(POINTER_MODE_EDIT, DO_CARRY);
                        setStatusMessage(editor.allRightText, DO_CARRY);
                    }
                }
                else if (input.keyb.leftAmigaKeyDown)
                {
                    editor.playMode = PLAY_MODE_NORMAL;
                    modPlay(DONT_SET_PATTERN, modEntry->currOrder, editor.f9Pos);

                    editor.currMode = MODE_PLAY;
                    pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
                    setStatusMessage(editor.allRightText, DO_CARRY);
                }
                else if ((editor.currMode != MODE_PLAY) && (editor.currMode != MODE_RECORD))
                {
                    modSetPos(DONT_SET_ORDER, editor.f9Pos);
                }
            }
        }
        break;

        case SDL_SCANCODE_F10:
        {
            if (input.keyb.shiftKeyDown)
            {
                editor.f10Pos = modEntry->currRow;
                displayMsg("POSITION SET");
            }
            else
            {
                if (input.keyb.leftAltKeyDown)
                {
                    editor.playMode = PLAY_MODE_PATTERN;
                    modPlay(modEntry->currPattern, DONT_SET_ORDER, editor.f10Pos);

                    editor.currMode = MODE_PLAY;
                    pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
                    setStatusMessage(editor.allRightText, DO_CARRY);
                }
                else if (input.keyb.leftCtrlKeyDown)
                {
                    if (!editor.ui.samplerScreenShown)
                    {
                        editor.playMode = PLAY_MODE_PATTERN;
                        modPlay(modEntry->currPattern, DONT_SET_ORDER, editor.f10Pos);

                        editor.currMode = MODE_RECORD;
                        pointerSetMode(POINTER_MODE_EDIT, DO_CARRY);
                        setStatusMessage(editor.allRightText, DO_CARRY);
                    }
                }
                else if (input.keyb.leftAmigaKeyDown)
                {
                    editor.playMode = PLAY_MODE_NORMAL;
                    modPlay(DONT_SET_PATTERN, modEntry->currOrder, editor.f10Pos);

                    editor.currMode = MODE_PLAY;
                    pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
                    setStatusMessage(editor.allRightText, DO_CARRY);
                }
                else if ((editor.currMode != MODE_PLAY) && (editor.currMode != MODE_RECORD))
                {
                    modSetPos(DONT_SET_ORDER, editor.f10Pos);
                }
            }
        }
        break;

        case SDL_SCANCODE_F11:
        {
            if (input.keyb.leftAltKeyDown)
            {
                editor.ui.realVuMeters ^= 1;
                if (editor.ui.realVuMeters)
                    displayMsg("VU-METERS: REAL");
                else
                    displayMsg("VU-METERS: FAKE");
            }
        }
        break;

        case SDL_SCANCODE_TAB:
        {
            if (input.keyb.shiftKeyDown)
                movePatCurPrevCh();
            else
                movePatCurNextCh();
        }
        break;

        case SDL_SCANCODE_0:
        {
            if (input.keyb.leftCtrlKeyDown)
            {
                editor.editMoveAdd = 0;
                displayMsg("EDITSKIP = 0");
                editor.ui.updateTrackerFlags = true;
            }
            else if (input.keyb.shiftKeyDown)
            {
                noteSrc = &modEntry->patterns[modEntry->currPattern][(modEntry->currRow * AMIGA_VOICES) + editor.cursor.channel];
                editor.effectMacros[9] = (noteSrc->command << 8) | noteSrc->param;

                displayMsg("COMMAND STORED!");
            }
            else
            {
                handleEditKeys(keyEntry, EDIT_NORMAL);
            }
        }
        break;

        case SDL_SCANCODE_1:
        {
            if (input.keyb.leftAmigaKeyDown)
            {
                trackNoteUp(TRANSPOSE_ALL, 0, MOD_ROWS - 1);
            }
            else if (input.keyb.leftCtrlKeyDown)
            {
                editor.editMoveAdd = 1;
                displayMsg("EDITSKIP = 1");
                editor.ui.updateTrackerFlags = true;
            }
            else if (input.keyb.shiftKeyDown)
            {
                noteSrc = &modEntry->patterns[modEntry->currPattern][(modEntry->currRow * AMIGA_VOICES) + editor.cursor.channel];
                editor.effectMacros[0] = (noteSrc->command << 8) | noteSrc->param;

                displayMsg("COMMAND STORED!");
            }
            else
            {
                handleEditKeys(keyEntry, EDIT_NORMAL);
            }
        }
        break;

        case SDL_SCANCODE_2:
        {
            if (input.keyb.leftAmigaKeyDown)
            {
                pattNoteUp(TRANSPOSE_ALL);
            }
            else if (input.keyb.leftCtrlKeyDown)
            {
                editor.editMoveAdd = 2;
                displayMsg("EDITSKIP = 2");
                editor.ui.updateTrackerFlags = true;
            }
            else if (input.keyb.shiftKeyDown)
            {
                noteSrc = &modEntry->patterns[modEntry->currPattern][(modEntry->currRow * AMIGA_VOICES) + editor.cursor.channel];
                editor.effectMacros[1] = (noteSrc->command << 8) | noteSrc->param;

                displayMsg("COMMAND STORED!");
            }
            else
            {
                handleEditKeys(keyEntry, EDIT_NORMAL);
            }
        }
        break;

        case SDL_SCANCODE_3:
        {
            if (input.keyb.leftAmigaKeyDown)
            {
                trackNoteUp(TRANSPOSE_ALL, 0, MOD_ROWS - 1);
            }
            else if (input.keyb.leftCtrlKeyDown)
            {
                editor.editMoveAdd = 3;
                displayMsg("EDITSKIP = 3");
                editor.ui.updateTrackerFlags = true;
            }
            else if (input.keyb.shiftKeyDown)
            {
                noteSrc = &modEntry->patterns[modEntry->currPattern][(modEntry->currRow * AMIGA_VOICES) + editor.cursor.channel];
                editor.effectMacros[2] = (noteSrc->command << 8) | noteSrc->param;

                displayMsg("COMMAND STORED!");
            }
            else
            {
                handleEditKeys(keyEntry, EDIT_NORMAL);
            }
        }
        break;

        case SDL_SCANCODE_4:
        {
            if (input.keyb.leftAmigaKeyDown)
            {
                pattNoteUp(TRANSPOSE_ALL);
            }
            else if (input.keyb.leftCtrlKeyDown)
            {
                editor.editMoveAdd = 4;
                displayMsg("EDITSKIP = 4");
                editor.ui.updateTrackerFlags = true;
            }
            else if (input.keyb.shiftKeyDown)
            {
                noteSrc = &modEntry->patterns[modEntry->currPattern][(modEntry->currRow * AMIGA_VOICES) + editor.cursor.channel];
                editor.effectMacros[3] = (noteSrc->command << 8) | noteSrc->param;

                displayMsg("COMMAND STORED!");
            }
            else
            {
                handleEditKeys(keyEntry, EDIT_NORMAL);
            }
        }
        break;

        case SDL_SCANCODE_5:
        {
            if (input.keyb.leftCtrlKeyDown)
            {
                editor.editMoveAdd = 5;
                displayMsg("EDITSKIP = 5");
                editor.ui.updateTrackerFlags = true;
            }
            else if (input.keyb.shiftKeyDown)
            {
                noteSrc = &modEntry->patterns[modEntry->currPattern][(modEntry->currRow * AMIGA_VOICES) + editor.cursor.channel];
                editor.effectMacros[4] = (noteSrc->command << 8) | noteSrc->param;

                displayMsg("COMMAND STORED!");
            }
            else
            {
                handleEditKeys(keyEntry, EDIT_NORMAL);
            }
        }
        break;

        case SDL_SCANCODE_6:
        {
            if (input.keyb.leftCtrlKeyDown)
            {
                editor.editMoveAdd = 6;
                displayMsg("EDITSKIP = 6");
                editor.ui.updateTrackerFlags = true;
            }
            else if (input.keyb.shiftKeyDown)
            {
                noteSrc = &modEntry->patterns[modEntry->currPattern][(modEntry->currRow * AMIGA_VOICES) + editor.cursor.channel];
                editor.effectMacros[5] = (noteSrc->command << 8) | noteSrc->param;

                displayMsg("COMMAND STORED!");
            }
            else
            {
                handleEditKeys(keyEntry, EDIT_NORMAL);
            }
        }
        break;

        case SDL_SCANCODE_7:
        {
            if (input.keyb.leftCtrlKeyDown)
            {
                editor.editMoveAdd = 7;
                displayMsg("EDITSKIP = 7");
                editor.ui.updateTrackerFlags = true;
            }
            else if (input.keyb.shiftKeyDown)
            {
                noteSrc = &modEntry->patterns[modEntry->currPattern][(modEntry->currRow * AMIGA_VOICES) + editor.cursor.channel];
                editor.effectMacros[6] = (noteSrc->command << 8) | noteSrc->param;

                displayMsg("COMMAND STORED!");
            }
            else
            {
                handleEditKeys(keyEntry, EDIT_NORMAL);
            }
        }
        break;

        case SDL_SCANCODE_8:
        {
            if (input.keyb.leftCtrlKeyDown)
            {
                editor.editMoveAdd = 8;
                displayMsg("EDITSKIP = 8");
                editor.ui.updateTrackerFlags = true;
            }
            else if (input.keyb.shiftKeyDown)
            {
                noteSrc = &modEntry->patterns[modEntry->currPattern][(modEntry->currRow * AMIGA_VOICES) + editor.cursor.channel];
                editor.effectMacros[7] = (noteSrc->command << 8) | noteSrc->param;

                displayMsg("COMMAND STORED!");
            }
            else
            {
                handleEditKeys(keyEntry, EDIT_NORMAL);
            }
        }
        break;

        case SDL_SCANCODE_9:
        {
            if (input.keyb.leftCtrlKeyDown)
            {
                editor.editMoveAdd = 9;
                displayMsg("EDITSKIP = 9");
                editor.ui.updateTrackerFlags = true;
            }
            else if (input.keyb.shiftKeyDown)
            {
                noteSrc = &modEntry->patterns[modEntry->currPattern][(modEntry->currRow * AMIGA_VOICES) + editor.cursor.channel];
                editor.effectMacros[8] = (noteSrc->command << 8) | noteSrc->param;

                displayMsg("COMMAND STORED!");
            }
            else
            {
                handleEditKeys(keyEntry, EDIT_NORMAL);
            }
        }
        break;

        case SDL_SCANCODE_KP_0:
        {
            editor.sampleZero = true;
            updateCurrSample();
        }
        break;

        case SDL_SCANCODE_KP_1:
        {
            editor.sampleZero = false;
            editor.currSample = editor.keypadSampleOffset + 12;

            updateCurrSample();

            if (input.keyb.leftAltKeyDown && (editor.pNoteFlag > 0))
            {
                editor.ui.changingDrumPadNote = true;

                setStatusMessage("SELECT NOTE", NO_CARRY);
                pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);

                break;
            }

            if (editor.pNoteFlag > 0)
                handleEditKeys(keyEntry, EDIT_SPECIAL);
        }
        break;

        case SDL_SCANCODE_KP_2:
        {
            editor.sampleZero = false;
            editor.currSample = editor.keypadSampleOffset + 13;

            updateCurrSample();

            if (input.keyb.leftAltKeyDown && (editor.pNoteFlag > 0))
            {
                editor.ui.changingDrumPadNote = true;

                setStatusMessage("SELECT NOTE", NO_CARRY);
                pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);

                break;
            }

            if (editor.pNoteFlag > 0)
                handleEditKeys(keyEntry, EDIT_SPECIAL);
        }
        break;

        case SDL_SCANCODE_KP_3:
        {
            editor.sampleZero = false;
            editor.currSample = editor.keypadSampleOffset + 14;

            updateCurrSample();

            if (input.keyb.leftAltKeyDown && (editor.pNoteFlag > 0))
            {
                editor.ui.changingDrumPadNote = true;

                setStatusMessage("SELECT NOTE", NO_CARRY);
                pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);

                break;
            }

            if (editor.pNoteFlag > 0)
                handleEditKeys(keyEntry, EDIT_SPECIAL);
        }
        break;

        case SDL_SCANCODE_KP_4:
        {
            editor.sampleZero = false;
            editor.currSample = editor.keypadSampleOffset + 8;

            updateCurrSample();

            if (input.keyb.leftAltKeyDown && (editor.pNoteFlag > 0))
            {
                editor.ui.changingDrumPadNote = true;

                setStatusMessage("SELECT NOTE", NO_CARRY);
                pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);

                break;
            }

            if (editor.pNoteFlag > 0)
                handleEditKeys(keyEntry, EDIT_SPECIAL);
        }
        break;

        case SDL_SCANCODE_KP_5:
        {
            editor.sampleZero = false;
            editor.currSample = editor.keypadSampleOffset + 9;

            updateCurrSample();

            if (input.keyb.leftAltKeyDown && (editor.pNoteFlag > 0))
            {
                editor.ui.changingDrumPadNote = true;

                setStatusMessage("SELECT NOTE", NO_CARRY);
                pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);

                break;
            }

            if (editor.pNoteFlag > 0)
                handleEditKeys(keyEntry, EDIT_SPECIAL);
        }
        break;

        case SDL_SCANCODE_KP_6:
        {
            editor.sampleZero = false;
            editor.currSample = editor.keypadSampleOffset + 10;

            updateCurrSample();

            if (input.keyb.leftAltKeyDown && (editor.pNoteFlag > 0))
            {
                editor.ui.changingDrumPadNote = true;

                setStatusMessage("SELECT NOTE", NO_CARRY);
                pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);

                break;
            }

            if (editor.pNoteFlag > 0)
                handleEditKeys(keyEntry, EDIT_SPECIAL);
        }
        break;

        case SDL_SCANCODE_KP_7:
        {
            editor.sampleZero = false;
            editor.currSample = editor.keypadSampleOffset + 4;

            updateCurrSample();

            if (input.keyb.leftAltKeyDown && (editor.pNoteFlag > 0))
            {
                editor.ui.changingDrumPadNote = true;

                setStatusMessage("SELECT NOTE", NO_CARRY);
                pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);

                break;
            }

            if (editor.pNoteFlag > 0)
                handleEditKeys(keyEntry, EDIT_SPECIAL);
        }
        break;

        case SDL_SCANCODE_KP_8:
        {
            editor.sampleZero = false;
            editor.currSample = editor.keypadSampleOffset + 5;

            updateCurrSample();

            if (input.keyb.leftAltKeyDown && (editor.pNoteFlag > 0))
            {
                editor.ui.changingDrumPadNote = true;

                setStatusMessage("SELECT NOTE", NO_CARRY);
                pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);

                break;
            }

            if (editor.pNoteFlag > 0)
                handleEditKeys(keyEntry, EDIT_SPECIAL);
        }
        break;

        case SDL_SCANCODE_KP_9:
        {
            editor.sampleZero = false;
            editor.currSample = editor.keypadSampleOffset + 6;

            updateCurrSample();

            if (input.keyb.leftAltKeyDown && (editor.pNoteFlag > 0))
            {
                editor.ui.changingDrumPadNote = true;

                setStatusMessage("SELECT NOTE", NO_CARRY);
                pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);

                break;
            }

            if (editor.pNoteFlag > 0)
                handleEditKeys(keyEntry, EDIT_SPECIAL);
        }
        break;

        case SDL_SCANCODE_KP_ENTER:
        {
            if (editor.ui.askScreenShown)
            {
                editor.ui.answerNo       = false;
                editor.ui.answerYes      = true;
                editor.ui.askScreenShown = false;

                handleAskYes();
            }
            else
            {
                editor.sampleZero = false;

                editor.currSample++;

                if (editor.currSample >= 0x10)
                {
                    editor.keypadSampleOffset = 0x00;

                    editor.currSample -= 0x10;
                    if (editor.currSample < 0x01)
                        editor.currSample = 0x01;
                }
                else
                {
                    editor.currSample     += 0x10;
                    editor.keypadSampleOffset = 0x10;
                }

                editor.currSample--;

                updateCurrSample();

                if (input.keyb.leftAltKeyDown && (editor.pNoteFlag > 0))
                {
                    editor.ui.changingDrumPadNote = true;

                    setStatusMessage("SELECT NOTE", NO_CARRY);
                    pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);

                    break;
                }

                if (editor.pNoteFlag > 0)
                    handleEditKeys(keyEntry, EDIT_SPECIAL);
            }
        }
        break;

        case SDL_SCANCODE_KP_PLUS:
        {
            editor.sampleZero = false;

            // the Amiga numpad has one more key, so we need to use this key for two sample numbers...
            if (editor.keypadToggle8CFlag)
                editor.currSample = editor.keypadSampleOffset + (0x0C - 1);
            else
                editor.currSample = editor.keypadSampleOffset + (0x08 - 1);

            updateCurrSample();

            if (input.keyb.leftAltKeyDown && (editor.pNoteFlag > 0))
                displayErrorMsg("INVALID PAD KEY !");

            editor.keypadToggle8CFlag ^= 1;
        }
        break;

        case SDL_SCANCODE_KP_MINUS:
        {
            editor.sampleZero = false;
            editor.currSample = editor.keypadSampleOffset + 3;

            updateCurrSample();

            if (input.keyb.leftAltKeyDown && (editor.pNoteFlag > 0))
            {
                editor.ui.changingDrumPadNote = true;

                setStatusMessage("SELECT NOTE", NO_CARRY);
                pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);

                break;
            }

            if (editor.pNoteFlag > 0)
                handleEditKeys(keyEntry, EDIT_SPECIAL);
        }
        break;

        case SDL_SCANCODE_KP_MULTIPLY:
        {
            editor.sampleZero = false;
            editor.currSample = editor.keypadSampleOffset + 2;

            updateCurrSample();

            if (input.keyb.leftAltKeyDown && (editor.pNoteFlag > 0))
            {
                editor.ui.changingDrumPadNote = true;

                setStatusMessage("SELECT NOTE", NO_CARRY);
                pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);

                break;
            }

            if (editor.pNoteFlag > 0)
                handleEditKeys(keyEntry, EDIT_SPECIAL);
        }
        break;

        case SDL_SCANCODE_KP_DIVIDE:
        {
            editor.sampleZero = false;
            editor.currSample = editor.keypadSampleOffset + 1;

            updateCurrSample();

            if (input.keyb.leftAltKeyDown && (editor.pNoteFlag > 0))
            {
                editor.ui.changingDrumPadNote = true;

                setStatusMessage("SELECT NOTE", NO_CARRY);
                pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);

                break;
            }

            if (editor.pNoteFlag > 0)
                handleEditKeys(keyEntry, EDIT_SPECIAL);
        }
        break;

        case SDL_SCANCODE_NUMLOCKCLEAR:
        {
            editor.sampleZero = false;
            editor.currSample = editor.keypadSampleOffset + 0;

            updateCurrSample();

            if (input.keyb.leftAltKeyDown && (editor.pNoteFlag > 0))
            {
                editor.ui.changingDrumPadNote = true;

                setStatusMessage("SELECT NOTE", NO_CARRY);
                pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);

                break;
            }

            if (editor.pNoteFlag > 0)
                handleEditKeys(keyEntry, EDIT_SPECIAL);
        }
        break;

        case SDL_SCANCODE_KP_PERIOD:
        {
            editor.ui.askScreenShown = true;
            editor.ui.askScreenType  = ASK_KILL_SAMPLE;

            pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
            setStatusMessage("KILL SAMPLE ?", NO_CARRY);
            renderAskDialog();
        }
        break;

        case SDL_SCANCODE_DOWN:
        {
            input.keyb.delayKey  = false;
            input.keyb.repeatKey = false;

            if (editor.ui.diskOpScreenShown)
            {
                if (editor.diskop.numFiles > DISKOP_LIST_SIZE)
                {
                    editor.diskop.scrollOffset++;

                    if (input.mouse.rightButtonPressed) // PT quirk: right mouse button speeds up even on keyb UP/DOWN
                        editor.diskop.scrollOffset += 3;

                    if (editor.diskop.scrollOffset > (editor.diskop.numFiles - DISKOP_LIST_SIZE))
                        editor.diskop.scrollOffset =  editor.diskop.numFiles - DISKOP_LIST_SIZE;

                    editor.ui.updateDiskOpFileList = true;
                }

                if (!input.keyb.repeatKey)
                    input.keyb.delayCounter = 0;

                input.keyb.repeatKey = true;
                input.keyb.delayKey  = false;
            }
            else if (editor.ui.posEdScreenShown)
            {
                if (modEntry->currOrder != (modEntry->head.orderCount - 1))
                {
                    if (++modEntry->currOrder > (modEntry->head.orderCount - 1))
                          modEntry->currOrder =  modEntry->head.orderCount - 1;

                    modSetPos(modEntry->currOrder, DONT_SET_ROW);
                    editor.ui.updatePosEd = true;
                }

                if (!input.keyb.repeatKey)
                    input.keyb.delayCounter = 0;

                input.keyb.repeatKey = true;
                input.keyb.delayKey  = true;
            }
            else if (!editor.ui.samplerScreenShown)
            {
                if ((editor.currMode != MODE_PLAY) && (editor.currMode != MODE_RECORD))
                    modSetPos(DONT_SET_ORDER, (modEntry->currRow + 1) & 0x3F);

                input.keyb.repeatKey = true;
            }
        }
        break;

        case SDL_SCANCODE_UP:
        {
            input.keyb.delayKey  = false;
            input.keyb.repeatKey = false;

            if (editor.ui.diskOpScreenShown)
            {
                editor.diskop.scrollOffset--;

                if (input.mouse.rightButtonPressed) // PT quirk: right mouse button speeds up even on keyb UP/DOWN
                    editor.diskop.scrollOffset -= 3;

                if (editor.diskop.scrollOffset < 0)
                    editor.diskop.scrollOffset = 0;

                editor.ui.updateDiskOpFileList = true;

                if (!input.keyb.repeatKey)
                    input.keyb.delayCounter = 0;

                input.keyb.repeatKey = true;
                input.keyb.delayKey  = false;
            }
            else if (editor.ui.posEdScreenShown)
            {
                if (modEntry->currOrder > 0)
                {
                    modSetPos(modEntry->currOrder - 1, DONT_SET_ROW);
                    editor.ui.updatePosEd = true;
                }

                if (!input.keyb.repeatKey)
                    input.keyb.delayCounter = 0;

                input.keyb.repeatKey = true;
                input.keyb.delayKey  = true;
            }
            else if (!editor.ui.samplerScreenShown)
            {
                if ((editor.currMode != MODE_PLAY) && (editor.currMode != MODE_RECORD))
                    modSetPos(DONT_SET_ORDER, (modEntry->currRow - 1) & 0x3F);

                input.keyb.repeatKey = true;
            }
        }
        break;

        case SDL_SCANCODE_LEFT:
        {
            input.keyb.delayKey  = false;
            input.keyb.repeatKey = false;

            if (input.keyb.leftCtrlKeyDown)
            {
                sampleDownButton();

                if (editor.repeatKeyFlag)
                {
                    input.keyb.delayKey  = true;
                    input.keyb.repeatKey = true;
                }
            }
            else if (input.keyb.shiftKeyDown)
            {
                if (modEntry->currOrder > 0)
                {
                    modSetPos(modEntry->currOrder - 1, DONT_SET_ROW);

                    if (editor.repeatKeyFlag)
                    {
                        input.keyb.delayKey  = true;
                        input.keyb.repeatKey = true;
                    }
                }
            }
            else if (input.keyb.leftAltKeyDown)
            {
                decPatt();

                if (editor.repeatKeyFlag)
                {
                    input.keyb.delayKey  = true;
                    input.keyb.repeatKey = true;
                }
            }
            else
            {
                movePatCurLeft();
                input.keyb.repeatKey = true;
            }
        }
        break;

        case SDL_SCANCODE_RIGHT:
        {
            input.keyb.delayKey  = false;
            input.keyb.repeatKey = false;

            if (input.keyb.leftCtrlKeyDown)
            {
                sampleUpButton();

                if (editor.repeatKeyFlag)
                {
                    input.keyb.delayKey  = true;
                    input.keyb.repeatKey = true;
                }
            }
            else if (input.keyb.shiftKeyDown)
            {
                if (modEntry->currOrder < 126)
                {
                    modSetPos(modEntry->currOrder + 1, DONT_SET_ROW);

                    if (editor.repeatKeyFlag)
                    {
                        input.keyb.delayKey  = true;
                        input.keyb.repeatKey = true;
                    }
                }
            }
            else if (input.keyb.leftAltKeyDown)
            {
                incPatt();

                if (editor.repeatKeyFlag)
                {
                    input.keyb.delayKey  = true;
                    input.keyb.repeatKey = true;
                }
            }
            else
            {
                movePatCurRight();
                input.keyb.repeatKey = true;
            }
        }
        break;

        case SDL_SCANCODE_A:
        {
            if (input.keyb.leftAmigaKeyDown)
            {
                trackOctaUp(TRANSPOSE_ALL, 0, MOD_ROWS - 1);
            }
            else if (input.keyb.leftCtrlKeyDown)
            {
                if (editor.ui.samplerScreenShown)
                {
                    samplerRangeAll();
                }
                else
                {
                    if (input.keyb.shiftKeyDown)
                    {
                        editor.muted[0] = true;
                        editor.muted[1] = true;
                        editor.muted[2] = true;
                        editor.muted[3] = true;

                        editor.muted[editor.cursor.channel] = false;
                        renderMuteButtons();

                        break;
                    }

                    editor.muted[editor.cursor.channel] ^= 1;
                    renderMuteButtons();
                }
            }
            else
            {
                handleEditKeys(keyEntry, EDIT_NORMAL);
            }
        }
        break;

        case SDL_SCANCODE_B:
        {
            if (input.keyb.leftCtrlKeyDown)
            {
                // CTRL+B doesn't change the status message back, so do this:
                if (editor.ui.introScreenShown)
                {
                    editor.ui.introScreenShown = false;
                    setStatusMessage(editor.allRightText, DO_CARRY);
                }

                if (editor.blockMarkFlag)
                {
                    editor.blockMarkFlag = false;
                }
                else
                {
                    editor.blockMarkFlag = true;
                    editor.blockFromPos  = modEntry->currRow;
                    editor.blockToPos    = modEntry->currRow;
                }

                editor.ui.updateStatusText = true;
            }
            else if (input.keyb.leftAltKeyDown)
            {
                s = &modEntry->samples[editor.currSample];

                if (s->length == 0)
                {
                    displayErrorMsg("SAMPLE IS EMPTY");
                    break;
                }

                boostSample(editor.currSample, true);

                if (editor.ui.samplerScreenShown)
                    displaySample();
            }
            else
            {
                handleEditKeys(keyEntry, EDIT_NORMAL);
            }
        }
        break;

        case SDL_SCANCODE_C:
        {
            if (input.keyb.leftAmigaKeyDown)
            {
                trackOctaDown(TRANSPOSE_ALL, 0, MOD_ROWS - 1);
            }
            else if (input.keyb.leftCtrlKeyDown)
            {
                if (editor.ui.samplerScreenShown)
                {
                    samplerSamCopy();
                    return;
                }

                if (!editor.blockMarkFlag)
                {
                    displayErrorMsg("NO BLOCK MARKED !");
                    return;
                }

                editor.blockMarkFlag   = false;
                editor.blockBufferFlag = true;

                for (i = 0; i < MOD_ROWS; ++i)
                    editor.blockBuffer[i] = modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel];

                if (editor.blockFromPos > editor.blockToPos)
                {
                    editor.buffFromPos = editor.blockToPos;
                    editor.buffToPos   = editor.blockFromPos;
                }
                else
                {
                    editor.buffFromPos = editor.blockFromPos;
                    editor.buffToPos   = editor.blockToPos;
                }

                setStatusMessage(editor.allRightText, DO_CARRY);
            }
            else
            {
                if (input.keyb.leftAltKeyDown)
                {
                    editor.muted[2] ^= 1; // toggle channel 3
                    renderMuteButtons();
                }
                else
                {
                    handleEditKeys(keyEntry, EDIT_NORMAL);
                }
            }
        }
        break;

        case SDL_SCANCODE_D:
        {
            if (input.keyb.leftAmigaKeyDown)
            {
                trackOctaUp(TRANSPOSE_ALL, 0, MOD_ROWS - 1);
            }
            else if (input.keyb.leftCtrlKeyDown)
            {
                saveUndo();
            }
            else
            {
                if (input.keyb.leftAltKeyDown)
                {
                    if (!editor.ui.posEdScreenShown)
                    {
                        editor.blockMarkFlag = false;

                        editor.ui.diskOpScreenShown ^= 1;
                        if (!editor.ui.diskOpScreenShown)
                        {
                            pointerSetPreviousMode();
                            setPrevStatusMessage();

                            displayMainScreen();
                        }
                        else
                        {
                            editor.ui.diskOpScreenShown = true;
                            renderDiskOpScreen();
                        }
                    }
                }
                else
                {
                    handleEditKeys(keyEntry, EDIT_NORMAL);
                }
            }
        }
        break;

        case SDL_SCANCODE_E:
        {
            if (input.keyb.leftAmigaKeyDown)
            {
                trackNoteDown(TRANSPOSE_ALL, 0, MOD_ROWS - 1);
            }
            else if (input.keyb.leftAltKeyDown)
            {
                if (!editor.ui.diskOpScreenShown && !editor.ui.posEdScreenShown)
                {
                    if (editor.ui.editOpScreenShown)
                        editor.ui.editOpScreen = (editor.ui.editOpScreen + 1) % 3;
                    else
                        editor.ui.editOpScreenShown = true;

                    renderEditOpScreen();
                }
            }
            else if (input.keyb.leftCtrlKeyDown)
            {
                saveUndo();

                j = modEntry->currRow + 1;

                while (j < MOD_ROWS)
                {
                    for (i = 62; i >= j; i--)
                    {
                        noteSrc = &modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel];
                        modEntry->patterns[modEntry->currPattern][((i + 1) * AMIGA_VOICES) + editor.cursor.channel] = *noteSrc;
                    }

                    modEntry->patterns[modEntry->currPattern][((i + 1) * AMIGA_VOICES) + editor.cursor.channel].period  = 0;
                    modEntry->patterns[modEntry->currPattern][((i + 1) * AMIGA_VOICES) + editor.cursor.channel].sample  = 0;
                    modEntry->patterns[modEntry->currPattern][((i + 1) * AMIGA_VOICES) + editor.cursor.channel].command = 0;
                    modEntry->patterns[modEntry->currPattern][((i + 1) * AMIGA_VOICES) + editor.cursor.channel].param   = 0;

                   j += 2;
                }

                updateWindowTitle(MOD_IS_MODIFIED);
                editor.ui.updatePatternData = true;
            }
            else
            {
                handleEditKeys(keyEntry, EDIT_NORMAL);
            }
        }
        break;

        case SDL_SCANCODE_F:
        {
            if (input.keyb.leftAmigaKeyDown)
            {
                pattOctaUp(TRANSPOSE_ALL);
            }
            else if (input.keyb.leftCtrlKeyDown)
            {
                toggleLEDFilter();

                if (editor.useLEDFilter)
                    displayMsg("LED FILTER ON");
                else
                    displayMsg("LED FILTER OFF");
            }
            else if (input.keyb.leftAltKeyDown)
            {
                s = &modEntry->samples[editor.currSample];

                if (s->length == 0)
                {
                    displayErrorMsg("SAMPLE IS EMPTY");
                    break;
                }

                filterSample(editor.currSample, true);

                if (editor.ui.samplerScreenShown)
                    displaySample();
            }
            else
            {
                handleEditKeys(keyEntry, EDIT_NORMAL);
            }
        }
        break;

        case SDL_SCANCODE_G:
        {
            if (input.keyb.leftCtrlKeyDown)
            {
                editor.ui.askScreenShown = true;
                editor.ui.askScreenType  = ASK_BOOST_ALL_SAMPLES;

                pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
                setStatusMessage("BOOST ALL SAMPLES", NO_CARRY);
                renderAskDialog();
            }
            else
            {
                handleEditKeys(keyEntry, EDIT_NORMAL);
            }
        }
        break;

        case SDL_SCANCODE_H:
        {
            if (input.keyb.leftCtrlKeyDown)
            {
                if (!editor.blockMarkFlag)
                {
                    displayErrorMsg("NO BLOCK MARKED !");
                    return;
                }

                trackNoteUp(TRANSPOSE_ALL, editor.blockFromPos, editor.blockToPos);
            }
            else
            {
                handleEditKeys(keyEntry, EDIT_NORMAL);
            }
        }
        break;

        case SDL_SCANCODE_I:
        {
            if (input.keyb.leftCtrlKeyDown)
            {
                if (!editor.blockBufferFlag)
                {
                    displayErrorMsg("BUFFER IS EMPTY !");
                    return;
                }

                if (modEntry->currRow < 63)
                {
                    for (i = 0; i <= (editor.buffToPos - editor.buffFromPos); ++i)
                    {
                        for (j = 62; j >= modEntry->currRow; --j)
                        {
                            noteSrc = &modEntry->patterns[modEntry->currPattern][(j * AMIGA_VOICES) + editor.cursor.channel];
                            modEntry->patterns[modEntry->currPattern][((j + 1) * AMIGA_VOICES) + editor.cursor.channel] = *noteSrc;
                        }
                    }
                }

                saveUndo();

                for (i = 0; i <= (editor.buffToPos - editor.buffFromPos); ++i)
                {
                    if ((modEntry->currRow + i) > 63)
                        break;

                    modEntry->patterns[modEntry->currPattern][((modEntry->currRow + i) * AMIGA_VOICES) + editor.cursor.channel]
                        = editor.blockBuffer[editor.buffFromPos + i];
                }

                if (!input.keyb.shiftKeyDown)
                {
                    modEntry->currRow += (i & 0xFF);
                    if (modEntry->currRow > 63)
                        modEntry->currRow = 0;
                }

                updateWindowTitle(MOD_IS_MODIFIED);
                editor.ui.updatePatternData = true;
            }
            else if (input.keyb.leftAltKeyDown)
            {
                editor.autoInsFlag ^= 1;
                editor.ui.updateTrackerFlags = true;
            }
            else
            {
                handleEditKeys(keyEntry, EDIT_NORMAL);
            }
        }
        break;

        case SDL_SCANCODE_J:
        {
            if (input.keyb.leftCtrlKeyDown)
            {
                if (!editor.blockBufferFlag)
                {
                    displayErrorMsg("BUFFER IS EMPTY !");
                    return;
                }

                saveUndo();

                noteSrc = &editor.blockBuffer[editor.buffFromPos];
                for (i = 0; i <= (editor.buffToPos - editor.buffFromPos); ++i)
                {
                    if ((modEntry->currRow + i) > 63)
                        break;

                    noteDst = &modEntry->patterns[modEntry->currPattern][((modEntry->currRow + i) * AMIGA_VOICES) + editor.cursor.channel];

                    if (noteSrc->period || noteSrc->sample)
                    {
                        *noteDst = *noteSrc;
                    }
                    else
                    {
                        noteDst->command = noteSrc->command;
                        noteDst->param   = noteSrc->param;
                    }

                    noteSrc++;
                }

                if (!input.keyb.shiftKeyDown)
                {
                    modEntry->currRow += (i & 0xFF);
                    if (modEntry->currRow > 63)
                        modEntry->currRow = 0;
                }

                updateWindowTitle(MOD_IS_MODIFIED);
                editor.ui.updatePatternData = true;
            }
            else
            {
                handleEditKeys(keyEntry, EDIT_NORMAL);
            }
        }
        break;

        case SDL_SCANCODE_K:
        {
            if (input.keyb.leftAltKeyDown)
            {
                for (i = 0; i < MOD_ROWS; ++i)
                {
                    noteSrc = &modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel];

                    if (noteSrc->sample == (editor.currSample + 1))
                    {
                        noteSrc->period  = 0;
                        noteSrc->sample  = 0;
                        noteSrc->command = 0;
                        noteSrc->param   = 0;
                    }
                }

                updateWindowTitle(MOD_IS_MODIFIED);
                editor.ui.updatePatternData = true;
            }
            else if (input.keyb.leftCtrlKeyDown)
            {
                saveUndo();

                i = modEntry->currRow;
                if (input.keyb.shiftKeyDown)
                {
                    // kill to start
                    while (i >= 0)
                    {
                        modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel].period  = 0;
                        modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel].sample  = 0;
                        modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel].command = 0;
                        modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel].param   = 0;

                        i--;
                    }
                }
                else
                {
                    // kill to end
                    while (i < MOD_ROWS)
                    {
                        modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel].period  = 0;
                        modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel].sample  = 0;
                        modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel].command = 0;
                        modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel].param   = 0;

                        i++;
                    }
                }

                updateWindowTitle(MOD_IS_MODIFIED);
                editor.ui.updatePatternData = true;
            }
            else
            {
                handleEditKeys(keyEntry, EDIT_NORMAL);
            }
        }
        break;

        case SDL_SCANCODE_L:
        {
            if (input.keyb.leftCtrlKeyDown)
            {
                if (!editor.blockMarkFlag)
                {
                    displayErrorMsg("NO BLOCK MARKED !");
                    return;
                }

                trackNoteDown(TRANSPOSE_ALL, editor.blockFromPos, editor.blockToPos);
            }
            else
            {
                handleEditKeys(keyEntry, EDIT_NORMAL);
            }
        }
        break;

        case SDL_SCANCODE_M:
        {
            if (input.keyb.leftCtrlKeyDown)
            {
                editor.multiFlag ^= 1;
                editor.ui.updateTrackerFlags = true;
                editor.ui.updateKeysText = true;
            }
            else if (input.keyb.leftAltKeyDown)
            {
                if (input.keyb.shiftKeyDown)
                    editor.metroChannel = editor.cursor.channel + 1;
                else
                    editor.metroFlag ^= 1;

                editor.ui.updateTrackerFlags = true;
            }
            else
            {
                handleEditKeys(keyEntry, EDIT_NORMAL);
            }
        }
        break;

        case SDL_SCANCODE_N:
        {
            if (input.keyb.leftCtrlKeyDown)
            {
                editor.blockMarkFlag = true;
                modEntry->currRow = editor.blockToPos;
            }
            else
            {
                handleEditKeys(keyEntry, EDIT_NORMAL);
            }
        }
        break;

        case SDL_SCANCODE_O:
        {
            if (input.keyb.leftCtrlKeyDown)
            {
                // fun fact: this function is broken in PT but I fixed it in my clone

                saveUndo();

                j = modEntry->currRow + 1;
                while (j < MOD_ROWS)
                {
                    for (i = j; i < (MOD_ROWS - 1); ++i)
                    {
                        noteSrc = &modEntry->patterns[modEntry->currPattern][((i + 1) * AMIGA_VOICES) + editor.cursor.channel];
                        modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel] = *noteSrc;
                    }

                    // clear newly made row on very bottom
                    modEntry->patterns[modEntry->currPattern][(63 * AMIGA_VOICES) + editor.cursor.channel].period  = 0;
                    modEntry->patterns[modEntry->currPattern][(63 * AMIGA_VOICES) + editor.cursor.channel].sample  = 0;
                    modEntry->patterns[modEntry->currPattern][(63 * AMIGA_VOICES) + editor.cursor.channel].command = 0;
                    modEntry->patterns[modEntry->currPattern][(63 * AMIGA_VOICES) + editor.cursor.channel].param   = 0;

                   j++;
                }

                updateWindowTitle(MOD_IS_MODIFIED);
                editor.ui.updatePatternData = true;
            }
            else
            {
                handleEditKeys(keyEntry, EDIT_NORMAL);
            }
        }
        break;

        case SDL_SCANCODE_P:
        {
            if (input.keyb.leftCtrlKeyDown)
            {
                if (!editor.blockBufferFlag)
                {
                    displayErrorMsg("BUFFER IS EMPTY !");
                    return;
                }

                saveUndo();

                for (i = 0; i <= (editor.buffToPos - editor.buffFromPos); ++i)
                {
                    if ((modEntry->currRow + i) > 63)
                        break;

                    modEntry->patterns[modEntry->currPattern][((modEntry->currRow + i) * AMIGA_VOICES) + editor.cursor.channel]
                        = editor.blockBuffer[editor.buffFromPos + i];
                }

                if (!input.keyb.shiftKeyDown)
                {
                    modEntry->currRow += (i & 0xFF);
                    if (modEntry->currRow > 63)
                        modEntry->currRow = 0;
                }

                updateWindowTitle(MOD_IS_MODIFIED);
                editor.ui.updatePatternData = true;
            }
            else if (input.keyb.leftAltKeyDown)
            {
                if (!editor.ui.diskOpScreenShown)
                {
                    editor.ui.posEdScreenShown ^= 1;
                    if (editor.ui.posEdScreenShown)
                    {
                        renderPosEdScreen();
                        editor.ui.updatePosEd = true;
                    }
                    else
                    {
                        displayMainScreen();
                    }
                }
            }
            else
            {
                handleEditKeys(keyEntry, EDIT_NORMAL);
            }
        }
        break;

        case SDL_SCANCODE_Q:
        {
            if (input.keyb.leftAmigaKeyDown)
            {
                trackNoteDown(TRANSPOSE_ALL, 0, MOD_ROWS - 1);
            }
            else if (input.keyb.leftCtrlKeyDown)
            {
                editor.muted[0] = false;
                editor.muted[1] = false;
                editor.muted[2] = false;
                editor.muted[3] = false;
                renderMuteButtons();
            }
            else if (input.keyb.leftAltKeyDown)
            {
                editor.ui.askScreenShown = true;
                editor.ui.askScreenType  = ASK_QUIT;

                pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
                setStatusMessage("REALLY QUIT ?", NO_CARRY);
                renderAskDialog();
            }
            else
            {
                handleEditKeys(keyEntry, EDIT_NORMAL);
            }
        }
        break;

        case SDL_SCANCODE_R:
        {
            if (input.keyb.leftAmigaKeyDown)
            {
                pattNoteDown(TRANSPOSE_ALL);
            }
            else if (input.keyb.leftCtrlKeyDown)
            {
                editor.f6Pos  = 0;
                editor.f7Pos  = 16;
                editor.f8Pos  = 32;
                editor.f9Pos  = 48;
                editor.f10Pos = 63;

                displayMsg("POS RESTORED !");
            }
            else if (input.keyb.leftAltKeyDown)
            {
                editor.ui.askScreenShown = true;
                editor.ui.askScreenType  = ASK_RESAMPLE;

                pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
                setStatusMessage("RESAMPLE?", NO_CARRY);
                renderAskDialog();
            }
            else
            {
                handleEditKeys(keyEntry, EDIT_NORMAL);
            }
        }
        break;

        case SDL_SCANCODE_S:
        {
            if (input.keyb.leftCtrlKeyDown)
                saveModule(DONT_CHECK_IF_FILE_EXIST, DONT_GIVE_NEW_FILENAME);
            else if (input.keyb.leftAmigaKeyDown)
                pattOctaUp(TRANSPOSE_ALL);
            else if (input.keyb.leftAltKeyDown)
                samplerScreen();
            else
                handleEditKeys(keyEntry, EDIT_NORMAL);
        }
        break;

        case SDL_SCANCODE_T:
        {
            if (input.keyb.leftCtrlKeyDown)
            {
                editor.swapChannelFlag = true;

                pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
                setStatusMessage("SWAP (1/2/3/4) ?", NO_CARRY);
            }
            else if (input.keyb.leftAltKeyDown)
            {
                toggleTuningTone();
            }
            else
            {
                handleEditKeys(keyEntry, EDIT_NORMAL);
            }
        }
        break;

        case SDL_SCANCODE_U:
        {
            if (input.keyb.leftCtrlKeyDown)
                undoLastChange();
            else
                handleEditKeys(keyEntry, EDIT_NORMAL);
        }
        break;

        case SDL_SCANCODE_V:
        {
            if (input.keyb.leftAmigaKeyDown)
            {
                pattOctaDown(TRANSPOSE_ALL);
            }
            else if (input.keyb.leftCtrlKeyDown)
            {
                if (editor.ui.samplerScreenShown)
                {
                    samplerSamPaste();
                }
                else
                {
                    editor.ui.askScreenShown = true;
                    editor.ui.askScreenType  = ASK_FILTER_ALL_SAMPLES;

                    pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
                    setStatusMessage("FILTER ALL SAMPLS", NO_CARRY);
                    renderAskDialog();
                }
            }
            else if (input.keyb.leftAltKeyDown)
            {
                editor.muted[3] ^= 1; // toggle channel 4
                renderMuteButtons();
            }
            else
            {
                handleEditKeys(keyEntry, EDIT_NORMAL);
            }
        }
        break;

        case SDL_SCANCODE_W:
        {
            if (input.keyb.leftAmigaKeyDown)
            {
                pattNoteDown(TRANSPOSE_ALL);
            }
            else if (input.keyb.leftCtrlKeyDown)
            {
                if (!editor.blockBufferFlag)
                {
                    displayErrorMsg("BUFFER IS EMPTY !");
                    return;
                }

                saveUndo();

                chTmp = editor.cursor.channel;
                for (i = 0; i <= (editor.buffToPos - editor.buffFromPos); ++i)
                {
                    if ((modEntry->currRow + i) > 63)
                        break;

                    noteDst = &modEntry->patterns[modEntry->currPattern][((modEntry->currRow + i) * AMIGA_VOICES) + chTmp];

                    if (!(editor.blockBuffer[i].period || editor.blockBuffer[i].sample))
                    {
                        noteDst->command = editor.blockBuffer[i].command;
                        noteDst->param   = editor.blockBuffer[i].param;
                    }
                    else
                    {
                        *noteDst = editor.blockBuffer[i];
                    }

                    chTmp = (chTmp + 1) & 3;
                }

                if (!input.keyb.shiftKeyDown)
                {
                    modEntry->currRow += (i & 0xFF);
                    if (modEntry->currRow > 63)
                        modEntry->currRow = 0;
                }

                updateWindowTitle(MOD_IS_MODIFIED);
                editor.ui.updatePatternData = true;
            }
            else
            {
                handleEditKeys(keyEntry, EDIT_NORMAL);
            }
        }
        break;

        case SDL_SCANCODE_X:
        {
            if (input.keyb.leftAmigaKeyDown)
            {
                pattOctaDown(TRANSPOSE_ALL);
            }
            else if (input.keyb.leftCtrlKeyDown)
            {
                if (editor.ui.samplerScreenShown)
                {
                    samplerSamDelete(SAMPLE_CUT);
                    return;
                }

                if (!editor.blockMarkFlag)
                {
                    displayErrorMsg("NO BLOCK MARKED !");
                    return;
                }

                editor.blockMarkFlag = false;

                saveUndo();

                editor.blockBufferFlag = true;

                for (i = 0; i < MOD_ROWS; ++i)
                    editor.blockBuffer[i] = modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel];

                if (editor.blockFromPos > editor.blockToPos)
                {
                    editor.buffFromPos = editor.blockToPos;
                    editor.buffToPos   = editor.blockFromPos;
                }
                else
                {
                    editor.buffFromPos = editor.blockFromPos;
                    editor.buffToPos   = editor.blockToPos;
                }

                for (i = editor.buffFromPos; i <= editor.buffToPos; ++i)
                {
                    modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel].period  = 0;
                    modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel].sample  = 0;
                    modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel].command = 0;
                    modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel].param   = 0;
                }

                setStatusMessage(editor.allRightText, DO_CARRY);
                updateWindowTitle(MOD_IS_MODIFIED);
                editor.ui.updatePatternData = true;
            }
            else
            {
                if (input.keyb.leftAltKeyDown)
                {
                    editor.muted[1] ^= 1; // toggle channel 2
                    renderMuteButtons();
                }
                else
                {
                    handleEditKeys(keyEntry, EDIT_NORMAL);
                }
            }
        }
        break;

        case SDL_SCANCODE_Y:
        {
            if (input.keyb.leftCtrlKeyDown)
            {
                if (!editor.blockMarkFlag)
                {
                    displayErrorMsg("NO BLOCK MARKED !");
                    return;
                }

                editor.blockMarkFlag = false;

                saveUndo();

                if (editor.blockFromPos >= editor.blockToPos)
                {
                    blockFrom = editor.blockToPos;
                    blockTo   = editor.blockFromPos;
                }
                else
                {
                    blockFrom = editor.blockFromPos;
                    blockTo   = editor.blockToPos;
                }

                while (blockFrom < blockTo)
                {
                    noteDst = &modEntry->patterns[modEntry->currPattern][(blockFrom * AMIGA_VOICES) + editor.cursor.channel];
                    noteSrc = &modEntry->patterns[modEntry->currPattern][(blockTo   * AMIGA_VOICES) + editor.cursor.channel];

                    noteTmp  = *noteDst;
                    *noteDst = *noteSrc;
                    *noteSrc =  noteTmp;

                    blockFrom += 1;
                    blockTo   -= 1;
                }

                setStatusMessage(editor.allRightText, DO_CARRY);
                updateWindowTitle(MOD_IS_MODIFIED);
                editor.ui.updatePatternData = true;
            }
            else if (input.keyb.leftAltKeyDown)
            {
                editor.ui.askScreenShown = true;
                editor.ui.askScreenType  = ASK_SAVE_ALL_SAMPLES;

                pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
                setStatusMessage("SAVE ALL SAMPLES?", NO_CARRY);
                renderAskDialog();
            }
            else
            {
                handleEditKeys(keyEntry, EDIT_NORMAL);
            }
        }
        break;

        case SDL_SCANCODE_Z:
        {
            if (input.keyb.leftAmigaKeyDown)
            {
                trackOctaDown(TRANSPOSE_ALL, 0, MOD_ROWS - 1);
            }
            else if (input.keyb.leftCtrlKeyDown)
            {
                if (editor.ui.samplerScreenShown)
                {
                    editor.ui.askScreenShown = true;
                    editor.ui.askScreenType  = ASK_RESTORE_SAMPLE;

                    pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
                    setStatusMessage("RESTORE SAMPLE ?", NO_CARRY);
                    renderAskDialog();
                }
                else
                {
                    modSetTempo(125);
                    modSetSpeed(6);

                    for (i = 0; i < AMIGA_VOICES; ++i)
                    {
                        modEntry->channels[i].n_wavecontrol = 0;
                        modEntry->channels[i].n_glissfunk   = 0;
                        modEntry->channels[i].n_finetune    = 0;
                        modEntry->channels[i].n_loopcount   = 0;
                    }

                    displayMsg("EFX RESTORED !");
                }
            }
            else if (input.keyb.leftAltKeyDown)
            {
                editor.muted[0] ^= 1; // toggle channel 1
                renderMuteButtons();
            }
            else
            {
                handleEditKeys(keyEntry, EDIT_NORMAL);
            }
        }
        break;

        default:
            handleEditKeys(keyEntry, EDIT_NORMAL);
        break;
    }
}

void movePatCurPrevCh(void)
{
    int8_t pos;

    pos = ((editor.cursor.pos + 5) / 6) - 1;

    editor.cursor.pos  = (pos < 0) ? (3 * 6) : (pos * 6);
    editor.cursor.mode = CURSOR_NOTE;

         if (editor.cursor.pos <  6) editor.cursor.channel = 0;
    else if (editor.cursor.pos < 12) editor.cursor.channel = 1;
    else if (editor.cursor.pos < 18) editor.cursor.channel = 2;
    else if (editor.cursor.pos < 24) editor.cursor.channel = 3;

    updateCursorPos();
}

void movePatCurNextCh(void)
{
    int8_t pos;

    pos = (editor.cursor.pos / 6) + 1;

    editor.cursor.pos  = (pos == 4) ? 0 : (pos * 6);
    editor.cursor.mode = CURSOR_NOTE;

         if (editor.cursor.pos <  6) editor.cursor.channel = 0;
    else if (editor.cursor.pos < 12) editor.cursor.channel = 1;
    else if (editor.cursor.pos < 18) editor.cursor.channel = 2;
    else if (editor.cursor.pos < 24) editor.cursor.channel = 3;

    updateCursorPos();
}

void movePatCurRight(void)
{
    editor.cursor.pos = (editor.cursor.pos == 23) ? 0 : (editor.cursor.pos + 1);

         if (editor.cursor.pos <  6) editor.cursor.channel = 0;
    else if (editor.cursor.pos < 12) editor.cursor.channel = 1;
    else if (editor.cursor.pos < 18) editor.cursor.channel = 2;
    else if (editor.cursor.pos < 24) editor.cursor.channel = 3;

    editor.cursor.mode = editor.cursor.pos % 6;
    updateCursorPos();
}

void movePatCurLeft(void)
{
    editor.cursor.pos = (editor.cursor.pos == 0) ? 23 : (editor.cursor.pos - 1);

         if (editor.cursor.pos <  6) editor.cursor.channel = 0;
    else if (editor.cursor.pos < 12) editor.cursor.channel = 1;
    else if (editor.cursor.pos < 18) editor.cursor.channel = 2;
    else if (editor.cursor.pos < 24) editor.cursor.channel = 3;

    editor.cursor.mode = editor.cursor.pos % 6;
    updateCursorPos();
}

void handleKeyRepeat(SDL_Scancode keyEntry)
{
    uint8_t repeatNum;

    if (!input.keyb.repeatKey || (editor.ui.clearScreenShown || editor.ui.askScreenShown))
    {
        input.keyb.repeatCounter = 0;
        return;
    }

    if (input.keyb.delayKey)
    {
        if (input.keyb.delayCounter < KEYB_REPEAT_DELAY)
        {
            input.keyb.delayCounter++;
            return;
        }
    }

    switch (keyEntry) // only some buttons repeat
    {
        case SDL_SCANCODE_PAGEUP:
        {
            if (input.keyb.repeatCounter >= 3)
            {
                input.keyb.repeatCounter = 0;

                if (editor.ui.terminalShown)
                {
                    terminalScrollPageUp();
                }
                else if (editor.ui.posEdScreenShown)
                {
                    if ((modEntry->currOrder - (POSED_LIST_SIZE - 1)) > 0)
                        modSetPos(modEntry->currOrder - (POSED_LIST_SIZE - 1), DONT_SET_ROW);
                    else
                        modSetPos(0, DONT_SET_ROW);
                }
                else if (editor.ui.diskOpScreenShown)
                {
                    if (editor.ui.diskOpScreenShown)
                    {
                        editor.diskop.scrollOffset -= (DISKOP_LIST_SIZE - 1);
                        if (editor.diskop.scrollOffset < 0)
                            editor.diskop.scrollOffset = 0;

                        editor.ui.updateDiskOpFileList = true;
                    }
                }
                else
                {
                    if ((editor.currMode == MODE_IDLE) || (editor.currMode == MODE_EDIT))
                    {
                             if (modEntry->currRow == 63) modSetPos(DONT_SET_ORDER, modEntry->currRow - 15);
                        else if (modEntry->currRow == 15) modSetPos(DONT_SET_ORDER, 0); // 15-16 would turn into -1, which is "DON'T SET ROW" flag
                        else                              modSetPos(DONT_SET_ORDER, modEntry->currRow - 16);
                    }
                }
            }
        }
        break;

        case SDL_SCANCODE_PAGEDOWN:
        {
            if (editor.ui.terminalShown)
            {
                terminalScrollPageDown();
            }
            else if (input.keyb.repeatCounter >= 3)
            {
                input.keyb.repeatCounter = 0;

                if (editor.ui.posEdScreenShown)
                {
                    if ((modEntry->currOrder + (POSED_LIST_SIZE - 1)) <= (modEntry->head.orderCount - 1))
                        modSetPos(modEntry->currOrder + (POSED_LIST_SIZE - 1), DONT_SET_ROW);
                    else
                        modSetPos(modEntry->head.orderCount - 1, DONT_SET_ROW);
                }
                else if (editor.ui.diskOpScreenShown)
                {
                    if (editor.diskop.numFiles > DISKOP_LIST_SIZE)
                    {
                        editor.diskop.scrollOffset += (DISKOP_LIST_SIZE - 1);
                        if (editor.diskop.scrollOffset > (editor.diskop.numFiles - DISKOP_LIST_SIZE))
                            editor.diskop.scrollOffset =  editor.diskop.numFiles - DISKOP_LIST_SIZE;

                        editor.ui.updateDiskOpFileList = true;
                    }
                }
                else
                {
                    if ((editor.currMode == MODE_IDLE) || (editor.currMode == MODE_EDIT))
                        modSetPos(DONT_SET_ORDER, modEntry->currRow + 16);
                }
            }
        }
        break;

        case SDL_SCANCODE_LEFT:
        {
            if (editor.ui.editTextFlag)
            {
                if (input.keyb.repeatCounter >= 3)
                {
                    input.keyb.repeatCounter = 0;
                    textCharPrevious();
                }
            }
            else
            {
                if (input.keyb.leftCtrlKeyDown)
                {
                    if (input.keyb.repeatCounter >= 6)
                    {
                        input.keyb.repeatCounter = 0;
                        sampleDownButton();
                    }
                }
                else if (input.keyb.shiftKeyDown)
                {
                    if (input.keyb.repeatCounter >= 5)
                    {
                        input.keyb.repeatCounter = 0;

                        if (modEntry->currOrder > 0)
                            modSetPos(modEntry->currOrder - 1, DONT_SET_ROW);
                    }
                }
                else if (input.keyb.leftAltKeyDown)
                {
                    if (input.keyb.repeatCounter >= 7)
                    {
                        input.keyb.repeatCounter = 0;
                        decPatt();
                    }
                }
                else
                {
                    if (input.keyb.repeatCounter >= 7)
                    {
                        input.keyb.repeatCounter = 0;

                        if (!input.keyb.shiftKeyDown && !input.keyb.leftAltKeyDown && !input.keyb.leftCtrlKeyDown)
                            movePatCurLeft();
                    }
                }
            }
        }
        break;

        case SDL_SCANCODE_RIGHT:
        {
            if (editor.ui.editTextFlag)
            {
                if (input.keyb.repeatCounter >= 3)
                {
                    input.keyb.repeatCounter = 0;
                    textCharNext();
                }
            }
            else
            {
                if (input.keyb.leftCtrlKeyDown)
                {
                    if (input.keyb.repeatCounter >= 6)
                    {
                        input.keyb.repeatCounter = 0;
                        sampleUpButton();
                    }
                }
                else if (input.keyb.shiftKeyDown)
                {
                    if (input.keyb.repeatCounter >= 5)
                    {
                        input.keyb.repeatCounter = 0;

                        if (modEntry->currOrder < 126)
                            modSetPos(modEntry->currOrder + 1, DONT_SET_ROW);
                    }
                }
                else if (input.keyb.leftAltKeyDown)
                {
                    if (input.keyb.repeatCounter >= 7)
                    {
                        input.keyb.repeatCounter = 0;
                        incPatt();
                    }
                }
                else
                {
                    if (input.keyb.repeatCounter >= 7)
                    {
                        input.keyb.repeatCounter = 0;

                        if (!input.keyb.shiftKeyDown && !input.keyb.leftAltKeyDown && !input.keyb.leftCtrlKeyDown)
                            movePatCurRight();
                    }
                }
            }
        }
        break;

        case SDL_SCANCODE_UP:
        {
            if (editor.ui.terminalShown)
            {
                if (input.keyb.repeatCounter >= 1)
                {
                    input.keyb.repeatCounter = 0;
                    terminalScrollUp();
                }
            }
            else if (editor.ui.diskOpScreenShown)
            {
                if (input.keyb.repeatCounter >= 1)
                {
                    input.keyb.repeatCounter = 0;

                    editor.diskop.scrollOffset--;

                    if (input.mouse.rightButtonPressed) // PT quirk: right mouse button speeds up even on keyb UP/DOWN
                        editor.diskop.scrollOffset -= 3;

                    if (editor.diskop.scrollOffset < 0)
                        editor.diskop.scrollOffset = 0;

                    editor.ui.updateDiskOpFileList = true;
                }
            }
            else if (editor.ui.posEdScreenShown)
            {
                if (input.keyb.repeatCounter >= 3)
                {
                    input.keyb.repeatCounter = 0;

                    if (modEntry->currOrder > 0)
                    {
                        modSetPos(modEntry->currOrder - 1, DONT_SET_ROW);
                        editor.ui.updatePosEd = true;
                    }
                }
            }
            else if (!editor.ui.samplerScreenShown)
            {
                if ((editor.currMode != MODE_PLAY) && (editor.currMode != MODE_RECORD))
                {
                    repeatNum = 7;

                         if (input.keyb.leftAltKeyDown)   repeatNum = 1;
                    else if (input.keyb.shiftKeyDown) repeatNum = 3;

                    if (input.keyb.repeatCounter >= repeatNum)
                    {
                        input.keyb.repeatCounter = 0;
                        modSetPos(DONT_SET_ORDER, (modEntry->currRow - 1) & 0x3F);
                    }
                }
            }
        }
        break;

        case SDL_SCANCODE_DOWN:
        {
            if (editor.ui.terminalShown)
            {
                if (input.keyb.repeatCounter >= 1)
                {
                    input.keyb.repeatCounter = 0;
                    terminalScrollDown();
                }
            }
            else if (editor.ui.diskOpScreenShown)
            {
                if (input.keyb.repeatCounter >= 1)
                {
                    input.keyb.repeatCounter = 0;

                    if (editor.diskop.numFiles > DISKOP_LIST_SIZE)
                    {
                        editor.diskop.scrollOffset++;

                        if (input.mouse.rightButtonPressed) // PT quirk: right mouse button speeds up even on keyb UP/DOWN
                            editor.diskop.scrollOffset += 3;

                        if (editor.diskop.scrollOffset > (editor.diskop.numFiles - DISKOP_LIST_SIZE))
                            editor.diskop.scrollOffset =  editor.diskop.numFiles - DISKOP_LIST_SIZE;

                        editor.ui.updateDiskOpFileList = true;
                    }
                }
            }
            else if (editor.ui.posEdScreenShown)
            {
                if (input.keyb.repeatCounter >= 3)
                {
                    input.keyb.repeatCounter = 0;

                    if (modEntry->currOrder != (modEntry->head.orderCount - 1))
                    {
                        if (++modEntry->currOrder > (modEntry->head.orderCount - 1))
                              modEntry->currOrder =  modEntry->head.orderCount - 1;

                        modSetPos(modEntry->currOrder, DONT_SET_ROW);
                        editor.ui.updatePosEd = true;
                    }
                }
            }
            else if (!editor.ui.samplerScreenShown)
            {
                if ((editor.currMode != MODE_PLAY) && (editor.currMode != MODE_RECORD))
                {
                    repeatNum = 7;

                         if (input.keyb.leftAltKeyDown)   repeatNum = 1;
                    else if (input.keyb.shiftKeyDown) repeatNum = 3;

                    if (input.keyb.repeatCounter >= repeatNum)
                    {
                        input.keyb.repeatCounter = 0;
                        modSetPos(DONT_SET_ORDER, (modEntry->currRow + 1) & 0x3F);
                    }
                }
            }
        }
        break;

        case SDL_SCANCODE_BACKSPACE:
        {
            if (editor.ui.editTextFlag)
            {
                // only repeat backspace while editing texts

                if (input.keyb.repeatCounter >= 3)
                {
                    input.keyb.repeatCounter = 0;
                    keyDownHandler(keyEntry, 0);
                }
            }
        }
        break;

        case SDL_SCANCODE_KP_ENTER:
        case SDL_SCANCODE_RETURN:
            break; // do NOT repeat enter!

        default:
        {
            if (input.keyb.repeatCounter >= 3)
            {
                input.keyb.repeatCounter = 0;
                keyDownHandler(keyEntry, 0);
            }
        }
        break;
    }

    input.keyb.repeatCounter++;
}

int8_t handleGeneralModes(SDL_Scancode keyEntry)
{
    int8_t rawKey;
    int16_t i;
    note_t *noteSrc, noteTmp;

    // SAMPLER SCREEN (volume box)
    if (editor.ui.samplerVolBoxShown && !editor.ui.editTextFlag)
    {
        if (keyEntry == SDL_SCANCODE_ESCAPE)
        {
            editor.ui.samplerVolBoxShown = false;
            removeSamplerVolBox();

            return (false);
        }
    }

    // SAMPLER SCREEN (filters box)
    if (editor.ui.samplerFiltersBoxShown && !editor.ui.editTextFlag)
    {
        if (keyEntry == SDL_SCANCODE_ESCAPE)
        {
            editor.ui.samplerFiltersBoxShown = false;
            removeSamplerFiltersBox();

            return (false);
        }
    }

    // EDIT OP. SCREEN #3
    if (editor.mixFlag)
    {
        if (keyEntry == SDL_SCANCODE_ESCAPE)
        {
            exitGetTextLine(EDIT_TEXT_UPDATE);

            editor.mixFlag = false;
            editor.ui.updateMixText = true;

            return (false);
        }
    }

    // EDIT OP. SCREEN #4
    if (editor.ui.changingChordNote)
    {
        if (keyEntry == SDL_SCANCODE_ESCAPE)
        {
            editor.ui.changingChordNote = false;

            setPrevStatusMessage();
            pointerSetPreviousMode();

            return (false);
        }

             if (keyEntry == SDL_SCANCODE_F1) editor.keyOctave = OCTAVE_LOW;
        else if (keyEntry == SDL_SCANCODE_F2) editor.keyOctave = OCTAVE_HIGH;

        rawKey = keyToNote(scanCodeToUSKey(keyEntry));
        if (rawKey >= 0)
        {
            if (editor.ui.changingChordNote == 1)
            {
                editor.note1 = rawKey;
                editor.ui.updateNote1Text = true;
            }
            else if (editor.ui.changingChordNote == 2)
            {
                editor.note2 = rawKey;
                editor.ui.updateNote2Text = true;
            }
            else if (editor.ui.changingChordNote == 3)
            {
                editor.note3 = rawKey;
                editor.ui.updateNote3Text = true;
            }
            else if (editor.ui.changingChordNote == 4)
            {
                editor.note4 = rawKey;
                editor.ui.updateNote4Text = true;
            }

            editor.ui.changingChordNote = false;

            recalcChordLength();

            setPrevStatusMessage();
            pointerSetPreviousMode();
        }

        return (false);
    }

    // CHANGE DRUMPAD NOTE
    if (editor.ui.changingDrumPadNote)
    {
        if (keyEntry == SDL_SCANCODE_ESCAPE)
        {
            editor.ui.changingDrumPadNote = false;

            setPrevStatusMessage();
            pointerSetPreviousMode();

            return (false);
        }

             if (keyEntry == SDL_SCANCODE_F1) editor.keyOctave = OCTAVE_LOW;
        else if (keyEntry == SDL_SCANCODE_F2) editor.keyOctave = OCTAVE_HIGH;

        rawKey = keyToNote(scanCodeToUSKey(keyEntry));
        if (rawKey >= 0)
        {
            pNoteTable[editor.currSample] = rawKey;
            editor.ui.changingDrumPadNote = false;

            setPrevStatusMessage();
            pointerSetPreviousMode();
        }

        return (false);
    }

    // SAMPLER SCREEN
    if (editor.ui.changingSmpResample)
    {
        if (keyEntry == SDL_SCANCODE_ESCAPE)
        {
            editor.ui.changingSmpResample = false;
            editor.ui.updateResampleNote = true;

            setPrevStatusMessage();
            pointerSetPreviousMode();

            return (false);
        }

             if (keyEntry == SDL_SCANCODE_F1) editor.keyOctave = OCTAVE_LOW;
        else if (keyEntry == SDL_SCANCODE_F2) editor.keyOctave = OCTAVE_HIGH;

        rawKey = keyToNote(scanCodeToUSKey(keyEntry));
        if (rawKey >= 0)
        {
            editor.resampleNote = rawKey;
            editor.ui.changingSmpResample = false;
            editor.ui.updateResampleNote = true;

            setPrevStatusMessage();
            pointerSetPreviousMode();
        }

        return (false);
    }

    // DISK OP. SCREEN
    if (editor.diskop.isFilling)
    {
        if (editor.ui.askScreenShown && (editor.ui.askScreenType == ASK_QUIT))
        {
            if (keyEntry == SDL_SCANCODE_Y)
            {
                editor.ui.askScreenShown = false;
                editor.ui.answerNo       = false;
                editor.ui.answerYes      = true;

                handleAskYes();
            }
            else if (keyEntry == SDL_SCANCODE_N)
            {
                editor.ui.askScreenShown = false;
                editor.ui.answerNo       = true;
                editor.ui.answerYes      = false;

                handleAskNo();
            }
        }

        return (false);
    }

    // if MOD2WAV is ongoing, only react to ESC and Y/N on exit ask dialog
    if (editor.isWAVRendering)
    {
        if (editor.ui.askScreenShown && (editor.ui.askScreenType == ASK_QUIT))
        {
            if (keyEntry == SDL_SCANCODE_Y)
            {
                editor.isWAVRendering = false;
                SDL_WaitThread(editor.mod2WavThread, NULL);

                editor.ui.askScreenShown = false;
                editor.ui.answerNo       = false;
                editor.ui.answerYes      = true;

                handleAskYes();
            }
            else if (keyEntry == SDL_SCANCODE_N)
            {
                editor.ui.askScreenShown = false;
                editor.ui.answerNo       = true;
                editor.ui.answerYes      = false;

                handleAskNo();

                pointerSetMode(POINTER_MODE_READ_DIR, NO_CARRY);
                setStatusMessage("RENDERING MOD...", NO_CARRY);
            }
        }
        else if (keyEntry == SDL_SCANCODE_ESCAPE)
        {
            editor.abortMod2Wav = true;
        }

        return (false);
    }

    // SWAP CHANNEL (CTRL+T)
    if (editor.swapChannelFlag)
    {
        switch (keyEntry)
        {
            case SDL_SCANCODE_ESCAPE:
            {
                editor.swapChannelFlag = false;

                pointerSetPreviousMode();
                setPrevStatusMessage();
            }
            break;

            case SDL_SCANCODE_1:
            {
                for (i = 0; i < MOD_ROWS; ++i)
                {
                    noteSrc = &modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel];
                    noteTmp = modEntry->patterns[modEntry->currPattern][i * AMIGA_VOICES];

                    modEntry->patterns[modEntry->currPattern][i * AMIGA_VOICES] = *noteSrc;
                    *noteSrc = noteTmp;
                }

                editor.swapChannelFlag = false;

                pointerSetPreviousMode();
                setPrevStatusMessage();
            }
            break;

            case SDL_SCANCODE_2:
            {
                for (i = 0; i < MOD_ROWS; ++i)
                {
                    noteSrc = &modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel];
                    noteTmp = modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + 1];

                    modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + 1] = *noteSrc;
                    *noteSrc = noteTmp;
                }

                editor.swapChannelFlag = false;

                pointerSetPreviousMode();
                setPrevStatusMessage();
            }
            break;

            case SDL_SCANCODE_3:
            {
                for (i = 0; i < MOD_ROWS; ++i)
                {
                    noteSrc = &modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel];
                    noteTmp = modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + 2];

                    modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + 2] = *noteSrc;
                    *noteSrc = noteTmp;
                }

                editor.swapChannelFlag = false;

                pointerSetPreviousMode();
                setPrevStatusMessage();
            }
            break;

            case SDL_SCANCODE_4:
            {
                for (i = 0; i < MOD_ROWS; ++i)
                {
                    noteSrc = &modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel];
                    noteTmp = modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + 3];

                    modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + 3] = *noteSrc;
                    *noteSrc = noteTmp;
                }

                editor.swapChannelFlag = false;

                pointerSetPreviousMode();
                setPrevStatusMessage();
            }
            break;

            default: break;
        }

        return (false);
    }

    // YES/NO ASK DIALOG
    if (editor.ui.askScreenShown)
    {
        if (editor.ui.pat2SmpDialogShown)
        {
            // PAT2SMP specific ask dialog
            switch (keyEntry)
            {
                case SDL_SCANCODE_KP_ENTER:
                case SDL_SCANCODE_RETURN:
                case SDL_SCANCODE_H:
                {
                    editor.ui.askScreenShown = false;
                    editor.ui.answerNo       = true;
                    editor.ui.answerYes      = false;

                    editor.pat2SmpHQ = true;
                    handleAskYes();
                }
                break;

                case SDL_SCANCODE_L:
                {
                    editor.ui.askScreenShown = false;
                    editor.ui.answerNo       = false;
                    editor.ui.answerYes      = true;

                    editor.pat2SmpHQ = false;
                    handleAskYes();

                    // pointer/status is updated by the 'yes handler'
                }
                break;

                case SDL_SCANCODE_ESCAPE:
                case SDL_SCANCODE_A:
                case SDL_SCANCODE_N:
                {
                    editor.ui.askScreenShown = false;
                    editor.ui.answerNo       = true;
                    editor.ui.answerYes      = false;

                    handleAskNo();
                }
                break;

                default: break;
            }
        }
        else
        {
            // normal yes/no dialog
            switch (keyEntry)
            {
                case SDL_SCANCODE_ESCAPE:
                case SDL_SCANCODE_N:
                {
                    editor.ui.askScreenShown = false;
                    editor.ui.answerNo       = true;
                    editor.ui.answerYes      = false;

                    handleAskNo();
                }
                break;

                case SDL_SCANCODE_KP_ENTER:
                case SDL_SCANCODE_RETURN:
                case SDL_SCANCODE_Y:
                {
                    editor.ui.askScreenShown = false;
                    editor.ui.answerNo       = false;
                    editor.ui.answerYes      = true;

                    handleAskYes();

                    // pointer/status is updated by the 'yes handler'
                }
                break;

                default: break;
            }
        }

        return (false);
    }

    // CLEAR SCREEN DIALOG
    if (editor.ui.clearScreenShown)
    {
        switch (keyEntry)
        {
            case SDL_SCANCODE_S:
            {
                editor.ui.clearScreenShown = false;
                removeClearScreen();

                modStop();
                clearSamples();

                editor.playMode = PLAY_MODE_NORMAL;
                editor.currMode = MODE_IDLE;

                pointerSetPreviousMode();
                setPrevStatusMessage();
            }
            break;

            case SDL_SCANCODE_O:
            {
                editor.ui.clearScreenShown = false;
                removeClearScreen();

                modStop();
                clearSong();

                editor.playMode = PLAY_MODE_NORMAL;
                editor.currMode = MODE_IDLE;

                pointerSetPreviousMode();
                setPrevStatusMessage();
            }
            break;

            case SDL_SCANCODE_A:
            {
                editor.ui.clearScreenShown = false;
                removeClearScreen();

                modStop();
                clearAll();

                editor.playMode = PLAY_MODE_NORMAL;
                editor.currMode = MODE_IDLE;

                pointerSetPreviousMode();
                setPrevStatusMessage();
            }
            break;

            case SDL_SCANCODE_C:
            case SDL_SCANCODE_ESCAPE:
            {
                editor.ui.clearScreenShown = false;
                removeClearScreen();

                editor.currMode = MODE_IDLE;

                pointerSetPreviousMode();
                setPrevStatusMessage();

                editor.errorMsgActive  = true;
                editor.errorMsgBlock   = true;
                editor.errorMsgCounter = 0;

                pointerErrorMode();
            }
            break;

            default: break;
        }

        return (false);
    }

    return (true);
}

void handleTextEditInputChar(char textChar)
{
    char *readTmp;
    int8_t readTmpPrev;
    uint8_t digit1, digit2, digit3, digit4, digit5;
    uint32_t number;

    // we only want certain ASCII keys
    if ((textChar < 32) || (textChar > 126))
        return;

    // a..z -> A..Z
    if ((textChar >= 'a') && (textChar <= 'z'))
        textChar = toupper(textChar);

    if (editor.ui.editTextType == TEXT_EDIT_STRING)
    {
        if (editor.ui.editPos < editor.ui.textEndPtr)
        {
            if (!editor.mixFlag)
            {
                readTmp = editor.ui.textEndPtr;
                while (readTmp > editor.ui.editPos)
                {
                    readTmpPrev    = *--readTmp;
                    *(readTmp + 1) = readTmpPrev;
                }

                *editor.ui.textEndPtr = '\0';
                *editor.ui.editPos++  = textChar;

                textMarkerMoveRight();
            }
            else
            {
                if (((textChar >= '0') && (textChar <= '9')) || ((textChar >= 'A') && (textChar <= 'F')))
                {
                    if (editor.ui.dstPos == 14) // hack for sample mix text
                    {
                        *editor.ui.editPos = textChar;
                    }
                    else
                    {
                        *editor.ui.editPos++ = textChar;
                        textMarkerMoveRight();

                        if (editor.ui.dstPos == 9) // hack for sample mix text
                        {
                            editor.ui.editPos++;
                            textMarkerMoveRight();

                            editor.ui.editPos++;
                            textMarkerMoveRight();

                            editor.ui.editPos++;
                            textMarkerMoveRight();

                            editor.ui.editPos++;
                            textMarkerMoveRight();
                        }
                        else if (editor.ui.dstPos == 6) // hack for sample mix text
                        {
                            editor.ui.editPos++;
                            textMarkerMoveRight();
                        }
                    }
                }
            }
        }
    }
    else
    {
        if (editor.ui.editTextType == TEXT_EDIT_DECIMAL)
        {
            if ((textChar >= '0') && (textChar <= '9'))
            {
                textChar -= '0';

                if (editor.ui.numLen == 5)
                {
                    number = *editor.ui.numPtr32;
                    digit5 = number % 10; number /= 10;
                    digit4 = number % 10; number /= 10;
                    digit3 = number % 10; number /= 10;
                    digit2 = number % 10; number /= 10;
                    digit1 = (uint8_t)(number);

                         if (editor.ui.dstPos == 0) *editor.ui.numPtr32 = (textChar * 10000) + (digit2 * 1000) + (digit3 * 100) + (digit4 * 10) + digit5;
                    else if (editor.ui.dstPos == 1) *editor.ui.numPtr32 = (digit1 * 10000) + (textChar * 1000) + (digit3 * 100) + (digit4 * 10) + digit5;
                    else if (editor.ui.dstPos == 2) *editor.ui.numPtr32 = (digit1 * 10000) + (digit2 * 1000) + (textChar * 100) + (digit4 * 10) + digit5;
                    else if (editor.ui.dstPos == 3) *editor.ui.numPtr32 = (digit1 * 10000) + (digit2 * 1000) + (digit3 * 100) + (textChar * 10) + digit5;
                    else if (editor.ui.dstPos == 4) *editor.ui.numPtr32 = (digit1 * 10000) + (digit2 * 1000) + (digit3 * 100) + (digit4 * 10) + textChar;
                }
                else if (editor.ui.numLen == 4)
                {
                    number = *editor.ui.numPtr16;
                    digit4 = number % 10; number /= 10;
                    digit3 = number % 10; number /= 10;
                    digit2 = number % 10; number /= 10;
                    digit1 = (uint8_t)(number);

                         if (editor.ui.dstPos == 0) *editor.ui.numPtr16 = (textChar * 1000) + (digit2 * 100) + (digit3 * 10) + digit4;
                    else if (editor.ui.dstPos == 1) *editor.ui.numPtr16 = (digit1 * 1000) + (textChar * 100) + (digit3 * 10) + digit4;
                    else if (editor.ui.dstPos == 2) *editor.ui.numPtr16 = (digit1 * 1000) + (digit2 * 100) + (textChar * 10) + digit4;
                    else if (editor.ui.dstPos == 3) *editor.ui.numPtr16 = (digit1 * 1000) + (digit2 * 100) + (digit3 * 10) + textChar;
                }
                else if (editor.ui.numLen == 3)
                {
                    number = *editor.ui.numPtr16;
                    digit3 = number % 10; number /= 10;
                    digit2 = number % 10; number /= 10;
                    digit1 = (uint8_t)(number);

                         if (editor.ui.dstPos == 0) *editor.ui.numPtr16 = (textChar * 100) + (digit2 * 10) + digit3;
                    else if (editor.ui.dstPos == 1) *editor.ui.numPtr16 = (digit1 * 100) + (textChar * 10) + digit3;
                    else if (editor.ui.dstPos == 2) *editor.ui.numPtr16 = (digit1 * 100) + (digit2 * 10) + textChar;
                }
                else if (editor.ui.numLen == 2)
                {
                    number = *editor.ui.numPtr16;
                    digit2 = number % 10; number /= 10;
                    digit1 = (uint8_t)(number);

                         if (editor.ui.dstPos == 0) *editor.ui.numPtr16 = (textChar * 10) + digit2;
                    else if (editor.ui.dstPos == 1) *editor.ui.numPtr16 = (digit1 * 10) + textChar;
                }

                textMarkerMoveRight();
                if (editor.ui.dstPos >= editor.ui.numLen)
                    exitGetTextLine(EDIT_TEXT_UPDATE);
            }
        }
        else
        {
            if (((textChar >= '0') && (textChar <= '9')) || ((textChar >= 'A') && (textChar <= 'F')))
            {
                     if (textChar <= '9') textChar -= '0';
                else if (textChar <= 'F') textChar -= ('A' - 10);

                if (editor.ui.numBits == 17)
                {
                    // only for sample length/repeat/replen
                    *editor.ui.numPtr32 &= ~(0x000F0000 >> (editor.ui.dstPos << 2));
                    *editor.ui.numPtr32 |= (textChar << (16 - (editor.ui.dstPos << 2)));
                }
                else if (editor.ui.numBits == 16)
                {
                    *editor.ui.numPtr16 &= ~(0xF000 >> (editor.ui.dstPos << 2));
                    *editor.ui.numPtr16 |= (textChar << (12 - (editor.ui.dstPos << 2)));
                }
                else if (editor.ui.numBits == 8)
                {
                    *editor.ui.numPtr8 &= ~(0xF0 >> (editor.ui.dstPos << 2));
                    *editor.ui.numPtr8 |= (textChar << (4 - (editor.ui.dstPos << 2)));
                }

                textMarkerMoveRight();
                if (editor.ui.dstPos >= editor.ui.numLen)
                    exitGetTextLine(EDIT_TEXT_UPDATE);
            }
        }
    }

    updateTextObject(editor.ui.editObject);

    if (!input.keyb.repeatKey)
        input.keyb.delayCounter = 0;

    input.keyb.repeatKey = true;
    input.keyb.delayKey  = true;
}

int8_t handleTextEditMode(SDL_Scancode keyEntry)
{
    char *readTmp;
    int8_t readTmpNext;
    int16_t i, j;
    note_t *noteSrc;

    switch (keyEntry)
    {
        case SDL_SCANCODE_ESCAPE:
        {
            editor.blockMarkFlag = false;

            if (editor.ui.editTextFlag)
            {
                exitGetTextLine(EDIT_TEXT_NO_UPDATE);
                return (false);
            }
        }
        break;

        case SDL_SCANCODE_HOME:
        {
            if (editor.ui.editTextFlag && !editor.mixFlag)
            {
                while (editor.ui.editPos > editor.ui.showTextPtr)
                    textCharPrevious();
            }
        }
        break;

        case SDL_SCANCODE_END:
        {
            if (editor.ui.editTextFlag && !editor.mixFlag)
            {
                if (editor.ui.editTextType != TEXT_EDIT_STRING)
                    break;

                while (!editor.ui.dstOffsetEnd)
                    textCharNext();
            }
        }
        break;

        case SDL_SCANCODE_LEFT:
        {
            if (editor.ui.editTextFlag)
            {
                textCharPrevious();

                if (!input.keyb.repeatKey)
                    input.keyb.delayCounter = 0;

                input.keyb.repeatKey = true;
                input.keyb.delayKey  = true;
            }
            else
            {
                input.keyb.delayKey  = false;
                input.keyb.repeatKey = true;
            }
        }
        break;

        case SDL_SCANCODE_RIGHT:
        {
            if (editor.ui.editTextFlag)
            {
                textCharNext();

                if (!input.keyb.repeatKey)
                    input.keyb.delayCounter = 0;

                input.keyb.repeatKey = true;
                input.keyb.delayKey  = true;
            }
            else
            {
                input.keyb.delayKey  = false;
                input.keyb.repeatKey = true;
            }
        }
        break;

        case SDL_SCANCODE_DELETE:
        {
            if (editor.ui.editTextFlag)
            {
                if (editor.mixFlag || (editor.ui.editTextType != TEXT_EDIT_STRING))
                    break;

                readTmp = editor.ui.editPos;
                while (readTmp < editor.ui.textEndPtr)
                {
                    readTmpNext = *(readTmp + 1);
                    *readTmp++  = readTmpNext;
                }

                // hack to prevent cloning last character if the song/sample name has one character too much
                if ((editor.ui.editObject == PTB_SONGNAME) || (editor.ui.editObject == PTB_SAMPLENAME))
                     *editor.ui.textEndPtr = '\0';

                if (!input.keyb.repeatKey)
                    input.keyb.delayCounter = 0;

                input.keyb.repeatKey = true;
                input.keyb.delayKey  = true;

                updateTextObject(editor.ui.editObject);
            }
        }
        break;

        case SDL_SCANCODE_BACKSPACE:
        {
            if (editor.ui.editTextFlag)
            {
                if (editor.mixFlag || (editor.ui.editTextType != TEXT_EDIT_STRING))
                    break;

                if (editor.ui.editPos > editor.ui.dstPtr)
                {
                    editor.ui.editPos--;

                    readTmp = editor.ui.editPos;
                    while (readTmp < editor.ui.textEndPtr)
                    {
                        readTmpNext = *(readTmp + 1);
                        *readTmp++  = readTmpNext;
                    }

                    // hack to prevent cloning last character if the song/sample name has one character too much
                    if ((editor.ui.editObject == PTB_SONGNAME) || (editor.ui.editObject == PTB_SAMPLENAME))
                        *editor.ui.textEndPtr = '\0';

                    textMarkerMoveLeft();
                    updateTextObject(editor.ui.editObject);
                }

                if (!input.keyb.repeatKey)
                    input.keyb.delayCounter = 0;

                input.keyb.repeatKey = true;
                input.keyb.delayKey  = true;
            }
            else
            {
                if (editor.ui.diskOpScreenShown)
                {
#ifdef _WIN32
                    diskOpSetPath(L"..", DISKOP_CACHE);
#else
                    diskOpSetPath("..", DISKOP_CACHE);
#endif
                }
                else if (input.keyb.shiftKeyDown || input.keyb.leftAltKeyDown || input.keyb.leftCtrlKeyDown)
                {
                    saveUndo();

                    if (input.keyb.leftAltKeyDown && !input.keyb.leftCtrlKeyDown)
                    {
                        if (modEntry->currRow > 0)
                        {
                            for (i = 0; i < AMIGA_VOICES; ++i)
                            {
                                for (j = (modEntry->currRow - 1); j < MOD_ROWS; ++j)
                                {
                                    noteSrc = &modEntry->patterns[modEntry->currPattern][((j + 1) * AMIGA_VOICES) + i];
                                    modEntry->patterns[modEntry->currPattern][(j * AMIGA_VOICES) + i] = *noteSrc;
                                }

                                // clear newly made row on very bottom
                                modEntry->patterns[modEntry->currPattern][(63 * AMIGA_VOICES) + i].period  = 0;
                                modEntry->patterns[modEntry->currPattern][(63 * AMIGA_VOICES) + i].sample  = 0;
                                modEntry->patterns[modEntry->currPattern][(63 * AMIGA_VOICES) + i].command = 0;
                                modEntry->patterns[modEntry->currPattern][(63 * AMIGA_VOICES) + i].param   = 0;
                            }

                            modEntry->currRow--;
                            editor.ui.updatePatternData = true;
                        }
                    }
                    else
                    {
                        if (modEntry->currRow > 0)
                        {
                            for (i = (modEntry->currRow - 1); i < (MOD_ROWS - 1); ++i)
                            {
                                noteSrc = &modEntry->patterns[modEntry->currPattern][((i + 1) * AMIGA_VOICES) + editor.cursor.channel];

                                if (input.keyb.leftCtrlKeyDown)
                                {
                                    modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel].command = noteSrc->command;
                                    modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel].param   = noteSrc->param;
                                }
                                else
                                {
                                    modEntry->patterns[modEntry->currPattern][(i * AMIGA_VOICES) + editor.cursor.channel] = *noteSrc;
                                }
                            }

                            // clear newly made row on very bottom

                            modEntry->patterns[modEntry->currPattern][(63 * AMIGA_VOICES) + editor.cursor.channel].period  = 0;
                            modEntry->patterns[modEntry->currPattern][(63 * AMIGA_VOICES) + editor.cursor.channel].sample  = 0;
                            modEntry->patterns[modEntry->currPattern][(63 * AMIGA_VOICES) + editor.cursor.channel].command = 0;
                            modEntry->patterns[modEntry->currPattern][(63 * AMIGA_VOICES) + editor.cursor.channel].param   = 0;

                            modEntry->currRow--;
                            editor.ui.updatePatternData = true;
                        }
                    }
                }
                else
                {
                    editor.stepPlayEnabled   = true;
                    editor.stepPlayBackwards = true;

                    doStopIt();
                    playPattern((modEntry->currRow - 1) & 0x3F);
                }
            }
        }
        break;

        default: break;
    }

    if (editor.ui.editTextFlag)
    {
        if ((keyEntry == SDL_SCANCODE_RETURN) || (keyEntry == SDL_SCANCODE_KP_ENTER))
        {
            // dirty hack
            if (editor.ui.editObject == PTB_SAMPLES)
                editor.ui.tmpDisp8++;

            exitGetTextLine(EDIT_TEXT_UPDATE);

            if (editor.mixFlag)
            {
                editor.mixFlag = false;
                editor.ui.updateMixText = true;

                doMix();
            }
        }

        return (false); // don't continue further key handling
    }

    return (true); // continue further key handling (we're not editing text)
}

void handleTerminalKeys(SDL_Scancode keyEntry)
{
    switch (keyEntry)
    {
        case SDL_SCANCODE_F12:
        {
            if (input.keyb.leftAltKeyDown)
            {
                editor.ui.terminalShown = false;
                removeTerminalScreen();
            }
        }
        break;

        case SDL_SCANCODE_ESCAPE:
        {
            editor.ui.terminalShown = false;
            removeTerminalScreen();
        }
        break;

        case SDL_SCANCODE_PAGEUP:
        {
            terminalScrollPageUp();

            if (!input.keyb.repeatKey)
                input.keyb.delayCounter = 0;

            input.keyb.repeatKey = true;
            input.keyb.delayKey  = true;

            input.keyb.repeatCounter = 0;
            input.keyb.lastRepKey = SDL_SCANCODE_PAGEUP;
        }
        break;

        case SDL_SCANCODE_PAGEDOWN:
        {
            terminalScrollPageDown();

            if (!input.keyb.repeatKey)
                input.keyb.delayCounter = 0;

            input.keyb.repeatKey = true;
            input.keyb.delayKey  = true;

            input.keyb.repeatCounter = 0;
            input.keyb.lastRepKey = SDL_SCANCODE_PAGEDOWN;
        }
        break;

        case SDL_SCANCODE_UP:
        {
            terminalScrollUp();

            if (!input.keyb.repeatKey)
                input.keyb.delayCounter = 0;

            input.keyb.repeatKey = true;
            input.keyb.delayKey  = true;

            input.keyb.repeatCounter = 0;
            input.keyb.lastRepKey = SDL_SCANCODE_UP;
        }
        break;

        case SDL_SCANCODE_DOWN:
        {
            terminalScrollDown();

            if (!input.keyb.repeatKey)
                input.keyb.delayCounter = 0;

            input.keyb.repeatKey = true;
            input.keyb.delayKey  = true;

            input.keyb.repeatCounter = 0;
            input.keyb.lastRepKey = SDL_SCANCODE_DOWN;
        }
        break;

        case SDL_SCANCODE_HOME:terminalScrollToStart(); break;
        case SDL_SCANCODE_END: terminalScrollToEnd(); break;

        default:
        break;
    }
}

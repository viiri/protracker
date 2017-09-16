#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <SDL2/SDL.h>
#ifdef _WIN32
#define WIN32_MEAN_AND_LEAN
#include <windows.h>
#include <SDL2/SDL_syswm.h>
#else
#include <unistd.h> // chdir()
#endif
#include <sys/stat.h>
#include "pt_header.h"
#include "pt_helpers.h"
#include "pt_palette.h"
#include "pt_keyboard.h"
#include "pt_textout.h"
#include "pt_mouse.h"
#include "pt_diskop.h"
#include "pt_sampler.h"
#include "pt_config.h"
#include "pt_visuals.h"
#include "pt_edit.h"
#include "pt_modloader.h"
#include "pt_sampleloader.h"
#include "pt_terminal.h"
#include "pt_unicode.h"
#include "pt_scopes.h"
#include "pt_audio.h"

extern int8_t forceMixerOff; // pt_audio.c
extern uint32_t palette[PALETTE_NUM]; // pt_palette.c

uint8_t bigEndian;  // globalized
module_t *modEntry; // globalized

// accessed by pt_visuals.c
uint32_t *pixelBuffer  = NULL;
SDL_Window *window     = NULL;
SDL_Renderer *renderer = NULL;
SDL_Texture  *texture  = NULL;
uint8_t fullscreen = false, vsync60HzPresent = false;
// -----------------------------

#ifdef _WIN32
#define SYSMSG_FILE_ARG (WM_USER + 1)
#define ARGV_SHARED_MEM_MAX_LEN ((PATH_MAX_LEN * 2) + 2)

// for taking control over windows key and numlock on keyboard if app has focus
uint8_t windowsKeyIsDown;
HHOOK g_hKeyboardHook;
static HWND hWnd_to = NULL;
static HANDLE oneInstHandle = NULL, hMapFile = NULL;
static LPCTSTR sharedMemBuf = NULL;
static TCHAR sharedHwndName[] = TEXT("Local\\PTCloneHwnd");
static TCHAR sharedFileName[] = TEXT("Local\\PTCloneFilename");
static int8_t handleSingleInstancing(int32_t argc, char **argv);
static void handleSysMsg(SDL_Event inputEvent);
static LONG WINAPI exceptionHandler(EXCEPTION_POINTERS *ptr);
static uint8_t backupMadeAfterCrash;
#endif

static uint64_t timeNext64;
static SDL_TimerID timer50Hz;
static module_t *tempMod;

#ifdef __APPLE__
static void osxSetDirToProgramDirFromArgs(char **argv);
#endif
static void handleInput(void);
static int8_t initializeVars(void);
static void loadModFromArg(char *arg);
static void handleSigTerm(void);
static void loadDroppedFile(char *fullPath, uint32_t fullPathLen, uint8_t autoPlay);
static void cleanUp(void);
static void initSyncMainThread(void);
static void syncMainThread(void);
static void readMouseXY(void);

int main(int argc, char *argv[])
{
    SDL_version sdlVer;

    // very first thing to do is to set a big endian flag using a well-known hack
    // DO *NOT* run this test later in the code, as some macros depend on the flag!
    union
    {
        uint32_t a;
        uint8_t b[4];
    } endianTest;

    endianTest.a = 1;
    bigEndian = endianTest.b[3];
    // ----------------------------

#if SDL_PATCHLEVEL <= 4
    #pragma message("WARNING: The SDL2 dev lib is older than ver 2.0.5. You'll get fullscreen mode bugs.")
#endif

    SDL_GetVersion(&sdlVer);
    if (((sdlVer.major != SDL_MAJOR_VERSION) || (sdlVer.minor != SDL_MINOR_VERSION) || (sdlVer.patch != SDL_PATCHLEVEL)))
    {
#ifdef _WIN32
        showErrorMsgBox("SDL2.dll is not the correct version, and the program will terminate.\n\n" \
                        "Loaded dll version: %d.%d.%d\n" \
                        "Required dll version: %d.%d.%d",
                        sdlVer.major, sdlVer.minor, sdlVer.patch,
                        SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL);
#else
        showErrorMsgBox("The loaded SDL2 library is not the correct version, and the program will terminate.\n\n" \
                        "Loaded library version: %d.%d.%d\n" \
                        "Required library version: %d.%d.%d",
                        sdlVer.major, sdlVer.minor, sdlVer.patch,
                        SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL);
#endif
        return (0);
    }

#ifdef _WIN32
    if (IsProcessorFeaturePresent(PF_XMMI64_INSTRUCTIONS_AVAILABLE) == false)
    {
        showErrorMsgBox("Your computer's processor is too old and doesn't have the SSE2 instruction set\n" \
                        "which is needed for this program to run. Sorry!");
        return (0);
    }

    // for taking control over windows key and numlock on keyboard if app has focus
    windowsKeyIsDown = false;
    g_hKeyboardHook  = SetWindowsHookEx(WH_KEYBOARD_LL, lowLevelKeyboardProc, GetModuleHandle(NULL), 0);
    SetUnhandledExceptionFilter(exceptionHandler); // crash handler
#endif

#ifdef __APPLE__
    osxSetDirToProgramDirFromArgs(argv);
#endif

    if (!initializeVars())
    {
        cleanUp();
        SDL_Quit();

        return (1);
    }

    if (!loadConfig()) // returns false on mem alloc failure
    {
        cleanUp();
        SDL_Quit();

        return (1);
    }

    if (!setupVideo())
    {
        cleanUp();
        SDL_Quit();

        return (1);
    }

    // allow only one instance, and send arguments to it (song to play)
#ifdef _WIN32
    if (handleSingleInstancing(argc, argv))
    {
        cleanUp();
        SDL_Quit();
        return (0);
    }

    SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);
#endif

    if (!setupAudio())
    {
        cleanUp();
        SDL_Quit();

        return (1);
    }

    if (!terminalInit())
    {
        cleanUp();
        SDL_Quit();

        return (0);
    }

    if (!unpackBMPs())
    {
        cleanUp();
        SDL_Quit();

        return (1);
    }

    timer50Hz = SDL_AddTimer(1000 / 50, _50HzCallBack, NULL);
    if (timer50Hz == 0)
    {
        showErrorMsgBox("Couldn't create 50Hz timer:\n%s", SDL_GetError());

        cleanUp();
        SDL_Quit();

        return (1);
    }

    setupSprites();
    diskOpSetInitPath();

    editor.programRunning = true; // set this before initScopes()

    if (!initScopes())
    {
        cleanUp();
        SDL_Quit();

        return (1);
    }

    modEntry = createNewMod();
    if (modEntry == NULL)
    {
        cleanUp();
        SDL_Quit();

        return (1);
    }

    modSetTempo(editor.initialTempo);
    modSetSpeed(editor.initialSpeed);

    updateWindowTitle(MOD_NOT_MODIFIED);
    pointerSetMode(POINTER_MODE_IDLE, DO_CARRY);
    setStatusMessage(editor.allRightText, DO_CARRY);
    setStatusMessage("PROTRACKER V2.3D", NO_CARRY);

    terminalPrintf("Program was started.\n\n");
    terminalPrintf("Build date: %s %s\n", __DATE__, __TIME__);
    terminalPrintf("Platform endianness: %s\n\n", bigEndian ? "big-endian" : "little-endian");

    if (!editor.configFound)
        terminalPrintf("Warning: could not load config file, using default settings.\n\n");

    terminalPrintf("Configuration:\n");
    terminalPrintf("- Video upscaling factor: %dx\n", ptConfig.videoScaleFactor);
    terminalPrintf("- Video 60Hz vsync: %s\n", vsync60HzPresent ? "yes" : "no");
    terminalPrintf("- \"MOD.\" filenames: %s\n", ptConfig.modDot ? "yes" : "no");
    terminalPrintf("- Stereo separation: %d%%\n", ptConfig.stereoSeparation);
    terminalPrintf("- Audio BLEP synthesis: %s\n", ptConfig.blepSynthesis ? "yes" : "no");
    terminalPrintf("- Audio output rate: %dHz\n", ptConfig.soundFrequency);
    terminalPrintf("- Audio buffer size: %d samples\n", editor.audioBufferSize);
    terminalPrintf("- Audio latency: ~%.2fms\n", (editor.audioBufferSize / (float)(ptConfig.soundFrequency)) * 1000.0f);
    terminalPrintf("\nEverything is up and running.\n\n");

    // load a .MOD from the command arguments if passed (also ignore OS X < 10.9 -psn argument on double-click launch)
    if (((argc >= 2) && (strlen(argv[1]) > 0)) && !((argc == 2) && (!strncmp(argv[1], "-psn_", 5))))
    {
        loadModFromArg(argv[1]);

        // play song
        if (modEntry->moduleLoaded)
        {
            editor.playMode = PLAY_MODE_NORMAL;
            modPlay(DONT_SET_PATTERN, 0, DONT_SET_ROW);
            editor.currMode = MODE_PLAY;
            pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
            setStatusMessage(editor.allRightText, DO_CARRY);
        }
    }
    else
    {
        if (!editor.configFound)
            displayErrorMsg("CONFIG NOT FOUND!");
    }

    displayMainScreen();
    fillToVuMetersBgBuffer();
    updateCursorPos();

    SDL_ShowWindow(window);

    initSyncMainThread();
    while (editor.programRunning)
    {
        syncMainThread();
        readMouseXY();
        eraseSprites();
        updateKeyModifiers(); // set/clear CTRL/ALT/SHIFT/AMIGA key states
        handleInput();
        updateMouseCounters();
        handleKeyRepeat(input.keyb.lastRepKey);

        if (!input.mouse.buttonWaiting && (editor.ui.sampleMarkingPos == -1) &&
            !editor.ui.forceSampleDrag && !editor.ui.forceVolDrag &&
            !editor.ui.forceSampleEdit && !editor.ui.forceTermBarDrag)
        {
            handleMouseButtons();
            handleSamplerFiltersBoxRepeats();
        }

        renderFrame();
        renderSprites();
        flipFrame();

        sinkVisualizerBars();
    }

    cleanUp();
    SDL_Quit();

    return (0);
}

static void handleInput(void)
{
    char inputChar;
    SDL_Event inputEvent;

    while (SDL_PollEvent(&inputEvent))
    {
#ifdef _WIN32
        handleSysMsg(inputEvent);
#endif
        if (editor.ui.editTextFlag && (inputEvent.type == SDL_TEXTINPUT))
        {
            // text input when editing texts/numbers

            inputChar = inputEvent.text.text[0];
            if (inputChar == '\0')
                continue;

            handleTextEditInputChar(inputChar);
            continue; // continue SDL event loop
        }
        else if (inputEvent.type == SDL_MOUSEWHEEL)
        {
            if (inputEvent.wheel.y < 0)
                mouseWheelDownHandler();
            else if (inputEvent.wheel.y > 0)
                mouseWheelUpHandler();
        }
        else if (inputEvent.type == SDL_DROPFILE)
        {
            loadDroppedFile(inputEvent.drop.file, strlen(inputEvent.drop.file), false);
            SDL_free(inputEvent.drop.file);
            SDL_RaiseWindow(window); // set window focus
        }
        if (inputEvent.type == SDL_QUIT)
        {
            handleSigTerm();
        }
        else if (inputEvent.type == SDL_KEYUP)
        {
            keyUpHandler(inputEvent.key.keysym.scancode);
        }
        else if (inputEvent.type == SDL_KEYDOWN)
        {
            if (editor.repeatKeyFlag || (input.keyb.lastRepKey != inputEvent.key.keysym.scancode))
                keyDownHandler(inputEvent.key.keysym.scancode, inputEvent.key.keysym.sym);
        }
        else if (inputEvent.type == SDL_MOUSEBUTTONUP)
        {
            mouseButtonUpHandler(inputEvent.button.button);

            if (!editor.ui.askScreenShown && !editor.ui.terminalShown && editor.ui.introScreenShown)
            {
                if (editor.ui.terminalWasClosed)
                {
                    // bit of a kludge to prevent terminal exit from closing intro screen
                    editor.ui.terminalWasClosed = false;
                }
                else
                {
                    if (!editor.ui.clearScreenShown)
                        setStatusMessage(editor.allRightText, DO_CARRY);

                    editor.ui.introScreenShown = false;
                }
            }
        }
        else if (inputEvent.type == SDL_MOUSEBUTTONDOWN)
        {
            if ((editor.ui.sampleMarkingPos == -1) &&
                !editor.ui.forceSampleDrag && !editor.ui.forceVolDrag &&
                !editor.ui.forceSampleEdit && !editor.ui.forceTermBarDrag)
            {
                mouseButtonDownHandler(inputEvent.button.button);
            }
        }

        if (editor.ui.throwExit)
        {
            editor.programRunning = false;

            if (editor.diskop.isFilling)
            {
                editor.diskop.isFilling = false;

                editor.diskop.forceStopReading = true;
                SDL_WaitThread(editor.diskop.fillThread, NULL);
            }

            if (editor.isWAVRendering)
            {
                editor.isWAVRendering = false;
                editor.abortMod2Wav   = true;
                SDL_WaitThread(editor.mod2WavThread, NULL);
            }
        }
    }
}

static int8_t initializeVars(void)
{
    clearPaulaAndScopes();

    // clear common structs
    memset(&input,    0, sizeof (input));
    memset(&editor,   0, sizeof (editor));
    memset(&ptConfig, 0, sizeof (ptConfig));

    modEntry = NULL;

    // copy often used strings
    strcpy(editor.mixText,           "MIX 01+02 TO 03");
    strcpy(editor.allRightText,      "ALL RIGHT");
    strcpy(editor.modLoadOoMText,    "Module loading failed: out of memory!\n");
    strcpy(editor.outOfMemoryText,   "OUT OF MEMORY !!!");
    strcpy(editor.diskOpListOoMText, "Failed to list directory: out of memory!\n");

    // allocate memory (if initializeVars() returns false, every allocations are free'd)
    if (!allocSamplerVars())
    {
        showErrorMsgBox("Out of memory!");
        return (false);
    }

    if (!allocDiskOpVars())
    {
        showErrorMsgBox("Out of memory!");
        return (false);
    }

    ptConfig.defaultDiskOpDir = (char *)(malloc(PATH_MAX_LEN + 1));
    if (ptConfig.defaultDiskOpDir == NULL)
    {
        showErrorMsgBox("Out of memory!");
        return (false);
    }

    editor.rowVisitTable = (uint8_t *)(malloc(MOD_ORDERS * MOD_ROWS));
    if (editor.rowVisitTable == NULL)
    {
        showErrorMsgBox("Out of memory!");
        return (false);
    }

    editor.ui.pattNames = (char *)(calloc(MAX_PATTERNS, 16));
    if (editor.ui.pattNames == NULL)
    {
        showErrorMsgBox("Out of memory!");
        return (false);
    }

    editor.scopeBuffer = (uint32_t *)(malloc(200 * 44));
    if (editor.scopeBuffer == NULL)
    {
        showErrorMsgBox("Out of memory!");
        return (false);
    }

    editor.tempSample = (int8_t *)(calloc(MAX_SAMPLE_LEN, 1));
    if (editor.tempSample == NULL)
    {
        showErrorMsgBox("Out of memory!");
        return (false);
    }

    // setup initial mouse coordinates
    input.mouse.x = SCREEN_W / 2;
    input.mouse.y = SCREEN_H / 2;
    input.mouse.prevX = input.mouse.x;
    input.mouse.prevY = input.mouse.y;

    // set various non-zero values
    editor.vol1 = 100;
    editor.vol2 = 100;
    editor.note1 = 36;
    editor.note2 = 36;
    editor.note3 = 36;
    editor.note4 = 36;
    editor.f7Pos = 16;
    editor.f8Pos = 32;
    editor.f9Pos = 48;
    editor.f10Pos = 63;
    editor.oldNote1 = 36;
    editor.oldNote2 = 36;
    editor.oldNote3 = 36;
    editor.oldNote4 = 36;
    editor.tuningVol = 32;
    editor.sampleVol = 100;
    editor.tuningNote = 24;
    editor.metroSpeed = 4;
    editor.editMoveAdd = 1;
    editor.initialTempo = 125;
    editor.initialSpeed = 6;
    editor.resampleNote = 24;
    editor.currPlayNote = 24;
    editor.quantizeValue = 1;
    editor.effectMacros[0] = 0x0102;
    editor.effectMacros[1] = 0x0202;
    editor.effectMacros[2] = 0x0037;
    editor.effectMacros[3] = 0x0047;
    editor.effectMacros[4] = 0x0304;
    editor.effectMacros[5] = 0x0F06;
    editor.effectMacros[6] = 0x0C10;
    editor.effectMacros[7] = 0x0C20;
    editor.effectMacros[8] = 0x0E93;
    editor.effectMacros[9] = 0x0A0F;
    editor.multiModeNext[0] = 2;
    editor.multiModeNext[1] = 3;
    editor.multiModeNext[2] = 4;
    editor.multiModeNext[3] = 1;
    editor.ui.visualizerMode = VISUAL_QUADRASCOPE;
    editor.ui.introScreenShown = true;
    editor.ui.sampleMarkingPos = -1;
    editor.ui.previousPointerMode = editor.ui.pointerMode;

    // setup GUI text pointers
    editor.vol1Disp          = &editor.vol1;
    editor.vol2Disp          = &editor.vol2;
    editor.sampleToDisp      = &editor.sampleTo;
    editor.lpCutOffDisp      = &editor.lpCutOff;
    editor.hpCutOffDisp      = &editor.hpCutOff;
    editor.samplePosDisp     = &editor.samplePos;
    editor.sampleVolDisp     = &editor.sampleVol;
    editor.currSampleDisp    = &editor.currSample;
    editor.metroSpeedDisp    = &editor.metroSpeed;
    editor.sampleFromDisp    = &editor.sampleFrom;
    editor.chordLengthDisp   = &editor.chordLength;
    editor.metroChannelDisp  = &editor.metroChannel;
    editor.quantizeValueDisp = &editor.quantizeValue;

    return (true);
}

static void loadModFromArg(char *arg)
{
    uint32_t filenameLen;
    UNICHAR *filenameU;

    editor.ui.introScreenShown = false;
    setStatusMessage(editor.allRightText, DO_CARRY);

    filenameLen = strlen(arg);

    filenameU = (UNICHAR *)(calloc((filenameLen + 2), sizeof (UNICHAR)));
    if (filenameU == NULL)
    {
        displayErrorMsg(editor.outOfMemoryText);
        terminalPrintf(editor.modLoadOoMText);

        return;
    }

#ifdef _WIN32
    MultiByteToWideChar(CP_UTF8, 0, arg, -1, filenameU, filenameLen);
#else
    strcpy(filenameU, arg);
#endif

    tempMod = modLoad(filenameU);
    if (tempMod != NULL)
    {
        modEntry->moduleLoaded = false;
        modFree();
        modEntry = tempMod;
        setupNewMod();
        modEntry->moduleLoaded = true;
    }
    else
    {
        editor.errorMsgActive  = true;
        editor.errorMsgBlock   = true;
        editor.errorMsgCounter = 0;

        // status/error message is set in the mod loader
        pointerErrorMode();
    }

    free(filenameU);
}

void resetAllScreens(void)
{
    editor.mixFlag                = false;
    editor.swapChannelFlag        = false;
    editor.ui.clearScreenShown    = false;
    editor.ui.changingChordNote   = false;
    editor.ui.changingSmpResample = false;
    editor.ui.pat2SmpDialogShown  = false;
    editor.ui.disablePosEd        = false;
    editor.ui.disableVisualizer   = false;

    if (editor.ui.terminalShown)
    {
        editor.ui.terminalShown = false;
        removeTerminalScreen();

        updateDiskOp();
        updatePosEd();
        updateEditOp();
        updateVisualizer();
    }
    else if (editor.ui.samplerScreenShown)
    {
        editor.ui.samplerVolBoxShown     = false;
        editor.ui.samplerFiltersBoxShown = false;

        displaySample();
    }

    if (editor.ui.editTextFlag)
        exitGetTextLine(EDIT_TEXT_NO_UPDATE);
}

static void handleSigTerm(void)
{
    if (modEntry->modified)
    {
        resetAllScreens();

        editor.ui.askScreenShown = true;
        editor.ui.askScreenType = ASK_QUIT;

        pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
        setStatusMessage("REALLY QUIT ?", NO_CARRY);
        renderAskDialog();
    }
    else
    {
        editor.ui.throwExit = true;
    }
}

// Windows specific routines
#ifdef _WIN32
static int8_t instanceAlreadyOpen(void)
{
    hMapFile = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, sharedHwndName);
    if (hMapFile != NULL)
        return (true); /* another instance is already open */

    /* no instance is open, let's created a shared memory file with hWnd in it */
    oneInstHandle = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof (HWND), sharedHwndName);
    if (oneInstHandle != NULL)
    {
        sharedMemBuf = (LPTSTR)(MapViewOfFile(oneInstHandle, FILE_MAP_ALL_ACCESS, 0, 0, sizeof (HWND)));
        if (sharedMemBuf != NULL)
        {
            CopyMemory((PVOID)(sharedMemBuf), &editor.ui.hWnd, sizeof (HWND));
            UnmapViewOfFile(sharedMemBuf); sharedMemBuf = NULL;
        }
    }

    return (false);
}

static int8_t handleSingleInstancing(int32_t argc, char **argv)
{
    if (instanceAlreadyOpen())
    {
        if ((argc >= 2) && (strlen(argv[1]) > 0))
        {
            sharedMemBuf = (LPTSTR)(MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, sizeof (HWND)));
            if (sharedMemBuf != NULL)
            {
                memcpy(&hWnd_to, sharedMemBuf, sizeof (HWND));
                UnmapViewOfFile(sharedMemBuf); sharedMemBuf = NULL;
                CloseHandle(hMapFile); hMapFile = NULL;

                hMapFile = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, ARGV_SHARED_MEM_MAX_LEN, sharedFileName);
                if (hMapFile != NULL)
                {
                    sharedMemBuf = (LPTSTR)(MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, ARGV_SHARED_MEM_MAX_LEN));
                    if (sharedMemBuf != NULL)
                    {
                        strcpy((char *)(sharedMemBuf), argv[1]);
                        UnmapViewOfFile(sharedMemBuf); sharedMemBuf = NULL;

                        SendMessageA(hWnd_to, SYSMSG_FILE_ARG, 0, 0);
                        SDL_Delay(100); // wait a bit to make sure first instance received msg

                        CloseHandle(hMapFile); hMapFile = NULL;

                        return (true); // quit instance now
                    }
                }

                return (true);
            }

            CloseHandle(hMapFile); hMapFile = NULL;
        }
    }

    return (false);
}

static void handleSysMsg(SDL_Event inputEvent)
{
    SDL_SysWMmsg *wmMsg;

    if (inputEvent.type == SDL_SYSWMEVENT)
    {
        wmMsg = inputEvent.syswm.msg;
        if ((wmMsg->subsystem == SDL_SYSWM_WINDOWS) && (wmMsg->msg.win.msg == SYSMSG_FILE_ARG))
        {
            hMapFile = OpenFileMappingA(FILE_MAP_READ, FALSE, sharedFileName);
            if (hMapFile != NULL)
            {
                sharedMemBuf = (LPTSTR)(MapViewOfFile(hMapFile, FILE_MAP_READ, 0, 0, ARGV_SHARED_MEM_MAX_LEN));
                if (sharedMemBuf != NULL)
                {
                    loadDroppedFile((char *)(sharedMemBuf), strlen(sharedMemBuf), true);
                    UnmapViewOfFile(sharedMemBuf); sharedMemBuf = NULL;

                    SDL_RestoreWindow(window);
                }

                CloseHandle(hMapFile); hMapFile = NULL;
            }
        }
    }
}

static LONG WINAPI exceptionHandler(EXCEPTION_POINTERS *ptr)
{
#define BACKUP_FILES_TO_TRY 1000

    char filePath[MAX_PATH], fileName[MAX_PATH], *filePathPtr;
    uint16_t i;
    uint32_t fileNameLength;
    struct stat statBuffer;

    (void)(ptr);

    // Important: Try to clear the "only one" instance handle!
    if (oneInstHandle != NULL)
    {
        CloseHandle(oneInstHandle);
        oneInstHandle = NULL;
    }

    if (backupMadeAfterCrash == false)
    {
        // get executable path
        GetModuleFileNameA(NULL, filePath, MAX_PATH);

        // cut off executable
        fileNameLength = strlen(filePath);
        if (fileNameLength)
        {
            filePathPtr = filePath + fileNameLength;
            while (filePathPtr != filePath)
            {
                if (*--filePathPtr == '\\')
                {
                    *(filePathPtr++) = '\0';
                    break;
                }
            }

            // find a free filename
            for (i = 1; i < 1000; ++i)
            {
                sprintf(fileName, "%s\\BACKUP%03d.MOD", filePath, i);
                if (stat(fileName, &statBuffer) != 0)
                    break; // filename OK
            }

            if (i != 1000)
                modSave(fileName);
        }

        backupMadeAfterCrash = true;

        showErrorMsgBox("Oh no!\nProTracker has crashed...\n\nA backup .MOD was hopefully " \
                        "saved to the program directory.\n\nPlease report this to 8bitbubsy " \
                        "(IRC or olav.sorensen@live.no).\nTry to mention what you did before the crash happened.");
    }

    return (EXCEPTION_CONTINUE_SEARCH);
}
#endif

// DRAG AND DROP RELATED ROUTINES
static uint8_t testExtension(char *ext, uint8_t extLen, char *fullPath)
{
    // checks for EXT.filename and filename.EXT
    char *fileName, begStr[8], endStr[8];
    uint32_t fileNameLen;

    extLen++; // add one to length (dot)

    fileName = strrchr(fullPath, DIR_DELIMITER);
    if (fileName != NULL)
        fileName++;
    else
        fileName = fullPath;

    fileNameLen = strlen(fileName);
    if (fileNameLen >= extLen)
    {
        sprintf(begStr, "%s.", ext);
        if (!my_strnicmp(begStr, fileName, extLen))
            return (true);

        sprintf(endStr, ".%s", ext);
        if (!my_strnicmp(endStr, fileName + (fileNameLen - extLen), extLen))
            return (true);
    }

    return (false);
}

static void loadDroppedFile(char *fullPath, uint32_t fullPathLen, uint8_t autoPlay)
{
    char *fileName, *ansiName;
    uint8_t isMod, songWasPlaying;
    UNICHAR *fullPathU;

    if (editor.diskop.isFilling || editor.isWAVRendering)
        return;

    ansiName = (char *)(calloc(fullPathLen + 10, sizeof (char)));
    if (ansiName == NULL)
    {
        displayErrorMsg(editor.outOfMemoryText);
        terminalPrintf(editor.modLoadOoMText);

        return;
    }

    fullPathU = (UNICHAR *)(calloc((fullPathLen + 2), sizeof (UNICHAR)));
    if (fullPathU == NULL)
    {
        displayErrorMsg(editor.outOfMemoryText);
        terminalPrintf(editor.modLoadOoMText);

        return;
    }

#ifdef _WIN32
    MultiByteToWideChar(CP_UTF8, 0, fullPath, -1, fullPathU, fullPathLen);
#else
    strcpy(fullPathU, fullPath);
#endif

    unicharToAnsi(ansiName, fullPathU, fullPathLen);

    // make a new pointer point to filename (strip path)
    fileName = strrchr(ansiName, DIR_DELIMITER);
    if (fileName != NULL)
        fileName++;
    else
        fileName = ansiName;

    // check if the file extension is a module (FIXME: check module by content instead..?)
    isMod = false;
    if (testExtension("MOD", 3, fileName))
        isMod = true;
    else if (testExtension("M15", 3, fileName))
        isMod = true;
    else if (testExtension("STK", 3, fileName))
        isMod = true;
    else if (testExtension("NST", 3, fileName))
        isMod = true;
    else if (testExtension("UST", 3, fileName))
        isMod = true;
    else if (testExtension("PP", 2, fileName))
        isMod = true;
    else if (testExtension("NT", 2, fileName))
        isMod = true;

    if (isMod)
    {
        songWasPlaying = editor.songPlaying;

        tempMod = modLoad(fullPathU);
        if (tempMod != NULL)
        {
            modStop();
            modEntry->moduleLoaded = false;
            modFree();
            modEntry = tempMod;
            setupNewMod();
            modEntry->moduleLoaded = true;

            resetAllScreens();
            setStatusMessage("ALL RIGHT", DO_CARRY);
            pointerSetMode(POINTER_MODE_IDLE, DO_CARRY);

            editor.ui.diskOpScreenShown = false;
            displayMainScreen();

            if (editor.ui.samplerScreenShown)
            {
                editor.ui.samplerScreenShown = false;
                samplerScreen();
            }

            if (songWasPlaying || autoPlay)
            {
                editor.playMode = PLAY_MODE_NORMAL;
                modPlay(DONT_SET_PATTERN, 0, DONT_SET_ROW);
                editor.currMode = MODE_PLAY;
                pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
                setStatusMessage(editor.allRightText, DO_CARRY);
            }
        }
        else
        {
            editor.errorMsgActive  = true;
            editor.errorMsgBlock   = true;
            editor.errorMsgCounter = 0;

            // status/error message is set in the mod loader
            pointerErrorMode();
        }
    }
    else
    {
        loadSample(fullPathU, fileName);
    }

    free(ansiName);
    free(fullPathU);
}

static void cleanUp(void) // never call this inside the main loop!
{
    audioClose();

    if (timer50Hz != 0)
        SDL_RemoveTimer(timer50Hz);

    modFree();

    if (editor.rowVisitTable != NULL) free(editor.rowVisitTable);
    if (editor.ui.pattNames  != NULL) free(editor.ui.pattNames);
    if (editor.tempSample    != NULL) free(editor.tempSample);
    if (editor.scopeBuffer   != NULL) free(editor.scopeBuffer);

    deAllocSamplerVars();
    deAllocDiskOpVars();
    freeDiskOpFileMem();
    freeBMPs();
    terminalFree();

    if (ptConfig.defaultDiskOpDir != NULL) free(ptConfig.defaultDiskOpDir);
    videoClose();
    freeSprites();

#ifdef _WIN32
    UnhookWindowsHookEx(g_hKeyboardHook);
    if (oneInstHandle != NULL) CloseHandle(oneInstHandle);
#endif
}

static void initSyncMainThread(void)
{
    double perfFreq_f, frameLength_f;

    perfFreq_f    = (double)(SDL_GetPerformanceFrequency());
    frameLength_f = (perfFreq_f / VBLANK_HZ) + 0.5;
    timeNext64    = SDL_GetPerformanceCounter() + (int32_t)(frameLength_f);
}

static void syncMainThread(void)
{
    // this routine almost never delays if we have 60Hz vsync, but it's still needed for safety

    int32_t delayMs;
    uint64_t timeNow64, timeElapsed64;
    double delayMs_f, perfFreq_f, frameLength_f;

    perfFreq_f = (double)(SDL_GetPerformanceFrequency()); // should be safe for double
    if (perfFreq_f <= 0.0)
        return; // panic!

    timeNow64 = SDL_GetPerformanceCounter();
    if (timeNext64 > timeNow64)
    {
        timeElapsed64 = timeNext64 - timeNow64;

        delayMs_f = ((1000.0 * (double)(timeElapsed64)) / perfFreq_f) + 0.5;
        delayMs   = (int32_t)(delayMs_f);

        if (delayMs > 0)
            SDL_Delay(delayMs);
    }

    frameLength_f = (perfFreq_f / VBLANK_HZ) + 0.5;
    timeNext64   += (int32_t)(frameLength_f);
}

static void readMouseXY(void)
{
    int16_t x, y;
    int32_t mx, my;
    float mx_f, my_f;

    SDL_PumpEvents();
    SDL_GetMouseState(&mx, &my);

    if (input.mouse.scaleX_f != 1.0f)
    {
        mx_f = mx * input.mouse.scaleX_f;
        mx = (int32_t)(mx_f + 0.5f);
    }

    if (input.mouse.scaleY_f != 1.0f)
    {
        my_f = my * input.mouse.scaleY_f;
        my = (int32_t)(my_f + 0.5f);
    }

    // clamp to edges
    mx = CLAMP(mx, 0, SCREEN_W - 1);
    my = CLAMP(my, 0, SCREEN_H - 1);

    x = (int16_t)(mx);
    y = (int16_t)(my);

    input.mouse.x = x;
    input.mouse.y = y;

    setSpritePos(SPRITE_MOUSE_POINTER, x, y);
}

#ifdef __APPLE__
static void osxSetDirToProgramDirFromArgs(char **argv)
{
    char *tmpPath;
    int32_t i, tmpPathLen;

    /* OS X/macOS: hackish way of setting the current working directory to the place where we double clicked
    ** on the icon (for protracker.ini loading)
    */

    // if we launched from the terminal, argv[0][0] would be '.'
    if ((argv[0] != NULL) && (argv[0][0] == DIR_DELIMITER)) // don't do the hack if we launched from the terminal
    {
        tmpPath = strdup(argv[0]);
        if (tmpPath != NULL)
        {
            // cut off program filename
            tmpPathLen = strlen(tmpPath);
            for (i = tmpPathLen - 1; i >= 0; --i)
            {
                if (tmpPath[i] == DIR_DELIMITER)
                {
                    tmpPath[i] = '\0';
                    break;
                }
            }

            chdir(tmpPath);     // path to binary
            chdir("../../../"); // we should now be in the directory where the config can be.

            free(tmpPath);
        }
    }
}
#endif

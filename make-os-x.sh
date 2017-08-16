#!/bin/bash

arch=$(arch)
if [ $arch == "ppc" ]; then
    echo Sorry, PowerPC \(PPC\) is not supported...
else
    echo Compiling 32-bit and 64-bit compatible binary, please wait...
    
    rm release/protracker-osx.app/Contents/MacOS/protracker &> /dev/null
    
    clang -mmacosx-version-min=10.6 -arch i386 -arch x86_64 -mfpmath=sse -msse2 -I/Library/Frameworks/SDL2.framework/Headers -F/Library/Frameworks src/*.c src/gfx/*.c -O3 -lm -Wall -Winit-self -Wextra -Wunused -Wredundant-decls -Wswitch-default -framework SDL2 -framework Cocoa -lm -o release/protracker-osx.app/Contents/MacOS/protracker
    install_name_tool -change @rpath/SDL2.framework/Versions/A/SDL2 @executable_path/../Frameworks/SDL2.framework/Versions/A/SDL2 release/protracker-osx.app/Contents/MacOS/protracker
    
    rm src/*.o src/gfx/*.o &> /dev/null
    echo Done! The binary \(protracker-osx.app\) is in the folder named \'release\'.
fi

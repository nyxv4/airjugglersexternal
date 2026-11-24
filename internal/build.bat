@echo off
rem build NYX internal (dll + injector) with MSVC BuildTools
setlocal
set VCVARS="C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist %VCVARS% (
    echo vcvars64.bat not found
    exit /b 1
)
call %VCVARS% >nul
if not exist build mkdir build

cl /nologo /O2 /MT /EHsc /std:c++17 ^
   /I..\include ^
   dllmain.cpp ..\src\game\offsets.cpp ..\src\core\http.cpp ^
   /LD /Fe:build\NyxInternal.dll /Fo:build\ ^
   wininet.lib || exit /b 1

cl /nologo /O2 /MT /EHsc /std:c++17 ^
   injector.cpp manualmap.cpp /Fe:build\NyxInjector.exe /Fo:build\ advapi32.lib || exit /b 1

echo.
echo built: build\NyxInternal.dll + build\NyxInjector.exe
echo inject while in-game:  NyxInjector.exe C:\path\to\NyxInternal.dll
endlocal

@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /EHsc /O2 /std:c++17 /I ..\include cfgtest.cpp ..\src\menu\settings.cpp /Fe:cfgtest.exe
cfgtest.exe

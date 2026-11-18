@echo off
rem Compila o WinMac com o MSVC (Visual Studio 2022 ou Build Tools). Saída: build\WinMac.exe e dist\WinMac-Setup.exe
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VS="
if exist "%VSWHERE%" for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS=%%i"
if not defined VS (echo Visual Studio C++ nao encontrado. & exit /b 1)
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0"
if not exist build mkdir build
if not exist dist mkdir dist
rc /nologo /fo build\app.res res\app.rc || exit /b 1
cl /nologo /std:c++20 /O2 /GL /Gy /MT /EHsc /W3 /DUNICODE /D_UNICODE /utf-8 /Fobuild\ /Fe:build\WinMac.exe src\*.cpp build\app.res ^
  /link /LTCG /OPT:REF /OPT:ICF /SUBSYSTEM:WINDOWS user32.lib gdi32.lib dwmapi.lib d2d1.lib dwrite.lib windowscodecs.lib shell32.lib ^
  ole32.lib oleaut32.lib shlwapi.lib propsys.lib winhttp.lib comctl32.lib advapi32.lib wlanapi.lib shcore.lib version.lib wbemuuid.lib ^
  iphlpapi.lib bcrypt.lib powrprof.lib windowsapp.lib || exit /b 1
copy /y build\WinMac.exe dist\WinMac-Setup.exe >nul
echo OK: dist\WinMac-Setup.exe

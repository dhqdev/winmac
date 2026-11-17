@echo off
rem Publica uma versão nova do WinMac nas Releases do GitHub.
rem Antes: aumente a versão em src\version.h. Precisa do GitHub CLI (winget install GitHub.cli) logado: gh auth login
setlocal
cd /d "%~dp0.."
for /f "tokens=3 delims= " %%v in ('findstr /c:"define WINMAC_VERSION_STR" src\version.h') do set V=%%~v
if not defined V (echo Versao nao encontrada em src\version.h & exit /b 1)
call build.cmd || exit /b 1
git tag v%V% 2>nul
git push origin main --tags || exit /b 1
gh release create v%V% dist\WinMac-Setup.exe --title "WinMac %V%" --generate-notes || exit /b 1
echo Publicado: WinMac %V%

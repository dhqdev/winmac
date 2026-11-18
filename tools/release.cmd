@echo off
rem As releases agora sao automaticas: aumente a versao em src\version.h e envie para a main.
rem O GitHub Actions (.github\workflows\build.yml) compila, cria a tag e publica o WinMac-Setup.exe.
cd /d "%~dp0.."
git push origin main

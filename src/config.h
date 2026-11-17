#pragma once
#include "common.h"

// Configuração em %LOCALAPPDATA%\WinMac\config.ini (UTF-16, editável no Bloco de Notas, mas o app Ajustes cuida disso).
namespace cfg {
void Init(const std::wstring& dirOverride = L"");
std::wstring Path();
bool Has(const wchar_t* sec, const wchar_t* key);
std::wstring Str(const wchar_t* sec, const wchar_t* key, const wchar_t* def = L"");
int Int(const wchar_t* sec, const wchar_t* key, int def);
float Float(const wchar_t* sec, const wchar_t* key, float def);
void Set(const wchar_t* sec, const wchar_t* key, const std::wstring& v);
void SetInt(const wchar_t* sec, const wchar_t* key, int v);
void SetFloat(const wchar_t* sec, const wchar_t* key, float v);
std::vector<std::wstring> List(const wchar_t* sec, const wchar_t* key);
void SetList(const wchar_t* sec, const wchar_t* key, const std::vector<std::wstring>& v);
}

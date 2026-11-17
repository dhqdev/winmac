#pragma once
#include "common.h"

// HTTP(S) simples com WinHTTP e um leitor de JSON mínimo.
namespace net {
// GET de uma URL https. Retorna o corpo; status recebe o código HTTP (0 se falhou a conexão).
std::string Get(const std::wstring& url, int* status = nullptr, const std::wstring& extraHeaders = L"");
// Baixa para um arquivo, chamando progress(baixado, total) de vez em quando. cancel pode interromper.
bool Download(const std::wstring& url, const std::wstring& file, const std::function<void(uint64_t, uint64_t)>& progress,
              const std::atomic<bool>* cancel = nullptr);
std::wstring UrlEncode(const std::wstring& s);
std::string Sha256File(const std::wstring& file);   // hex minúsculo, vazio se falhar

struct JVal {
    enum T { Null, Num, Str, Arr, Obj, Bool } t = Null;
    double n = 0;
    std::string s;   // UTF-8
    std::vector<JVal> a;
    std::vector<std::pair<std::string, JVal>> o;
    const JVal* Get(const char* k) const;
    std::wstring W() const { return Utf8ToW(s); }
    double Number(double def = 0) const { return t == JVal::Num ? n : def; }
};
bool ParseJson(const std::string& text, JVal& out);
}

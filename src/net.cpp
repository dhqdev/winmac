#include "net.h"
#include "version.h"
#include <winhttp.h>
#include <bcrypt.h>

namespace net {
namespace {
struct Handles {
    HINTERNET s = nullptr, c = nullptr, r = nullptr;
    ~Handles() {
        if (r) WinHttpCloseHandle(r);
        if (c) WinHttpCloseHandle(c);
        if (s) WinHttpCloseHandle(s);
    }
};

bool Open(const std::wstring& url, const std::wstring& headers, Handles& h, int* status) {
    URL_COMPONENTS uc{ sizeof(uc) };
    wchar_t host[256] = {}, path[2048] = {};
    uc.lpszHostName = host;
    uc.dwHostNameLength = 255;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 2047;
    wchar_t extra[2048] = {};
    uc.lpszExtraInfo = extra;
    uc.dwExtraInfoLength = 2047;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return false;
    std::wstring full = std::wstring(path) + extra;
    std::wstring ua = L"WinMac/" WINMAC_VERSION;
    h.s = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!h.s) h.s = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!h.s) return false;
    WinHttpSetTimeouts(h.s, 8000, 8000, 15000, 30000);
    h.c = WinHttpConnect(h.s, host, uc.nPort, 0);
    if (!h.c) return false;
    DWORD flags = uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    h.r = WinHttpOpenRequest(h.c, L"GET", full.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!h.r) return false;
    // sem descompressão automática: em algumas versões do Windows ela devolvia o corpo vazio

    if (!WinHttpSendRequest(h.r, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(), headers.empty() ? 0 : (DWORD)-1L,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
        return false;
    if (!WinHttpReceiveResponse(h.r, nullptr)) return false;
    DWORD code = 0, sz = sizeof(code);
    WinHttpQueryHeaders(h.r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz, WINHTTP_NO_HEADER_INDEX);
    if (status) *status = (int)code;
    return true;
}
}  // namespace

std::string Get(const std::wstring& url, int* status, const std::wstring& extraHeaders) {
    std::string out;
    if (status) *status = 0;
    Handles h;
    if (!Open(url, extraHeaders, h, status)) return out;
    // lê até o fim (com descompressão ligada, "dados disponíveis" pode vir 0 antes do fim)
    std::vector<char> buf(64 * 1024);
    for (;;) {
        DWORD rd = 0;
        if (!WinHttpReadData(h.r, buf.data(), (DWORD)buf.size(), &rd) || rd == 0) break;
        out.append(buf.data(), rd);
        if (out.size() > 8'000'000) break;
    }
    return out;
}

bool Download(const std::wstring& url, const std::wstring& file, const std::function<void(uint64_t, uint64_t)>& progress,
              const std::atomic<bool>* cancel) {
    Handles h;
    int status = 0;
    if (!Open(url, L"", h, &status) || status != 200) return false;
    uint64_t total = 0;
    wchar_t len[32] = {};
    DWORD sz = sizeof(len);
    if (WinHttpQueryHeaders(h.r, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, len, &sz, WINHTTP_NO_HEADER_INDEX)) total = _wcstoui64(len, nullptr, 10);
    HANDLE f = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    uint64_t got = 0;
    bool ok = true;
    std::vector<char> buf(64 * 1024);
    DWORD tick = 0;
    for (;;) {
        if (cancel && *cancel) { ok = false; break; }
        DWORD rd = 0;
        if (!WinHttpReadData(h.r, buf.data(), (DWORD)buf.size(), &rd)) { ok = false; break; }
        if (rd == 0) break;
        DWORD wr = 0;
        if (!WriteFile(f, buf.data(), rd, &wr, nullptr) || wr != rd) { ok = false; break; }
        got += rd;
        if (progress && GetTickCount() - tick > 80) { tick = GetTickCount(); progress(got, total); }
    }
    CloseHandle(f);
    if (ok && total && got != total) ok = false;
    if (!ok) DeleteFileW(file.c_str());
    else if (progress) progress(got, total ? total : got);
    return ok;
}

std::wstring UrlEncode(const std::wstring& s) {
    std::string u = WToUtf8(s);
    std::wstring r;
    for (unsigned char ch : u) {
        if (isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') r += (wchar_t)ch;
        else {
            wchar_t b[4];
            swprintf_s(b, L"%%%02X", ch);
            r += b;
        }
    }
    return r;
}

std::string Sha256File(const std::wstring& file) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hh = nullptr;
    std::string out;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return out;
    if (BCryptCreateHash(alg, &hh, nullptr, 0, nullptr, 0, 0) == 0) {
        HANDLE f = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            std::vector<BYTE> buf(64 * 1024);
            DWORD rd = 0;
            bool ok = true;
            while (ReadFile(f, buf.data(), (DWORD)buf.size(), &rd, nullptr) && rd)
                if (BCryptHashData(hh, buf.data(), rd, 0) != 0) { ok = false; break; }
            CloseHandle(f);
            BYTE dig[32];
            if (ok && BCryptFinishHash(hh, dig, 32, 0) == 0) {
                char hx[3];
                for (BYTE b : dig) { sprintf_s(hx, "%02x", b); out += hx; }
            }
        }
        BCryptDestroyHash(hh);
    }
    BCryptCloseAlgorithmProvider(alg, 0);
    return out;
}

// ---------------- JSON ----------------
const JVal* JVal::Get(const char* k) const {
    for (auto& p : o)
        if (p.first == k) return &p.second;
    return nullptr;
}

namespace {
struct Parser {
    const char* p;
    const char* e;
    int depth = 0;
    void Ws() { while (p < e && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) ++p; }
    bool Lit(const char* s) {
        size_t n = strlen(s);
        if ((size_t)(e - p) < n || strncmp(p, s, n) != 0) return false;
        p += n;
        return true;
    }
    static void PutUtf8(std::string& o, unsigned cp) {
        if (cp < 0x80) o += (char)cp;
        else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
        else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
    }
    unsigned Hex4() {
        if (e - p < 4) return 0;
        unsigned v = 0;
        for (int i = 0; i < 4; ++i) {
            char ch = *p++;
            v <<= 4;
            if (ch >= '0' && ch <= '9') v |= ch - '0';
            else if (ch >= 'a' && ch <= 'f') v |= ch - 'a' + 10;
            else if (ch >= 'A' && ch <= 'F') v |= ch - 'A' + 10;
        }
        return v;
    }
    bool Str(std::string& o) {
        ++p;  // "
        while (p < e && *p != '"') {
            if (*p == '\\' && p + 1 < e) {
                ++p;
                char ch = *p++;
                switch (ch) {
                case 'n': o += '\n'; break;
                case 't': o += '\t'; break;
                case 'r': o += '\r'; break;
                case 'b': o += '\b'; break;
                case 'f': o += '\f'; break;
                case 'u': {
                    unsigned cp = Hex4();
                    if (cp >= 0xD800 && cp < 0xDC00 && e - p >= 6 && p[0] == '\\' && p[1] == 'u') {
                        p += 2;
                        unsigned lo = Hex4();
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    PutUtf8(o, cp);
                    break;
                }
                default: o += ch;
                }
            } else o += *p++;
        }
        if (p >= e) return false;
        ++p;
        return true;
    }
    bool Parse(JVal& v) {
        if (++depth > 64) return false;
        Ws();
        if (p >= e) return false;
        bool ok = false;
        if (*p == '{') {
            v.t = JVal::Obj;
            ++p;
            Ws();
            if (p < e && *p == '}') { ++p; ok = true; }
            while (!ok && p < e) {
                Ws();
                if (p >= e || *p != '"') break;
                std::string k;
                if (!Str(k)) break;
                Ws();
                if (p >= e || *p != ':') break;
                ++p;
                JVal val;
                if (!Parse(val)) break;
                v.o.emplace_back(std::move(k), std::move(val));
                Ws();
                if (p < e && *p == ',') { ++p; continue; }
                if (p < e && *p == '}') { ++p; ok = true; }
                break;
            }
        } else if (*p == '[') {
            v.t = JVal::Arr;
            ++p;
            Ws();
            if (p < e && *p == ']') { ++p; ok = true; }
            while (!ok && p < e) {
                JVal val;
                if (!Parse(val)) break;
                v.a.push_back(std::move(val));
                Ws();
                if (p < e && *p == ',') { ++p; continue; }
                if (p < e && *p == ']') { ++p; ok = true; }
                break;
            }
        } else if (*p == '"') {
            v.t = JVal::Str;
            ok = Str(v.s);
        } else if (Lit("true")) { v.t = JVal::Bool; v.n = 1; ok = true; }
        else if (Lit("false")) { v.t = JVal::Bool; ok = true; }
        else if (Lit("null")) { v.t = JVal::Null; ok = true; }
        else {
            std::string num;
            while (p < e && (isdigit((unsigned char)*p) || *p == '-' || *p == '+' || *p == '.' || *p == 'e' || *p == 'E')) num += *p++;
            if (!num.empty()) {
                v.t = JVal::Num;
                v.n = strtod(num.c_str(), nullptr);
                ok = true;
            }
        }
        --depth;
        return ok;
    }
};
}  // namespace

bool ParseJson(const std::string& text, JVal& out) {
    Parser ps{ text.data(), text.data() + text.size() };
    return ps.Parse(out);
}
}  // namespace net

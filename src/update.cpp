#include "update.h"
#include "net.h"
#include "config.h"
#include "version.h"
#include "install.h"

namespace update {
static std::vector<int> Parts(std::wstring v) {
    if (!v.empty() && (v[0] == L'v' || v[0] == L'V')) v = v.substr(1);
    std::vector<int> r;
    size_t p = 0;
    while (p < v.size() && r.size() < 4) {
        r.push_back(_wtoi(v.c_str() + p));
        size_t d = v.find(L'.', p);
        if (d == std::wstring::npos) break;
        p = d + 1;
    }
    while (r.size() < 3) r.push_back(0);
    return r;
}

int Compare(const std::wstring& a, const std::wstring& b) {
    auto x = Parts(a), y = Parts(b);
    for (size_t i = 0; i < std::max(x.size(), y.size()); ++i) {
        int u = i < x.size() ? x[i] : 0, v = i < y.size() ? y[i] : 0;
        if (u != v) return u < v ? -1 : 1;
    }
    return 0;
}

Info Check() {
    Info in;
    int status = 0;
    std::string body = net::Get(L"https://api.github.com/repos/" WINMAC_REPO L"/releases/latest", &status,
                                L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n");
    if (status == 404) { in.ok = true; in.version = WINMAC_VERSION; return in; }   // ainda sem releases
    if (status != 200) {
        in.error = status ? L"O GitHub respondeu " + std::to_wstring(status) + L"." : L"Sem conexão com a internet.";
        return in;
    }
    net::JVal j;
    if (!net::ParseJson(body, j) || j.t != net::JVal::Obj) { in.error = L"Resposta inesperada do GitHub."; return in; }
    if (auto* t = j.Get("tag_name")) in.version = t->W();
    if (!in.version.empty() && (in.version[0] == L'v' || in.version[0] == L'V')) in.version = in.version.substr(1);
    if (auto* b = j.Get("body")) in.notes = b->W();
    if (auto* h = j.Get("html_url")) in.page = h->W();
    if (auto* as = j.Get("assets")) {
        for (auto& a : as->a) {
            auto* n = a.Get("name");
            if (!n) continue;
            std::wstring name = Lower(n->W());
            if (name.size() < 4 || name.compare(name.size() - 4, 4, L".exe") != 0) continue;
            if (auto* u = a.Get("browser_download_url")) in.url = u->W();
            if (auto* sz = a.Get("size")) in.size = (uint64_t)sz->Number();
            if (auto* d = a.Get("digest"))
                if (d->s.rfind("sha256:", 0) == 0) in.sha256 = d->s.substr(7);
            if (name.find(L"setup") != std::wstring::npos) break;   // prefere WinMac-Setup.exe
        }
    }
    in.ok = !in.version.empty();
    in.newer = in.ok && !in.url.empty() && Compare(in.version, WINMAC_VERSION) > 0;
    if (!in.ok) in.error = L"Não encontrei a versão no GitHub.";
    return in;
}

void CheckAsync(HWND notify) {
    std::thread([notify]() {
        auto* in = new Info(Check());
        if (!PostMessageW(notify, WM_MD_UPDATE, 0, (LPARAM)in)) delete in;
    }).detach();
}

void SaveAvailable(const Info& in) {
    cfg::SetInt(L"Update", L"LastCheck", (int)(GetTickCount64() / 1000));
    if (in.newer) {
        cfg::Set(L"Update", L"Available", in.version);
        cfg::Set(L"Update", L"Url", in.url);
    } else if (in.ok) {
        cfg::Set(L"Update", L"Available", L"");
    }
}

std::wstring DownloadAndRun(const Info& in, const std::function<void(float)>& progress, const std::atomic<bool>* cancel) {
    if (in.url.empty()) return L"Esta versão não tem instalador publicado.";
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::wstring file = std::wstring(tmp) + L"WinMac-Setup-" + in.version + L".exe";
    bool ok = net::Download(in.url, file, [&](uint64_t got, uint64_t total) {
        if (progress) progress(total ? (float)((double)got / total) : 0.f);
    }, cancel);
    if (!ok) return cancel && *cancel ? L"Cancelado." : L"Falha ao baixar a atualização.";
    if (!in.sha256.empty()) {
        std::string h = net::Sha256File(file);
        std::string want = in.sha256;
        std::transform(want.begin(), want.end(), want.begin(), ::tolower);
        if (h != want) {
            DeleteFileW(file.c_str());
            return L"O arquivo baixado não confere (SHA-256). Atualização cancelada por segurança.";
        }
    }
    std::wstring args = L"--apply-update \"" + install::Dir() + L"\"";
    SHELLEXECUTEINFOW sei{ sizeof(sei) };
    sei.fMask = SEE_MASK_NOASYNC;
    sei.lpFile = file.c_str();
    sei.lpParameters = args.c_str();
    sei.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei)) return L"Não consegui iniciar o instalador da atualização.";
    return L"";
}
}  // namespace update

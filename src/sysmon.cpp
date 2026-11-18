#include "sysmon.h"
#include "apps.h"
#include <tlhelp32.h>
#include <psapi.h>
#include <winsock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <netioapi.h>

namespace sysmon {
namespace {
Snapshot g_s;
ULONGLONG g_idle = 0, g_kern = 0, g_user = 0;
ULONG64 g_rx = 0, g_tx = 0;
ULONGLONG g_netTick = 0, g_procTick = 0;
std::unordered_map<DWORD, ULONGLONG> g_procTimes;
std::unordered_map<std::wstring, std::wstring> g_names;

ULONGLONG F(const FILETIME& f) { return ((ULONGLONG)f.dwHighDateTime << 32) | f.dwLowDateTime; }

void Push(std::vector<float>& v, float x) {
    v.push_back(x);
    if (v.size() > 40) v.erase(v.begin());
}

std::wstring Pretty(const std::wstring& exe, const std::wstring& file) {
    auto it = g_names.find(file);
    if (it != g_names.end()) return it->second;
    std::wstring n = exe.empty() ? L"" : apps::FileDescription(exe);
    if (n.empty()) {
        n = file;
        size_t dot = n.rfind(L'.');
        if (dot != std::wstring::npos) n.resize(dot);
    }
    if (g_names.size() > 500) g_names.clear();
    g_names[file] = n;
    return n;
}
}  // namespace

void Sample(bool withProcesses) {
    // CPU total
    FILETIME fi, fk, fu;
    if (GetSystemTimes(&fi, &fk, &fu)) {
        ULONGLONG i = F(fi), k = F(fk), u = F(fu);
        if (g_kern) {
            ULONGLONG di = i - g_idle, dk = k - g_kern, du = u - g_user, tot = dk + du;
            g_s.cpu = tot ? (float)(100.0 * (tot - di) / tot) : 0.f;
            g_s.cpu = std::clamp(g_s.cpu, 0.f, 100.f);
            Push(g_s.cpuHist, g_s.cpu);
        }
        g_idle = i;
        g_kern = k;
        g_user = u;
    }
    MEMORYSTATUSEX ms{ sizeof(ms) };
    if (GlobalMemoryStatusEx(&ms)) {
        g_s.memTotal = ms.ullTotalPhys;
        g_s.memUsed = ms.ullTotalPhys - ms.ullAvailPhys;
    }
    wchar_t win[MAX_PATH];
    GetWindowsDirectoryW(win, MAX_PATH);
    win[3] = 0;
    ULARGE_INTEGER fa, tot, fr;
    if (GetDiskFreeSpaceExW(win, &fa, &tot, &fr)) { g_s.diskFree = fr.QuadPart; g_s.diskTotal = tot.QuadPart; }
    g_s.uptimeSec = GetTickCount64() / 1000;
    // rede (soma das interfaces físicas ligadas)
    PMIB_IF_TABLE2 t = nullptr;
    if (GetIfTable2(&t) == NO_ERROR && t) {
        ULONG64 rx = 0, tx = 0;
        for (ULONG n = 0; n < t->NumEntries; ++n) {
            auto& r = t->Table[n];
            if (!r.InterfaceAndOperStatusFlags.HardwareInterface || r.OperStatus != IfOperStatusUp) continue;
            rx += r.InOctets;
            tx += r.OutOctets;
        }
        FreeMibTable(t);
        ULONGLONG now = GetTickCount64();
        if (g_netTick && now > g_netTick && rx >= g_rx && tx >= g_tx) {
            double dt = (now - g_netTick) / 1000.0;
            g_s.down = (rx - g_rx) / dt;
            g_s.up = (tx - g_tx) / dt;
            Push(g_s.netHist, (float)g_s.down);
        }
        g_rx = rx;
        g_tx = tx;
        g_netTick = now;
    }
    if (!withProcesses) return;
    // processos: diferença do tempo de CPU desde a última amostra
    ULONGLONG now = GetTickCount64();
    double wall = g_procTick ? (now - g_procTick) * 10000.0 : 0;   // em unidades de 100 ns
    g_procTick = now;
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    std::unordered_map<DWORD, ULONGLONG> times;
    std::map<std::wstring, Proc> byName;   // agrupa processos do mesmo app (Chrome tem dezenas)
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    int count = 0;
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe{ sizeof(pe) };
        for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
            if (pe.th32ProcessID == 0) continue;
            count++;
            HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
            if (!p) continue;
            FILETIME c, e, k, u;
            ULONGLONG cpuT = 0;
            if (GetProcessTimes(p, &c, &e, &k, &u)) cpuT = F(k) + F(u);
            PROCESS_MEMORY_COUNTERS pmc{ sizeof(pmc) };
            uint64_t mem = 0;
            if (K32GetProcessMemoryInfo(p, &pmc, sizeof(pmc))) mem = pmc.WorkingSetSize;
            wchar_t path[MAX_PATH * 2];
            DWORD n = MAX_PATH * 2;
            std::wstring exe;
            if (QueryFullProcessImageNameW(p, 0, path, &n)) exe.assign(path, n);
            CloseHandle(p);
            times[pe.th32ProcessID] = cpuT;
            double cpu = 0;
            auto it = g_procTimes.find(pe.th32ProcessID);
            if (wall > 0 && it != g_procTimes.end() && cpuT >= it->second) cpu = 100.0 * (cpuT - it->second) / (wall * si.dwNumberOfProcessors);
            std::wstring key = Lower(pe.szExeFile);
            if (key == L"system" || key == L"registry" || key == L"memory compression" || key == L"idle") continue;
            auto& agg = byName[key];
            if (agg.name.empty()) { agg.name = Pretty(exe, pe.szExeFile); agg.exe = exe; agg.pid = pe.th32ProcessID; }
            agg.cpu += cpu;
            agg.mem += mem;
        }
        CloseHandle(snap);
    }
    g_procTimes.swap(times);
    g_s.processes = count;
    std::vector<Proc> all;
    for (auto& kv : byName) all.push_back(kv.second);
    std::sort(all.begin(), all.end(), [](const Proc& a, const Proc& b) { return a.cpu != b.cpu ? a.cpu > b.cpu : a.mem > b.mem; });
    if (all.size() > 5) all.resize(5);
    g_s.top = all;
}

const Snapshot& Get() { return g_s; }

std::wstring Speed(double b) {
    wchar_t buf[48];
    if (b >= 1048576) swprintf_s(buf, L"%.1f MB/s", b / 1048576);
    else if (b >= 1024) swprintf_s(buf, L"%.0f KB/s", b / 1024);
    else swprintf_s(buf, L"%.0f B/s", b);
    std::wstring s = buf;
    for (auto& c : s) if (c == L'.') c = L',';
    return s;
}
}  // namespace sysmon

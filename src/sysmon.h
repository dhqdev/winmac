#pragma once
#include "common.h"

// Medições leves do computador para os widgets: CPU, memória, disco, rede e processos.
namespace sysmon {
struct Proc { std::wstring name, exe; double cpu = 0; uint64_t mem = 0; DWORD pid = 0; };
struct Snapshot {
    float cpu = 0;                 // 0..100
    std::vector<float> cpuHist;    // últimos ~40 valores
    uint64_t memUsed = 0, memTotal = 0;
    uint64_t diskFree = 0, diskTotal = 0;   // unidade do Windows
    double down = 0, up = 0;       // bytes/s
    std::vector<float> netHist;    // download, últimos ~40 valores (bytes/s)
    std::vector<Proc> top;         // 5 processos que mais usam CPU
    int processes = 0;
    uint64_t uptimeSec = 0;
};
void Sample(bool withProcesses);   // chame a cada ~2 s na thread da interface
const Snapshot& Get();
std::wstring Speed(double bytesPerSec);
}

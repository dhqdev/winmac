#include "tray.h"
#include "gfx.h"
#include <commctrl.h>

namespace tray {
static std::vector<Icon> s_icons;

struct TRAYDATA64 {
    HWND hwnd;
    UINT uID;
    UINT uCallbackMessage;
    DWORD Reserved[2];
    HICON hIcon;
};

static HWND Promoted() {
    HWND t = FindWindowW(L"Shell_TrayWnd", nullptr);
    HWND n = FindWindowExW(t, nullptr, L"TrayNotifyWnd", nullptr);
    HWND p = FindWindowExW(n, nullptr, L"SysPager", nullptr);
    return FindWindowExW(p, nullptr, L"ToolbarWindow32", nullptr);
}

static HWND OverflowTb() {
    HWND o = FindWindowW(L"NotifyIconOverflowWindow", nullptr);
    return FindWindowExW(o, nullptr, L"ToolbarWindow32", nullptr);
}

static void ReadToolbar(HWND tb, bool overflow, std::vector<Icon>& out) {
    if (!tb) return;
    DWORD pid = 0;
    GetWindowThreadProcessId(tb, &pid);
    HANDLE p = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return;
    void* remote = VirtualAllocEx(p, nullptr, 4096, MEM_COMMIT, PAGE_READWRITE);
    if (remote) {
        int count = (int)SendMessageW(tb, TB_BUTTONCOUNT, 0, 0);
        for (int i = 0; i < count && i < 64; ++i) {
            TBBUTTON btn{};
            DWORD_PTR ok = 0;
            if (!SendMessageTimeoutW(tb, TB_GETBUTTON, i, (LPARAM)remote, SMTO_ABORTIFHUNG, 200, &ok) || !ok) continue;
            SIZE_T rd = 0;
            if (!ReadProcessMemory(p, remote, &btn, sizeof(btn), &rd)) continue;
            if (btn.fsState & TBSTATE_HIDDEN) continue;
            TRAYDATA64 td{};
            if (!btn.dwData || !ReadProcessMemory(p, (void*)btn.dwData, &td, sizeof(td), &rd)) continue;
            DWORD opid = 0;
            GetWindowThreadProcessId(td.hwnd, &opid);
            if (!td.hwnd || opid == pid) continue;  // ícones do próprio Explorer (volume, rede, bateria): temos os nossos
            wchar_t tip[260] = {};
            DWORD_PTR len = 0;
            SendMessageTimeoutW(tb, TB_GETBUTTONTEXTW, btn.idCommand, (LPARAM)remote, SMTO_ABORTIFHUNG, 200, &len);
            if (len > 0 && len < 255) ReadProcessMemory(p, remote, tip, (len + 1) * sizeof(wchar_t), &rd);
            Icon ic;
            ic.owner = td.hwnd;
            ic.uid = td.uID;
            ic.hicon = td.hIcon;
            ic.tip = tip;
            ic.index = i;
            ic.overflow = overflow;
            out.push_back(std::move(ic));
        }
        VirtualFreeEx(p, remote, 0, MEM_RELEASE);
    }
    CloseHandle(p);
}

bool Refresh() {
    std::vector<Icon> v;
    ReadToolbar(OverflowTb(), true, v);
    ReadToolbar(Promoted(), false, v);
    bool changed = v.size() != s_icons.size();
    for (size_t i = 0; i < v.size(); ++i) {
        const Icon* old = nullptr;
        for (auto& o : s_icons)
            if (o.owner == v[i].owner && o.uid == v[i].uid) { old = &o; break; }
        if (old && old->hicon == v[i].hicon) v[i].bmp = old->bmp;
        else v[i].bmp = gfx::FromHIcon(v[i].hicon);
        if (!changed && (s_icons[i].owner != v[i].owner || s_icons[i].hicon != v[i].hicon || s_icons[i].tip != v[i].tip)) changed = true;
    }
    s_icons.swap(v);
    return changed;
}

const std::vector<Icon>& Icons() { return s_icons; }

void Click(const Icon& ic, bool right, bool dbl) {
    HWND tb = ic.overflow ? OverflowTb() : Promoted();
    if (!tb) return;
    DWORD pid = 0;
    GetWindowThreadProcessId(tb, &pid);
    HANDLE p = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return;
    RECT rc{};
    void* remote = VirtualAllocEx(p, nullptr, sizeof(RECT), MEM_COMMIT, PAGE_READWRITE);
    bool ok = false;
    if (remote) {
        DWORD_PTR r = 0;
        if (SendMessageTimeoutW(tb, TB_GETITEMRECT, ic.index, (LPARAM)remote, SMTO_ABORTIFHUNG, 200, &r) && r) {
            SIZE_T rd;
            ok = ReadProcessMemory(p, remote, &rc, sizeof(rc), &rd) != FALSE;
        }
        VirtualFreeEx(p, remote, 0, MEM_RELEASE);
    }
    CloseHandle(p);
    if (!ok) return;
    LPARAM pt = MAKELPARAM((rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2);
    AllowSetForegroundWindow(pid);
    DWORD opid = 0;
    GetWindowThreadProcessId(ic.owner, &opid);
    AllowSetForegroundWindow(opid);
    PostMessageW(tb, WM_MOUSEMOVE, 0, pt);
    if (right) {
        PostMessageW(tb, WM_RBUTTONDOWN, MK_RBUTTON, pt);
        PostMessageW(tb, WM_RBUTTONUP, 0, pt);
    } else {
        PostMessageW(tb, WM_LBUTTONDOWN, MK_LBUTTON, pt);
        PostMessageW(tb, WM_LBUTTONUP, 0, pt);
        if (dbl) {
            PostMessageW(tb, WM_LBUTTONDBLCLK, MK_LBUTTON, pt);
            PostMessageW(tb, WM_LBUTTONUP, 0, pt);
        }
    }
}
}  // namespace tray

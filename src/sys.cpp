#include "sys.h"
#include "sysx.h"
#include <wlanapi.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <wbemidl.h>
#include <winsock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>

namespace sys {
Battery GetBattery() {
    Battery b;
    SYSTEM_POWER_STATUS s{};
    if (GetSystemPowerStatus(&s)) {
        b.present = !(s.BatteryFlag & 128) && s.BatteryFlag != 255;
        b.pct = s.BatteryLifePercent <= 100 ? s.BatteryLifePercent : 100;
        b.plugged = s.ACLineStatus == 1;
        b.charging = (s.BatteryFlag & 8) != 0 || (b.plugged && b.pct < 100);
    }
    return b;
}

Wifi GetWifi() {
    Wifi w;
    HANDLE h = nullptr;
    DWORD ver = 0;
    if (WlanOpenHandle(2, nullptr, &ver, &h) == ERROR_SUCCESS) {
        PWLAN_INTERFACE_INFO_LIST list = nullptr;
        if (WlanEnumInterfaces(h, nullptr, &list) == ERROR_SUCCESS && list) {
            for (DWORD i = 0; i < list->dwNumberOfItems; ++i) {
                auto& ii = list->InterfaceInfo[i];
                if (ii.isState != wlan_interface_state_connected) continue;
                PWLAN_CONNECTION_ATTRIBUTES ca = nullptr;
                DWORD sz = 0;
                WLAN_OPCODE_VALUE_TYPE t;
                if (WlanQueryInterface(h, &ii.InterfaceGuid, wlan_intf_opcode_current_connection, nullptr, &sz, (PVOID*)&ca, &t) == ERROR_SUCCESS && ca) {
                    w.connected = w.wireless = true;
                    w.quality = (int)ca->wlanAssociationAttributes.wlanSignalQuality;
                    auto& ss = ca->wlanAssociationAttributes.dot11Ssid;
                    int n = MultiByteToWideChar(CP_UTF8, 0, (const char*)ss.ucSSID, ss.uSSIDLength, nullptr, 0);
                    w.ssid.resize(n);
                    MultiByteToWideChar(CP_UTF8, 0, (const char*)ss.ucSSID, ss.uSSIDLength, w.ssid.data(), n);
                    WlanFreeMemory(ca);
                    break;
                }
            }
            WlanFreeMemory(list);
        }
        WlanCloseHandle(h, nullptr);
    }
    if (!w.connected) {
        // cabo? verifica se existe alguma interface de rede ativa com gateway
        ULONG sz = 0;
        GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_GATEWAYS, nullptr, nullptr, &sz);
        std::vector<BYTE> buf(sz);
        auto* a = (PIP_ADAPTER_ADDRESSES)buf.data();
        if (sz && GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_GATEWAYS, nullptr, a, &sz) == NO_ERROR) {
            for (; a; a = a->Next)
                if (a->OperStatus == IfOperStatusUp && a->FirstGatewayAddress && a->IfType == IF_TYPE_ETHERNET_CSMACD) w.connected = true;
        }
    }
    return w;
}

static ComPtr<IAudioEndpointVolume> Endpoint() {
    ComPtr<IMMDeviceEnumerator> en;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en)))) return nullptr;
    ComPtr<IMMDevice> dev;
    if (FAILED(en->GetDefaultAudioEndpoint(eRender, eMultimedia, &dev))) return nullptr;
    ComPtr<IAudioEndpointVolume> v;
    dev->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, (void**)v.GetAddressOf());
    return v;
}

float GetVolume(bool* muted) {
    auto v = Endpoint();
    float f = 0;
    BOOL m = FALSE;
    if (v) {
        v->GetMasterVolumeLevelScalar(&f);
        v->GetMute(&m);
    }
    if (muted) *muted = m != FALSE;
    return f;
}
void SetVolume(float f) {
    if (auto v = Endpoint()) {
        v->SetMasterVolumeLevelScalar(std::clamp(f, 0.f, 1.f), nullptr);
        if (f > 0.001f) v->SetMute(FALSE, nullptr);
    }
}
void SetMute(bool m) {
    if (auto v = Endpoint()) v->SetMute(m, nullptr);
}

static ComPtr<IWbemServices> Wmi() {
    static ComPtr<IWbemServices> svc;
    if (svc) return svc;
    ComPtr<IWbemLocator> loc;
    if (FAILED(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&loc)))) return nullptr;
    BSTR ns = SysAllocString(L"ROOT\\WMI");
    HRESULT hr = loc->ConnectServer(ns, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &svc);
    SysFreeString(ns);
    if (FAILED(hr)) return nullptr;
    CoSetProxyBlanket(svc.Get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
    return svc;
}

static ComPtr<IWbemClassObject> First(IWbemServices* svc, const wchar_t* q) {
    BSTR lang = SysAllocString(L"WQL"), qs = SysAllocString(q);
    ComPtr<IEnumWbemClassObject> en;
    HRESULT hr = svc->ExecQuery(lang, qs, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &en);
    SysFreeString(lang);
    SysFreeString(qs);
    if (FAILED(hr)) return nullptr;
    ComPtr<IWbemClassObject> obj;
    ULONG n = 0;
    if (en->Next(2000, 1, &obj, &n) != WBEM_S_NO_ERROR || !n) return nullptr;
    return obj;
}

int GetBrightness() {
    auto svc = Wmi();
    if (!svc) return -1;
    auto obj = First(svc.Get(), L"SELECT CurrentBrightness FROM WmiMonitorBrightness");
    if (!obj) return -1;
    VARIANT v;
    VariantInit(&v);
    int r = -1;
    if (SUCCEEDED(obj->Get(L"CurrentBrightness", 0, &v, nullptr, nullptr))) {
        if (v.vt == VT_UI1) r = v.bVal;
        else if (v.vt == VT_I4) r = v.lVal;
    }
    VariantClear(&v);
    return r;
}

void SetBrightness(int pct) {
    auto svc = Wmi();
    if (!svc) return;
    auto inst = First(svc.Get(), L"SELECT * FROM WmiMonitorBrightnessMethods");
    if (!inst) return;
    VARIANT path;
    VariantInit(&path);
    if (FAILED(inst->Get(L"__PATH", 0, &path, nullptr, nullptr)) || path.vt != VT_BSTR) return;
    ComPtr<IWbemClassObject> cls, inDef, args;
    BSTR clsName = SysAllocString(L"WmiMonitorBrightnessMethods"), method = SysAllocString(L"WmiSetBrightness");
    if (SUCCEEDED(svc->GetObject(clsName, 0, nullptr, &cls, nullptr)) && SUCCEEDED(cls->GetMethod(method, 0, &inDef, nullptr)) &&
        SUCCEEDED(inDef->SpawnInstance(0, &args))) {
        VARIANT t, b;
        VariantInit(&t);
        VariantInit(&b);
        t.vt = VT_I4;
        t.lVal = 0;
        b.vt = VT_UI1;
        b.bVal = (BYTE)std::clamp(pct, 0, 100);
        args->Put(L"Timeout", 0, &t, 0);
        args->Put(L"Brightness", 0, &b, 0);
        svc->ExecMethod(path.bstrVal, method, 0, nullptr, args.Get(), nullptr, nullptr);
    }
    SysFreeString(clsName);
    SysFreeString(method);
    VariantClear(&path);
}

void SetDarkMode(bool dark) {
    const wchar_t* key = L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
    sysx::SetDword(HKEY_CURRENT_USER, key, L"AppsUseLightTheme", dark ? 0 : 1);
    sysx::SetDword(HKEY_CURRENT_USER, key, L"SystemUsesLightTheme", dark ? 0 : 1);
    DWORD_PTR r;
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"ImmersiveColorSet", SMTO_ABORTIFHUNG, 200, &r);
}
}  // namespace sys

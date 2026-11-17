#include "wifi.h"
#include <wlanapi.h>

namespace wifi {
namespace {
struct Client {
    HANDLE h = nullptr;
    GUID guid{};
    bool ok = false;
    Client() {
        DWORD ver = 0;
        if (WlanOpenHandle(2, nullptr, &ver, &h) != ERROR_SUCCESS) { h = nullptr; return; }
        PWLAN_INTERFACE_INFO_LIST list = nullptr;
        if (WlanEnumInterfaces(h, nullptr, &list) == ERROR_SUCCESS && list) {
            for (DWORD i = 0; i < list->dwNumberOfItems; ++i) {
                guid = list->InterfaceInfo[i].InterfaceGuid;
                ok = true;
                if (list->InterfaceInfo[i].isState == wlan_interface_state_connected) break;
            }
            WlanFreeMemory(list);
        }
    }
    ~Client() { if (h) WlanCloseHandle(h, nullptr); }
};

std::wstring SsidOf(const DOT11_SSID& s) {
    std::string a((const char*)s.ucSSID, s.uSSIDLength);
    std::wstring w = Utf8ToW(a);
    if (w.empty() && !a.empty()) for (char ch : a) w += (wchar_t)(unsigned char)ch;
    return w;
}

std::wstring Xml(const std::wstring& s) {
    std::wstring r;
    for (wchar_t ch : s) {
        switch (ch) {
        case L'&': r += L"&amp;"; break;
        case L'<': r += L"&lt;"; break;
        case L'>': r += L"&gt;"; break;
        case L'"': r += L"&quot;"; break;
        case L'\'': r += L"&apos;"; break;
        default: r += ch;
        }
    }
    return r;
}

std::wstring Hex(const std::wstring& ssid) {
    std::string u = WToUtf8(ssid);
    std::wstring r;
    wchar_t b[4];
    for (unsigned char ch : u) { swprintf_s(b, L"%02X", ch); r += b; }
    return r;
}

WLAN_INTERFACE_STATE State(Client& c) {
    PWLAN_INTERFACE_INFO_LIST list = nullptr;
    WLAN_INTERFACE_STATE st = wlan_interface_state_not_ready;
    if (WlanEnumInterfaces(c.h, nullptr, &list) == ERROR_SUCCESS && list) {
        for (DWORD i = 0; i < list->dwNumberOfItems; ++i)
            if (list->InterfaceInfo[i].InterfaceGuid == c.guid) st = list->InterfaceInfo[i].isState;
        WlanFreeMemory(list);
    }
    return st;
}

std::wstring CurrentSsid(Client& c) {
    PWLAN_CONNECTION_ATTRIBUTES ca = nullptr;
    DWORD sz = 0;
    WLAN_OPCODE_VALUE_TYPE t;
    std::wstring r;
    if (WlanQueryInterface(c.h, &c.guid, wlan_intf_opcode_current_connection, nullptr, &sz, (PVOID*)&ca, &t) == ERROR_SUCCESS && ca) {
        if (ca->isState == wlan_interface_state_connected) r = SsidOf(ca->wlanAssociationAttributes.dot11Ssid);
        WlanFreeMemory(ca);
    }
    return r;
}
}  // namespace

bool HasAdapter() { return Client().ok; }

bool RadioOn() {
    Client c;
    if (!c.ok) return false;
    PWLAN_RADIO_STATE rs = nullptr;
    DWORD sz = 0;
    WLAN_OPCODE_VALUE_TYPE t;
    bool on = true;
    if (WlanQueryInterface(c.h, &c.guid, wlan_intf_opcode_radio_state, nullptr, &sz, (PVOID*)&rs, &t) == ERROR_SUCCESS && rs) {
        for (DWORD i = 0; i < rs->dwNumberOfPhys; ++i)
            if (rs->PhyRadioState[i].dot11SoftwareRadioState == dot11_radio_state_off || rs->PhyRadioState[i].dot11HardwareRadioState == dot11_radio_state_off) on = false;
        WlanFreeMemory(rs);
    }
    return on;
}

bool SetRadio(bool on) {
    Client c;
    if (!c.ok) return false;
    WLAN_PHY_RADIO_STATE st{};
    st.dwPhyIndex = 0;
    st.dot11SoftwareRadioState = on ? dot11_radio_state_on : dot11_radio_state_off;
    return WlanSetInterface(c.h, &c.guid, wlan_intf_opcode_radio_state, sizeof(st), &st, nullptr) == ERROR_SUCCESS;
}

void Scan() {
    Client c;
    if (c.ok) WlanScan(c.h, &c.guid, nullptr, nullptr, nullptr);
}

std::vector<Net> List() {
    std::vector<Net> out;
    Client c;
    if (!c.ok) return out;
    PWLAN_AVAILABLE_NETWORK_LIST nl = nullptr;
    if (WlanGetAvailableNetworkList(c.h, &c.guid, 0, nullptr, &nl) != ERROR_SUCCESS || !nl) return out;
    for (DWORD i = 0; i < nl->dwNumberOfItems; ++i) {
        auto& a = nl->Network[i];
        if (a.dot11Ssid.uSSIDLength == 0) continue;   // rede oculta
        Net n;
        n.ssid = SsidOf(a.dot11Ssid);
        n.quality = (int)a.wlanSignalQuality;
        n.secured = a.bSecurityEnabled != FALSE;
        n.connected = (a.dwFlags & WLAN_AVAILABLE_NETWORK_CONNECTED) != 0;
        n.hasProfile = (a.dwFlags & WLAN_AVAILABLE_NETWORK_HAS_PROFILE) != 0 && a.strProfileName[0];
        n.profile = a.strProfileName;
        n.auth = a.dot11DefaultAuthAlgorithm;
        n.cipher = a.dot11DefaultCipherAlgorithm;
        n.enterprise = a.dot11DefaultAuthAlgorithm == DOT11_AUTH_ALGO_RSNA || a.dot11DefaultAuthAlgorithm == DOT11_AUTH_ALGO_WPA;
        // a mesma rede aparece repetida (com e sem perfil): fica a melhor
        auto it = std::find_if(out.begin(), out.end(), [&](const Net& o) { return o.ssid == n.ssid; });
        if (it != out.end()) {
            it->connected |= n.connected;
            if (n.hasProfile && !it->hasProfile) { it->hasProfile = true; it->profile = n.profile; }
            it->quality = std::max(it->quality, n.quality);
            continue;
        }
        out.push_back(n);
    }
    WlanFreeMemory(nl);
    std::sort(out.begin(), out.end(), [](const Net& a, const Net& b) {
        if (a.connected != b.connected) return a.connected;
        if (a.hasProfile != b.hasProfile) return a.hasProfile;
        return a.quality > b.quality;
    });
    return out;
}

Result Connect(const Net& n, const std::wstring& password) {
    Client c;
    if (!c.ok) return Result::Failed;
    if (n.enterprise) return Result::Unsupported;
    std::wstring profile = n.profile;
    bool created = false;
    if (!n.hasProfile || !password.empty()) {
        std::wstring auth, enc;
        switch (n.auth) {
        case DOT11_AUTH_ALGO_80211_OPEN: auth = n.secured ? L"open" : L"open"; enc = n.secured ? L"WEP" : L"none"; break;
        case DOT11_AUTH_ALGO_WPA_PSK: auth = L"WPAPSK"; break;
        case DOT11_AUTH_ALGO_RSNA_PSK: auth = L"WPA2PSK"; break;
        case 9 /* DOT11_AUTH_ALGO_WPA3_SAE */: auth = L"WPA3SAE"; break;
        default: return Result::Unsupported;
        }
        if (enc.empty()) enc = n.cipher == DOT11_CIPHER_ALGO_TKIP ? L"TKIP" : L"AES";
        if (n.secured && enc == L"WEP") return Result::Unsupported;
        if (n.secured && password.size() < 8) return Result::WrongPassword;
        profile = n.ssid;
        std::wstring xml = L"<?xml version=\"1.0\"?><WLANProfile xmlns=\"http://www.microsoft.com/networking/WLAN/profile/v1\"><name>" + Xml(profile) +
                           L"</name><SSIDConfig><SSID><hex>" + Hex(n.ssid) + L"</hex><name>" + Xml(n.ssid) +
                           L"</name></SSID></SSIDConfig><connectionType>ESS</connectionType><connectionMode>auto</connectionMode><MSM><security><authEncryption><authentication>" +
                           auth + L"</authentication><encryption>" + enc + L"</encryption><useOneX>false</useOneX></authEncryption>";
        if (n.secured)
            xml += L"<sharedKey><keyType>passPhrase</keyType><protected>false</protected><keyMaterial>" + Xml(password) + L"</keyMaterial></sharedKey>";
        xml += L"</security></MSM></WLANProfile>";
        DWORD reason = 0;
        if (WlanSetProfile(c.h, &c.guid, 0, xml.c_str(), nullptr, TRUE, nullptr, &reason) != ERROR_SUCCESS) return Result::Failed;
        created = true;
    }
    WLAN_CONNECTION_PARAMETERS p{};
    p.wlanConnectionMode = wlan_connection_mode_profile;
    p.strProfile = profile.c_str();
    p.dot11BssType = dot11_BSS_type_infrastructure;
    if (WlanConnect(c.h, &c.guid, &p, nullptr) != ERROR_SUCCESS) return Result::Failed;
    // espera associar e autenticar
    bool sawConnecting = false;
    for (int i = 0; i < 60; ++i) {
        Sleep(250);
        auto st = State(c);
        if (st == wlan_interface_state_connected && CurrentSsid(c) == n.ssid) return Result::Ok;
        if (st == wlan_interface_state_associating || st == wlan_interface_state_authenticating) sawConnecting = true;
        if (sawConnecting && st == wlan_interface_state_disconnected && i > 8) break;
    }
    if (created && n.secured) {
        WlanDeleteProfile(c.h, &c.guid, profile.c_str(), nullptr);   // senha errada não fica salva
        return Result::WrongPassword;
    }
    return Result::Failed;
}

void Disconnect() {
    Client c;
    if (c.ok) WlanDisconnect(c.h, &c.guid, nullptr);
}

std::wstring Describe(Result r) {
    switch (r) {
    case Result::Ok: return L"Conectado";
    case Result::WrongPassword: return L"Não deu certo. Confira a senha.";
    case Result::Unsupported: return L"Esta rede precisa do painel do Windows.";
    default: return L"Não foi possível conectar.";
    }
}
}  // namespace wifi

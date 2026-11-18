// Liga/desliga Wi-Fi e Bluetooth pela API Windows.Devices.Radios (C++/WinRT), sem administrador.
// Arquivo separado: os cabeçalhos do WinRT não convivem bem com as macros de common.h.
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Radios.h>
#include <future>

namespace sysx {
enum class Radio { Wifi, Bluetooth };

namespace {
namespace WR = winrt::Windows::Devices::Radios;

// roda numa thread MTA própria (o .get() não pode bloquear a thread da interface)
template <typename F>
auto InMta(F f) -> decltype(f()) {
    return std::async(std::launch::async, [f]() {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        auto r = f();
        winrt::uninit_apartment();
        return r;
    }).get();
}

WR::RadioKind Kind(sysx::Radio r) { return r == sysx::Radio::Wifi ? WR::RadioKind::WiFi : WR::RadioKind::Bluetooth; }
}  // namespace

int RadioState(Radio r) {
    try {
        return InMta([r]() -> int {
            auto radios = WR::Radio::GetRadiosAsync().get();
            int st = -1;
            for (auto const& x : radios)
                if (x.Kind() == Kind(r)) st = x.State() == WR::RadioState::On ? 1 : (st == 1 ? 1 : 0);
            return st;
        });
    } catch (...) {
        return -1;
    }
}

bool SetRadioState(Radio r, bool on) {
    try {
        return InMta([r, on]() -> bool {
            if (WR::Radio::RequestAccessAsync().get() != WR::RadioAccessStatus::Allowed) return false;
            bool ok = false;
            for (auto const& x : WR::Radio::GetRadiosAsync().get())
                if (x.Kind() == Kind(r)) ok = x.SetStateAsync(on ? WR::RadioState::On : WR::RadioState::Off).get() == WR::RadioAccessStatus::Allowed || ok;
            return ok;
        });
    } catch (...) {
        return false;
    }
}
}  // namespace sysx

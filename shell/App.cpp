// XI on Xbox: the UWP app. It signs in (LoginScreen), then runs FFXIRecompile's game host in this
// process. The host is not linked in yet (docs/handoff-gate1.md steps 2-4); for now a successful
// sign-in reports what the host would be started with.
#include "pch.h"

#include "LoginScreen.h"

using namespace winrt;
using namespace Windows::ApplicationModel::Activation;
using namespace Windows::UI::Xaml;

struct App : ApplicationT<App>
{
    LoginScreen m_login;

    void OnLaunched(LaunchActivatedEventArgs const&)
    {
        Window window = Window::Current();
        if (!window.Content())
            window.Content(m_login.Build([this](LoginDetails const& d, SignInResult const& r) { OnSignedIn(d, r); }));
        window.Activate();
    }

    void OnSignedIn(LoginDetails const& d, SignInResult const& r)
    {
        std::wstring text = to_hstring(r.message).c_str();
        text += L"\n\nThe game host is not part of the app yet. It would start with:\n ";
        for (std::string const& a : HostArguments(d, r))
            text += L" " + std::wstring(to_hstring(a == d.password ? std::string("********") : a).c_str());
        m_login.ShowStatus(text, false);
    }
};

int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    init_apartment(apartment_type::single_threaded);
    Application::Start([](auto&&) { make<App>(); });
    return 0;
}

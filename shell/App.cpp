// XI on Xbox: the UWP app. It signs in (LoginScreen), then runs FFXIRecompile's game host in this
// process. The host is not linked in yet (docs/handoff-gate1.md steps 2-4); for now a successful
// sign-in reports what the host would be started with.
#include "pch.h"

#include "LoginScreen.h"

using namespace winrt;
using namespace Windows::ApplicationModel::Activation;
using namespace Windows::UI::Xaml;

namespace
{
    // What went wrong, where it can be read afterwards: LocalState\app.log (on the console, through
    // Device Portal's file explorer).
    void log(std::wstring const& text)
    {
        try
        {
            std::wstring path = std::wstring(Windows::Storage::ApplicationData::Current().LocalFolder().Path()) + L"\\app.log";
            if (FILE* f = _wfopen(path.c_str(), L"a, ccs=UTF-8"))
            {
                fwprintf(f, L"%s\n", text.c_str());
                fclose(f);
            }
        }
        catch (...)
        {
        }
        OutputDebugStringW((text + L"\n").c_str());
    }
}

struct App : ApplicationT<App>
{
    LoginScreen m_login;

    App()
    {
        UnhandledException([](auto&&, UnhandledExceptionEventArgs const& e) {
            log(L"unhandled: " + std::wstring(e.Message().c_str()) + L" (" + std::to_wstring(e.Exception()) + L")");
        });
    }

    void OnLaunched(LaunchActivatedEventArgs const&)
    {
        Window window = Window::Current();
        try
        {
            if (!window.Content())
                window.Content(m_login.Build([this](LoginDetails const& d, SignInResult const& r) { OnSignedIn(d, r); }));
        }
        catch (hresult_error const& e)
        {
            log(L"building the login screen: " + std::wstring(e.message().c_str()) + L" (" + std::to_wstring(e.code()) + L")");
            throw;
        }
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
    // no init_apartment: Application::Start sets the thread up as the UI thread itself
    try
    {
        Application::Start([](auto&&) { make<App>(); });
    }
    catch (hresult_error const& e)
    {
        log(L"Application::Start: " + std::wstring(e.message().c_str()) + L" (" + std::to_wstring(e.code()) + L")");
        return 1;
    }
    return 0;
}

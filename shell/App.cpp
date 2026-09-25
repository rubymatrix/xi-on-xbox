// XI on Xbox: the UWP app. It signs in (LoginScreen), then runs FFXIRecompile's game host in this
// process (GameHost), from FFXIRecompile's build/uwp/ffxi_uwp.lib.
#include "pch.h"

#include "GameHost.h"
#include "LoginScreen.h"
#include "Settings.h"

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
        // Xbox: XAML apps start in "mouse mode", where the controller moves a pointer and A clicks.
        // The controller belongs to the game; the login screen uses it for focus navigation.
        RequiresPointerMode(ApplicationRequiresPointerMode::WhenRequested);
        UnhandledException([](auto&&, UnhandledExceptionEventArgs const& e) {
            log(L"unhandled: " + std::wstring(e.Message().c_str()) + L" (" + std::to_wstring(e.Exception()) + L")");
        });
    }

    void OnLaunched(LaunchActivatedEventArgs const&)
    {
        GameHost::WatchControllers();
        Window window = Window::Current();
        try
        {
            if (!window.Content())
                window.Content(m_login.Build([this](LoginDetails const& d, SignInResult const& r, std::string const& game) {
                    OnSignedIn(d, r, game);
                }));
        }
        catch (hresult_error const& e)
        {
            log(L"building the login screen: " + std::wstring(e.message().c_str()) + L" (" + std::to_wstring(e.code()) + L")");
            throw;
        }
        window.Activate();
    }

    // Signed in: the game takes over the window. When it ends, the app closes, as the game would.
    void OnSignedIn(LoginDetails const& d, SignInResult const& r, std::string const& game)
    {
        GameOptions o;
        o.game_dir = game;
        o.fps = LoadFps();
        o.profile = LoadProfile();
        o.resolution = ParseResolution(LoadResolution()); // {0, 0}: the screen's
        Window::Current().Content(m_game.Start(d, r, o, [](int code) {
            log(L"the game ended: " + std::to_wstring(code));
            SignOut();
        }));
    }

    GameHost m_game;
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

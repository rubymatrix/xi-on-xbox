// Runs FFXIRecompile's game host (host64's main, linked in as host_main from ffxi_uwp.lib) on its
// own thread, and connects it to the app: the window's keyboard, mouse and focus, the controller
// (Windows.Gaming.Input), a log in LocalState, and the picture: the Direct3D 12 back end's swap chain
// in a SwapChainPanel. The log's tail shows on top until the first frame, and again when the game ends.
#pragma once

#include <functional>

#include "Display.h"
#include "Session.h"

struct GameOptions
{
    std::string game_dir; // the FINAL FANTASY XI folder (PlayOnlineViewer next to it)
    bool profile = true;  // FFXI_PROFILE=1: the frame profile every 2 s, for tools/frame_budget.py
    int fps = 30;         // 30 (as the game shipped) or 60: the login screen's choice
    Resolution resolution{ 0, 0 }; // the game's; {0, 0}: the screen's (ScreenResolution)
};

class GameHost
{
public:
    // Starts the game; the returned element is the screen to show while it runs. on_exit is called on
    // the UI thread with host_main's result when the game ends.
    winrt::Windows::UI::Xaml::UIElement Start(LoginDetails const& d, SignInResult const& r, GameOptions const& o,
        std::function<void(int)> on_exit);

    static std::wstring LogPath();

private:
    void HookInput();
    void PollLog();

    winrt::Windows::UI::Xaml::Controls::TextBlock m_title{ nullptr }, m_log{ nullptr };
    winrt::Windows::UI::Xaml::UIElement m_overlay{ nullptr }; // the log, over the picture until it starts
    winrt::Windows::UI::Xaml::DispatcherTimer m_timer{ nullptr };
    std::function<void(int)> m_on_exit;
    bool m_hooked = false;
};

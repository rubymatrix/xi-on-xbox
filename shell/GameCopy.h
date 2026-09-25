// "Copy game from a computer": FINAL FANTASY XI and PlayOnlineViewer copied from a computer on the
// local network running tools/serve_game.py, into the app's storage (LocalState\SquareEnix). For the
// Xbox, which has no other way to take a 14 GB install; the copy then is the game folder.
#pragma once

#include <functional>
#include <string>

// LocalState\SquareEnix\FINAL FANTASY XI, where the copy goes.
std::wstring CopiedGameFolder();
// Whether a finished copy is there (its FFXiMain.dll).
bool HaveCopiedGame();

class GameCopy
{
public:
    // The panel for the login screen. on_done gets the copied game folder, on the UI thread.
    winrt::Windows::UI::Xaml::UIElement Build(std::function<void(std::wstring const&)> on_done);

private:
    winrt::fire_and_forget Start();
    void ShowProgress();

    std::function<void(std::wstring const&)> m_on_done;
    winrt::Windows::UI::Xaml::Controls::TextBox m_address{ nullptr };
    winrt::Windows::UI::Xaml::Controls::Button m_copy{ nullptr };
    winrt::Windows::UI::Xaml::Controls::ProgressBar m_bar{ nullptr };
    winrt::Windows::UI::Xaml::Controls::TextBlock m_status{ nullptr };
    winrt::Windows::UI::Xaml::DispatcherTimer m_timer{ nullptr };
};

// The first screen: the LandSandBoat server, the account and the password.
// Built in code, not XAML markup, so the project needs no XAML compiler or NuGet packages. The
// controls are the stock ones, which work with a keyboard and mouse, a controller, and the Xbox's
// on-screen keyboard.
#pragma once

#include <functional>

#include "GameCopy.h"
#include "Session.h"

class LoginScreen
{
public:
    // Called on the UI thread once the server answers.
    using SignedInHandler = std::function<void(LoginDetails const&, std::string const& game_dir)>;

    winrt::Windows::UI::Xaml::UIElement Build(SignedInHandler on_signed_in);
    void ShowStatus(std::wstring const& text, bool error);

private:
    winrt::fire_and_forget OnSignIn();
    LoginDetails Read() const;
    void SetBusy(bool busy);

    SignedInHandler m_on_signed_in;
    winrt::Windows::UI::Xaml::Controls::TextBox m_server{ nullptr }, m_id{ nullptr }, m_otp{ nullptr }, m_game{ nullptr };
    winrt::Windows::UI::Xaml::Controls::PasswordBox m_password{ nullptr };
    winrt::Windows::UI::Xaml::Controls::CheckBox m_remember{ nullptr }, m_fps60{ nullptr }, m_profile{ nullptr };
    winrt::Windows::UI::Xaml::Controls::ComboBox m_resolution{ nullptr };
    std::vector<std::string> m_resolutions; // per item: "" (the screen's) or "WxH"
    winrt::Windows::UI::Xaml::Controls::Button m_signin{ nullptr };
    winrt::Windows::UI::Xaml::Controls::ProgressRing m_busy{ nullptr };
    winrt::Windows::UI::Xaml::Controls::TextBlock m_status{ nullptr };
    GameCopy m_copy;
};

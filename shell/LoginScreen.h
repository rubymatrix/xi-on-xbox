// The first screen: how to sign in (PlayOnline or direct), the server, the ID and the password.
// Built in code, not XAML markup, so the project needs no XAML compiler or NuGet packages. The
// controls are the stock ones, which work with a keyboard and mouse, a controller, and the Xbox's
// on-screen keyboard.
#pragma once

#include <functional>

#include "Session.h"

class LoginScreen
{
public:
    // Called on the UI thread once signed in, with the host's sign-in arguments.
    using SignedInHandler = std::function<void(LoginDetails const&, SignInResult const&)>;

    winrt::Windows::UI::Xaml::UIElement Build(SignedInHandler on_signed_in);
    void ShowStatus(std::wstring const& text, bool error);

private:
    winrt::fire_and_forget OnSignIn();
    void ShowMode(LoginMode m);
    LoginMode Mode() const;
    LoginDetails Read() const;
    void SetBusy(bool busy);

    SignedInHandler m_on_signed_in;
    winrt::Windows::UI::Xaml::Controls::RadioButton m_pol{ nullptr }, m_direct{ nullptr };
    winrt::Windows::UI::Xaml::Controls::TextBox m_server{ nullptr }, m_id{ nullptr }, m_otp{ nullptr };
    winrt::Windows::UI::Xaml::Controls::PasswordBox m_password{ nullptr };
    winrt::Windows::UI::Xaml::Controls::CheckBox m_remember{ nullptr };
    winrt::Windows::UI::Xaml::Controls::Button m_signin{ nullptr };
    winrt::Windows::UI::Xaml::Controls::ProgressRing m_busy{ nullptr };
    winrt::Windows::UI::Xaml::Controls::TextBlock m_status{ nullptr };
    bool m_loading = false;
};

#include "pch.h"

#include "LoginScreen.h"
#include "Settings.h"

using namespace winrt;
using namespace Windows::UI;
using namespace Windows::UI::Core;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Media;
namespace Xin = Windows::UI::Xaml::Input;

namespace
{
    TextBox text_box(wchar_t const* header, wchar_t const* placeholder)
    {
        TextBox t;
        t.Header(box_value(header));
        t.PlaceholderText(placeholder);
        t.IsSpellCheckEnabled(false);
        t.IsTextPredictionEnabled(false);
        return t;
    }
}

UIElement LoginScreen::Build(SignedInHandler on_signed_in)
{
    m_on_signed_in = std::move(on_signed_in);

    StackPanel panel;
    panel.MaxWidth(440);
    panel.Spacing(14);
    panel.Padding(ThicknessHelper::FromUniformLength(24));
    panel.HorizontalAlignment(HorizontalAlignment::Stretch);
    panel.VerticalAlignment(VerticalAlignment::Center);

    TextBlock title;
    title.Text(L"FINAL FANTASY XI");
    title.FontSize(34);
    title.FontWeight(Text::FontWeights::SemiLight());
    panel.Children().Append(title);

    TextBlock how;
    how.Text(L"Sign in with");
    how.Margin(ThicknessHelper::FromLengths(0, 8, 0, 0));
    panel.Children().Append(how);
    StackPanel modes;
    modes.Orientation(Orientation::Horizontal);
    modes.Spacing(8);
    m_pol = RadioButton();
    m_pol.Content(box_value(L"PlayOnline"));
    m_pol.GroupName(L"mode");
    m_direct = RadioButton();
    m_direct.Content(box_value(L"Direct (LandSandBoat)"));
    m_direct.GroupName(L"mode");
    modes.Children().Append(m_pol);
    modes.Children().Append(m_direct);
    panel.Children().Append(modes);

    m_server = text_box(L"Server", L"10.0.1.10");
    m_server.InputScope([] {
        Xin::InputScope s;
        Xin::InputScopeName n;
        n.NameValue(Xin::InputScopeNameValue::Url);
        s.Names().Append(n);
        return s;
    }());
    panel.Children().Append(m_server);
    m_id = text_box(L"PlayOnline ID", L"");
    panel.Children().Append(m_id);
    m_password = PasswordBox();
    m_password.Header(box_value(L"Password"));
    panel.Children().Append(m_password);
    m_otp = text_box(L"One-time code (only if the account has two-factor sign-in)", L"");
    panel.Children().Append(m_otp);
    m_remember = CheckBox();
    m_remember.Content(box_value(L"Remember the password on this device"));
    panel.Children().Append(m_remember);

    StackPanel actions;
    actions.Orientation(Orientation::Horizontal);
    actions.Spacing(16);
    m_signin = Button();
    m_signin.Content(box_value(L"Sign in"));
    m_signin.MinWidth(140);
    if (auto accent = Application::Current().Resources().TryLookup(box_value(L"AccentButtonStyle")))
        m_signin.Style(accent.as<Style>());
    m_busy = ProgressRing();
    m_busy.Width(28);
    m_busy.Height(28);
    actions.Children().Append(m_signin);
    actions.Children().Append(m_busy);
    panel.Children().Append(actions);

    m_status = TextBlock();
    m_status.TextWrapping(TextWrapping::Wrap);
    m_status.IsTextSelectionEnabled(true);
    panel.Children().Append(m_status);

    // what was used last time
    m_loading = true;
    SavedLogin saved = LoadLogin();
    (saved.details.mode == LoginMode::Direct ? m_direct : m_pol).IsChecked(true);
    ShowMode(saved.details.mode);
    m_server.Text(to_hstring(saved.details.server));
    m_id.Text(to_hstring(saved.details.id));
    m_password.Password(to_hstring(saved.details.password));
    m_remember.IsChecked(saved.remember_password);
    m_loading = false;

    auto mode_changed = [this](auto&&, auto&&) {
        if (m_loading)
            return;
        ShowMode(Mode());
        m_id.Text(to_hstring(SavedId(Mode())));
        m_password.Password(L"");
    };
    m_pol.Checked(mode_changed);
    m_direct.Checked(mode_changed);
    m_signin.Click([this](auto&&, auto&&) { OnSignIn(); });
    m_password.KeyDown([this](auto&&, Xin::KeyRoutedEventArgs const& e) {
        if (e.Key() == Windows::System::VirtualKey::Enter && m_signin.IsEnabled())
            OnSignIn();
    });

    ScrollViewer scroll;
    scroll.Content(panel);
    scroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
    Grid root;
    root.RequestedTheme(ElementTheme::Dark);
    root.Background(SolidColorBrush(ColorHelper::FromArgb(255, 16, 18, 24)));
    root.Children().Append(scroll);
    return root;
}

LoginMode LoginScreen::Mode() const
{
    return m_direct.IsChecked() && m_direct.IsChecked().Value() ? LoginMode::Direct : LoginMode::PlayOnline;
}

void LoginScreen::ShowMode(LoginMode m)
{
    bool pol = m == LoginMode::PlayOnline;
    m_id.Header(box_value(pol ? L"PlayOnline ID" : L"Account"));
    m_id.PlaceholderText(pol ? L"NAAB1234" : L"");
    m_otp.Visibility(pol ? Visibility::Collapsed : Visibility::Visible);
    m_server.Header(box_value(pol ? L"PlayOnline server" : L"LandSandBoat server"));
}

LoginDetails LoginScreen::Read() const
{
    LoginDetails d;
    d.mode = Mode();
    d.server = to_string(m_server.Text());
    d.id = to_string(m_id.Text());
    d.password = to_string(m_password.Password());
    if (d.mode == LoginMode::Direct)
        d.otp = to_string(m_otp.Text());
    auto trim = [](std::string& s) {
        s.erase(0, s.find_first_not_of(" \t"));
        s.erase(s.find_last_not_of(" \t") + 1);
    };
    trim(d.server), trim(d.id), trim(d.otp);
    if (d.mode == LoginMode::PlayOnline)
        for (char& c : d.id)
            c = (char)toupper((unsigned char)c);
    return d;
}

void LoginScreen::SetBusy(bool busy)
{
    m_busy.IsActive(busy);
    for (Control c : { Control(m_signin), Control(m_pol), Control(m_direct), Control(m_server), Control(m_id),
                       Control(m_password), Control(m_otp), Control(m_remember) })
        c.IsEnabled(!busy);
}

void LoginScreen::ShowStatus(std::wstring const& text, bool error)
{
    m_status.Text(text);
    m_status.Foreground(SolidColorBrush(error ? ColorHelper::FromArgb(255, 255, 120, 110) : Colors::LightGray()));
}

fire_and_forget LoginScreen::OnSignIn()
{
    LoginDetails d = Read();
    if (d.server.empty() || d.id.empty() || d.password.empty())
    {
        ShowStatus(L"Fill in the server, the " + std::wstring(d.mode == LoginMode::PlayOnline ? L"PlayOnline ID" : L"account") +
                       L" and the password.",
                   true);
        co_return;
    }
    bool remember = m_remember.IsChecked() && m_remember.IsChecked().Value();
    SaveLogin(d, remember);
    SetBusy(true);
    ShowStatus(L"Signing in...", false);

    CoreDispatcher ui = Window::Current().Dispatcher();
    co_await resume_background();
    SignInResult r = SignIn(d);
    co_await resume_foreground(ui);

    SetBusy(false);
    ShowStatus(to_hstring(r.message).c_str(), !r.ok);
    if (r.ok && m_on_signed_in)
        m_on_signed_in(d, r);
}

#include "pch.h"

#include "Display.h"
#include "GameCopy.h"
#include "LoginScreen.h"
#include "Settings.h"

using namespace winrt;
using namespace Windows::UI;
using namespace Windows::UI::Core;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Media;
namespace Xin = Windows::UI::Xaml::Input;

// This machine's test login, to pre-fill the screen: shell\LocalDefaults.h, which git ignores (it
// holds a password). Any of these may be left out.
#if __has_include("LocalDefaults.h")
#include "LocalDefaults.h"
#endif
#ifndef LOCAL_SERVER
#define LOCAL_SERVER "game.phoenix-xi.com" // the default server (direct, LandSandBoat)
#endif
#ifndef LOCAL_POL_ID
#define LOCAL_POL_ID ""
#endif
#ifndef LOCAL_POL_PASSWORD
#define LOCAL_POL_PASSWORD ""
#endif
#ifndef LOCAL_ACCOUNT
#define LOCAL_ACCOUNT ""
#endif
#ifndef LOCAL_PASSWORD
#define LOCAL_PASSWORD ""
#endif
#ifndef LOCAL_GAME_DIR
#define LOCAL_GAME_DIR ""
#endif

namespace
{
    std::string or_default(std::string const& saved, char const* fallback) { return saved.empty() ? fallback : saved; }

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

    m_server = text_box(L"Server", L"game.phoenix-xi.com");
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
    m_game = text_box(L"FINAL FANTASY XI folder", L"C:\\...\\SquareEnix\\FINAL FANTASY XI");
    panel.Children().Append(m_game);
    // no install to point at (an Xbox): copy one from a computer on the network
    UIElement copy = m_copy.Build([this](std::wstring const& folder) { m_game.Text(folder); });
    panel.Children().Append(copy);
    m_remember = CheckBox();
    m_remember.Content(box_value(L"Remember the password on this device"));
    panel.Children().Append(m_remember);
    m_fps60 = CheckBox();
    m_fps60.Content(box_value(L"60 frames per second (the game shipped at 30)"));
    panel.Children().Append(m_fps60);

    // the game's resolution: the screen's, found now, or a fixed one
    m_resolution = ComboBox();
    m_resolution.Header(box_value(L"Resolution"));
    m_resolution.HorizontalAlignment(HorizontalAlignment::Stretch);
    Resolution screen = ScreenResolution();
    m_resolution.Items().Append(box_value(L"Match the screen (" + to_hstring(screen.first) + L" × " + to_hstring(screen.second) + L")"));
    m_resolutions = { "" };
    for (Resolution r : ResolutionChoices())
    {
        m_resolution.Items().Append(box_value(to_hstring(r.first) + L" × " + to_hstring(r.second)));
        m_resolutions.push_back(ToText(r));
    }
    panel.Children().Append(m_resolution);

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

    // what was used last time; where nothing was, this machine's defaults (LocalDefaults.h)
    m_loading = true;
    SavedLogin saved = LoadLogin();
    (saved.details.mode == LoginMode::Direct ? m_direct : m_pol).IsChecked(true);
    ShowMode(saved.details.mode);
    bool pol = saved.details.mode == LoginMode::PlayOnline;
    m_server.Text(to_hstring(or_default(saved.details.server, LOCAL_SERVER)));
    m_id.Text(to_hstring(or_default(saved.details.id, pol ? LOCAL_POL_ID : LOCAL_ACCOUNT)));
    m_password.Password(to_hstring(or_default(saved.details.password, pol ? LOCAL_POL_PASSWORD : LOCAL_PASSWORD)));
    m_remember.IsChecked(saved.remember_password);
    m_fps60.IsChecked(LoadFps() == 60);
    {
        std::string res = LoadResolution();
        auto it = std::find(m_resolutions.begin(), m_resolutions.end(), res);
        m_resolution.SelectedIndex(it == m_resolutions.end() ? 0 : (int32_t)(it - m_resolutions.begin()));
    }
    // the game folder: the one beside the app if it runs from one that holds it, else the one used
    // last, else a copy made here, else this machine's default
    std::string beside = to_string(PackagedGameFolder());
    std::string copied = HaveCopiedGame() ? to_string(CopiedGameFolder()) : std::string();
    m_game.Text(to_hstring(!beside.empty() ? beside : or_default(saved.game_dir, copied.empty() ? LOCAL_GAME_DIR : copied.c_str())));
    m_loading = false;

    auto mode_changed = [this](auto&&, auto&&) {
        if (m_loading)
            return;
        bool pol = Mode() == LoginMode::PlayOnline;
        ShowMode(Mode());
        m_id.Text(to_hstring(or_default(SavedId(Mode()), pol ? LOCAL_POL_ID : LOCAL_ACCOUNT)));
        m_password.Password(to_hstring(std::string(pol ? LOCAL_POL_PASSWORD : LOCAL_PASSWORD)));
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
                       Control(m_password), Control(m_otp), Control(m_game), Control(m_remember), Control(m_fps60), Control(m_resolution) })
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
    std::string game = to_string(m_game.Text());
    while (!game.empty() && (game.back() == '\\' || game.back() == '/' || game.back() == ' '))
        game.pop_back();
    DWORD attrs = GetFileAttributesW(to_hstring(game).c_str());
    if (game.empty() || attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY))
    {
        ShowStatus(game.empty() ? L"Fill in the FINAL FANTASY XI folder."
                                : L"The app cannot open that FINAL FANTASY XI folder (error " + std::to_wstring(GetLastError()) +
                                      L"). It has to exist, and be readable by apps.",
                   true);
        co_return;
    }
    bool remember = m_remember.IsChecked() && m_remember.IsChecked().Value();
    SaveLogin(d, remember, game);
    SaveFps(m_fps60.IsChecked() && m_fps60.IsChecked().Value() ? 60 : 30);
    {
        int32_t i = m_resolution.SelectedIndex();
        SaveResolution(i > 0 && i < (int32_t)m_resolutions.size() ? m_resolutions[(size_t)i] : "");
    }
    SetBusy(true);
    ShowStatus(L"Signing in...", false);

    CoreDispatcher ui = Window::Current().Dispatcher();
    co_await resume_background();
    SignInResult r = SignIn(d);
    co_await resume_foreground(ui);

    SetBusy(false);
    ShowStatus(to_hstring(r.message).c_str(), !r.ok);
    if (r.ok && m_on_signed_in)
        m_on_signed_in(d, r, game);
}

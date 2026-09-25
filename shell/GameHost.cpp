#include "pch.h"

#include "GameHost.h"

#include "uwp_bridge.h"

extern "C" int host_main(int argc, char** argv); // host/host64.c, built with /Dmain=host_main

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Gaming::Input;
using namespace Windows::Networking;
using namespace Windows::Networking::Sockets;
using namespace Windows::Security::Cryptography::Certificates;
using namespace Windows::Storage;
using namespace Windows::Storage::Streams;
using namespace Windows::System;
using namespace Windows::UI::Core;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Media;

namespace
{
    std::atomic<bool> g_running;
    std::atomic<int> g_exit_code;

    std::wstring local_folder() { return std::wstring(ApplicationData::Current().LocalFolder().Path().c_str()); }

    std::string utf8(std::wstring const& w) { return to_string(w); }

    // --- the keyboard: VirtualKey -> SDL_Scancode (USB HID usage) ---------------------------------------
    int scancode_of(VirtualKey k)
    {
        int v = (int)k;
        if (v >= 'A' && v <= 'Z')
            return 4 + (v - 'A');
        if (v >= '1' && v <= '9')
            return 30 + (v - '1');
        if (v == '0')
            return 39;
        if (v >= (int)VirtualKey::F1 && v <= (int)VirtualKey::F12)
            return 58 + (v - (int)VirtualKey::F1);
        if (v >= (int)VirtualKey::NumberPad1 && v <= (int)VirtualKey::NumberPad9)
            return 89 + (v - (int)VirtualKey::NumberPad1);
        switch (k)
        {
        case VirtualKey::NumberPad0: return 98;
        case VirtualKey::Enter: return 40;
        case VirtualKey::Escape: return 41;
        case VirtualKey::Back: return 42;
        case VirtualKey::Tab: return 43;
        case VirtualKey::Space: return 44;
        case VirtualKey::CapitalLock: return 57;
        case VirtualKey::Snapshot: return 70;
        case VirtualKey::Scroll: return 71;
        case VirtualKey::Pause: return 72;
        case VirtualKey::Insert: return 73;
        case VirtualKey::Home: return 74;
        case VirtualKey::PageUp: return 75;
        case VirtualKey::Delete: return 76;
        case VirtualKey::End: return 77;
        case VirtualKey::PageDown: return 78;
        case VirtualKey::Right: return 79;
        case VirtualKey::Left: return 80;
        case VirtualKey::Down: return 81;
        case VirtualKey::Up: return 82;
        case VirtualKey::NumberKeyLock: return 83;
        case VirtualKey::Divide: return 84;
        case VirtualKey::Multiply: return 85;
        case VirtualKey::Subtract: return 86;
        case VirtualKey::Add: return 87;
        case VirtualKey::Decimal: return 99;
        case VirtualKey::LeftControl: case VirtualKey::Control: return 224;
        case VirtualKey::LeftShift: case VirtualKey::Shift: return 225;
        case VirtualKey::LeftMenu: case VirtualKey::Menu: return 226;
        case VirtualKey::RightControl: return 228;
        case VirtualKey::RightShift: return 229;
        case VirtualKey::RightMenu: return 230;
        default: break;
        }
        switch (v) // the OEM keys (US layout)
        {
        case 0xBD: return 45; // - _
        case 0xBB: return 46; // = +
        case 0xDB: return 47; // [
        case 0xDD: return 48; // ]
        case 0xDC: return 49; // backslash
        case 0xBA: return 51; // ;
        case 0xDE: return 52; // '
        case 0xC0: return 53; // `
        case 0xBC: return 54; // ,
        case 0xBE: return 55; // .
        case 0xBF: return 56; // /
        default: return 0;
        }
    }

    // --- the controller: Windows.Gaming.Input -> SDL's buttons and axes --------------------------------------
    void poll_gamepad()
    {
        UwpPad pad{};
        auto pads = Gamepad::Gamepads();
        if (pads.Size())
        {
            GamepadReading r = pads.GetAt(0).GetCurrentReading();
            pad.connected = 1;
            struct { GamepadButtons from; int to; } const MAP[] = {
                { GamepadButtons::A, 0 }, { GamepadButtons::B, 1 }, { GamepadButtons::X, 2 }, { GamepadButtons::Y, 3 },
                { GamepadButtons::View, 4 }, { GamepadButtons::Menu, 6 }, { GamepadButtons::LeftThumbstick, 7 },
                { GamepadButtons::RightThumbstick, 8 }, { GamepadButtons::LeftShoulder, 9 },
                { GamepadButtons::RightShoulder, 10 }, { GamepadButtons::DPadUp, 11 }, { GamepadButtons::DPadDown, 12 },
                { GamepadButtons::DPadLeft, 13 }, { GamepadButtons::DPadRight, 14 },
            };
            for (auto const& m : MAP)
                if ((r.Buttons & m.from) == m.from)
                    pad.buttons |= 1u << m.to;
            auto axis = [](double v) { return (int16_t)std::clamp(v * 32767.0, -32768.0, 32767.0); };
            pad.axes[0] = axis(r.LeftThumbstickX);
            pad.axes[1] = axis(-r.LeftThumbstickY); // SDL: down is positive
            pad.axes[2] = axis(r.RightThumbstickX);
            pad.axes[3] = axis(-r.RightThumbstickY);
            pad.axes[4] = axis(r.LeftTrigger);
            pad.axes[5] = axis(r.RightTrigger);
        }
        uwp_gamepad(&pad);
    }

    // --- the picture: the D3D12 back end's swap chain in a SwapChainPanel -----------------------------------
    SwapChainPanel g_panel{ nullptr };
    CoreDispatcher g_ui{ nullptr };
    std::function<void()> g_on_attached;

    // The view's size in pixels.
    std::pair<int, int> view_pixels()
    {
        auto b = Window::Current().CoreWindow().Bounds();
        double scale = Windows::Graphics::Display::DisplayInformation::GetForCurrentView().RawPixelsPerViewPixel();
        return { (int)(b.Width * scale + 0.5), (int)(b.Height * scale + 0.5) };
    }

    // The game's settings (its registry key) for a player who brought none: the view's resolution,
    // windowed (the view is the window), and the retail controller layout. Written once; edit or replace
    // LocalState\settings.reg to change them.
    void write_default_settings(std::wstring const& path)
    {
        auto [w, h] = view_pixels();
        if (w < 640 || h < 480)
            w = 1920, h = 1080;
        FILE* f = _wfopen(path.c_str(), L"w");
        if (!f)
            return;
        fprintf(f,
            "REGEDIT4\n\n"
            "[HKEY_LOCAL_MACHINE\\SOFTWARE\\PlayOnlineUS\\SquareEnix\\FinalFantasyXI]\n"
            "\"0000\"=dword:00000006\n"          // mip mapping
            "\"0001\"=dword:%08x\n"              // window width
            "\"0002\"=dword:%08x\n"              // window height
            "\"0003\"=dword:00001000\n"          // background resolution
            "\"0004\"=dword:00001000\n"
            "\"0007\"=dword:00000001\n"          // sound
            "\"0011\"=dword:00000001\n"          // environment animation
            "\"0017\"=dword:00000001\n"          // bump mapping
            "\"0018\"=dword:00000001\n"          // texture compression
            "\"0019\"=dword:00000001\n"          // map compression
            "\"0022\"=dword:00000001\n"          // hardware mouse
            "\"0034\"=dword:00000001\n"          // windowed
            "\"0035\"=dword:00000001\n"          // always on top
            "\"0037\"=dword:%08x\n"              // interface (menu) resolution
            "\"0038\"=dword:%08x\n"
            "\"padmode000\"=\"1,1,0,0,0,1\"\n"
            "\"padsin000\"=\"8,9,13,12,10,0,1,3,2,15,-1,-1,14,-33,-33,32,32,-36,-36,35,35,6,7,5,4,11,-1\"\n",
            w, h, w / 2, h / 2);
        fclose(f);
    }

    void rumble(uint16_t low, uint16_t high)
    {
        auto pads = Gamepad::Gamepads();
        if (!pads.Size())
            return;
        GamepadVibration v{};
        v.LeftMotor = low / 65535.0;
        v.RightMotor = high / 65535.0;
        pads.GetAt(0).Vibration(v);
    }
}

// --- the LandSandBoat sign-in's TLS (uwp_bridge.h) ------------------------------------------------------
extern "C" int uwp_tls_exchange(uint32_t server, uint16_t port, const char* request, char* reply, size_t replyn, char* err,
    size_t errn)
{
    char host[16];
    snprintf(host, sizeof host, "%u.%u.%u.%u", server >> 24, (server >> 16) & 255, (server >> 8) & 255, server & 255);
    try
    {
        StreamSocket s;
        // private servers present self-signed certificates, as xiloader accepts
        for (auto e : { ChainValidationResult::Untrusted, ChainValidationResult::InvalidName, ChainValidationResult::Expired,
                        ChainValidationResult::IncompleteChain, ChainValidationResult::WrongUsage,
                        ChainValidationResult::RevocationInformationMissing, ChainValidationResult::RevocationFailure })
            s.Control().IgnorableServerCertificateErrors().Append(e);
        s.ConnectAsync(HostName(to_hstring(host)), to_hstring(port), SocketProtectionLevel::Tls12).get();
        DataWriter w(s.OutputStream());
        w.WriteBytes(array_view<uint8_t const>((uint8_t const*)request, (uint32_t)strlen(request)));
        w.StoreAsync().get();
        w.DetachStream();
        DataReader r(s.InputStream());
        r.InputStreamOptions(InputStreamOptions::Partial);
        uint32_t n = r.LoadAsync((uint32_t)(replyn - 1)).get(); // the first record with data in it
        if (!n)
        {
            snprintf(err, errn, "the login server did not reply");
            return 0;
        }
        std::vector<uint8_t> buf(n);
        r.ReadBytes(buf);
        memcpy(reply, buf.data(), n);
        reply[n] = 0;
        return 1;
    }
    catch (hresult_error const& e)
    {
        snprintf(err, errn, "TLS to %s:%u: %s", host, port, to_string(e.message()).c_str());
        return 0;
    }
}

// --- the swap chain (uwp_bridge.h): SwapChainPanel wants it on the UI thread -------------------------------
extern "C" void uwp_attach_swapchain(void* swap_chain)
{
    com_ptr<IDXGISwapChain1> chain;
    chain.copy_from(static_cast<IDXGISwapChain1*>(swap_chain));
    HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_ui.RunAsync(CoreDispatcherPriority::High, [chain, done] {
        HRESULT hr = g_panel.as<ISwapChainPanelNative>()->SetSwapChain(chain.get());
        // the buffers are in pixels; the panel lays out in view pixels
        if (auto sc2 = chain.try_as<IDXGISwapChain2>())
        {
            DXGI_MATRIX_3X2_F m{};
            m._11 = 1.0f / g_panel.CompositionScaleX();
            m._22 = 1.0f / g_panel.CompositionScaleY();
            sc2->SetMatrixTransform(&m);
        }
        fprintf(stderr, "[app] swap chain in the panel (%08lx)\n", (unsigned long)hr);
        if (SUCCEEDED(hr) && g_on_attached)
            g_on_attached();
        SetEvent(done);
    });
    WaitForSingleObject(done, INFINITE);
    CloseHandle(done);
}

std::wstring GameHost::LogPath() { return local_folder() + L"\\host64.log"; }

UIElement GameHost::Start(LoginDetails const& d, SignInResult const& r, GameOptions const& o, std::function<void(int)> on_exit)
{
    m_on_exit = std::move(on_exit);

    // the host's command line: argv[0] in LocalState, where it writes patch.<version>.ver
    std::wstring local = local_folder();
    CreateDirectoryW((local + L"\\USER").c_str(), nullptr);
    std::vector<std::string> args = { utf8(local + L"\\host64.exe"), "--game", o.game_dir, "--user-dir", utf8(local + L"\\USER") };
    std::wstring overlay = local + L"\\settings.reg"; // the game's settings: ours, until one is put there
    if (GetFileAttributesW(overlay.c_str()) == INVALID_FILE_ATTRIBUTES)
        write_default_settings(overlay);
    args.insert(args.end(), { "--reg-overlay", utf8(overlay) });
    args.insert(args.end(), { "--fps-divisor", std::to_string(60 / o.fps) }); // the game's own frame pacing
    for (auto& a : HostArguments(d, r))
        args.push_back(a);

    _putenv_s("FFXI_PROFILE", o.profile ? "1" : "0");
    _putenv_s("FFXI_CACHE_DIR", utf8(local).c_str()); // the D3D12 pipeline cache
    if (o.profile) // where the game thread's time goes (FFXIRecompile's tools/sample_report.py)
        _putenv_s("FFXI_SAMPLE", utf8(local + L"\\samples.bin").c_str());
    // stdout and stderr (every [recomp] and [gfx] line) to LocalState\host64.log
    std::wstring log = LogPath();
    _wfreopen(log.c_str(), L"w", stderr);
    _wfreopen((local + L"\\host64.out.log").c_str(), L"w", stdout);
    setvbuf(stderr, nullptr, _IONBF, 0);
    fprintf(stderr, "[app] starting the game: %s\n", o.game_dir.c_str());

    uwp_set_rumble(rumble);
    HookInput();

    g_running = true;
    std::thread([args, ui = Window::Current().Dispatcher(), this]() mutable {
        std::vector<char*> argv;
        for (auto& a : args)
            argv.push_back(a.data());
        argv.push_back(nullptr);
        int code = host_main((int)args.size(), argv.data());
        fprintf(stderr, "[app] the game host returned %d\n", code);
        fflush(stderr);
        g_exit_code = code;
        g_running = false;
        ui.RunAsync(CoreDispatcherPriority::Normal, [this, code] {
            if (m_timer)
                m_timer.Stop();
            PollLog();
            if (m_on_exit)
                m_on_exit(code);
        });
    }).detach();

    // the screen: the game's swap chain, with its log on top until the first frame is shown
    StackPanel panel;
    panel.Padding(ThicknessHelper::FromUniformLength(32));
    panel.Spacing(12);
    m_overlay = panel;
    m_title = TextBlock();
    m_title.Text(L"Starting FINAL FANTASY XI...");
    m_title.FontSize(24);
    panel.Children().Append(m_title);
    TextBlock where;
    where.Text(L"Log: " + hstring(log));
    where.Opacity(0.7);
    where.IsTextSelectionEnabled(true);
    panel.Children().Append(where);
    m_log = TextBlock();
    m_log.FontFamily(FontFamily(L"Consolas"));
    m_log.FontSize(13);
    m_log.TextWrapping(TextWrapping::NoWrap);
    panel.Children().Append(m_log);
    Grid root;
    root.RequestedTheme(ElementTheme::Dark);
    root.Background(SolidColorBrush(Windows::UI::ColorHelper::FromArgb(255, 0, 0, 0)));
    g_panel = SwapChainPanel();
    g_ui = Window::Current().Dispatcher();
    g_on_attached = [this] { m_overlay.Visibility(Visibility::Collapsed); };
    root.Children().Append(g_panel);
    root.Children().Append(panel);

    m_timer = DispatcherTimer();
    m_timer.Interval(std::chrono::milliseconds(1000));
    m_timer.Tick([this](auto&&, auto&&) { PollLog(); });
    m_timer.Start();
    return root;
}

void GameHost::PollLog()
{
    // the last lines of the log
    FILE* f = _wfopen(LogPath().c_str(), L"rb");
    if (!f)
        return;
    fseek(f, 0, SEEK_END);
    long size = ftell(f), from = size > 6000 ? size - 6000 : 0;
    fseek(f, from, SEEK_SET);
    std::string tail((size_t)(size - from), '\0');
    tail.resize(fread(tail.data(), 1, tail.size(), f));
    fclose(f);
    size_t cut = 0;
    int lines = 0;
    for (size_t i = tail.size(); i-- > 0;)
        if (tail[i] == '\n' && ++lines > 28)
        {
            cut = i + 1;
            break;
        }
    m_log.Text(to_hstring(tail.substr(cut)));
    if (!g_running)
    {
        m_title.Text(L"The game has ended (host returned " + to_hstring(g_exit_code.load()) + L")");
        m_overlay.Visibility(Visibility::Visible);
    }
}

void GameHost::HookInput()
{
    if (m_hooked)
        return;
    m_hooked = true;
    CoreWindow w = Window::Current().CoreWindow();
    auto b = w.Bounds();
    float scale = Windows::Graphics::Display::DisplayInformation::GetForCurrentView().RawPixelsPerViewPixel();
    uwp_view_size((int)(b.Width * scale), (int)(b.Height * scale));
    w.SizeChanged([scale](CoreWindow const&, WindowSizeChangedEventArgs const& e) {
        uwp_view_size((int)(e.Size().Width * scale), (int)(e.Size().Height * scale));
    });
    w.KeyDown([](CoreWindow const&, KeyEventArgs const& e) {
        if (int sc = scancode_of(e.VirtualKey()))
        {
            uwp_key(sc, 1, e.KeyStatus().WasKeyDown);
            e.Handled(true);
        }
    });
    w.KeyUp([](CoreWindow const&, KeyEventArgs const& e) {
        if (int sc = scancode_of(e.VirtualKey()))
        {
            uwp_key(sc, 0, 0);
            e.Handled(true);
        }
    });
    w.CharacterReceived([](CoreWindow const&, CharacterReceivedEventArgs const& e) {
        uint32_t c = e.KeyCode();
        if (c < 32 || c == 127)
            return; // Enter, Backspace, Tab, Escape arrive as keys
        wchar_t wc[2] = { (wchar_t)c, 0 };
        uwp_text(to_string(wc).c_str());
    });
    w.PointerMoved([scale](CoreWindow const&, PointerEventArgs const& e) {
        auto p = e.CurrentPoint().Position();
        uwp_mouse_move(p.X * scale, p.Y * scale);
    });
    auto buttons = [](PointerEventArgs const& e) {
        static bool was[3];
        auto props = e.CurrentPoint().Properties();
        bool now[3] = { props.IsLeftButtonPressed(), props.IsMiddleButtonPressed(), props.IsRightButtonPressed() };
        for (int i = 0; i < 3; ++i)
            if (now[i] != was[i])
                uwp_mouse_button(i + 1, now[i]), was[i] = now[i]; // SDL: 1 left, 2 middle, 3 right
    };
    w.PointerPressed([buttons](CoreWindow const&, PointerEventArgs const& e) { buttons(e); });
    w.PointerReleased([buttons](CoreWindow const&, PointerEventArgs const& e) { buttons(e); });
    w.PointerWheelChanged([](CoreWindow const&, PointerEventArgs const& e) {
        uwp_mouse_wheel(e.CurrentPoint().Properties().MouseWheelDelta() / 120.0f);
    });
    w.Activated([](CoreWindow const&, WindowActivatedEventArgs const& e) {
        uwp_focus(e.WindowActivationState() != CoreWindowActivationState::Deactivated);
    });
    uwp_focus(1);

    // the controller, 125 times a second
    std::thread([] {
        while (true)
        {
            poll_gamepad();
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
    }).detach();
}

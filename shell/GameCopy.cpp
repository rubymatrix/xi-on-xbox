#include "pch.h"

#include "GameCopy.h"

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Storage;
using namespace Windows::Storage::Streams;
using namespace Windows::UI::Core;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::Web::Http;
using namespace Windows::Web::Http::Filters;

namespace
{
    constexpr int WORKERS = 8; // at once: the install is tens of thousands of files, most small

    struct Entry
    {
        uint64_t size;
        std::wstring path; // "FINAL FANTASY XI/ROM/0/0.DAT"
    };

    // the copy in progress (one at a time), read by the progress display
    std::atomic<bool> g_running;
    std::atomic<uint64_t> g_bytes_done, g_bytes_total, g_files_done, g_files_total, g_failed;
    std::mutex g_error_lock;
    std::wstring g_error;
    std::chrono::steady_clock::time_point g_started;

    std::wstring local_folder() { return std::wstring(ApplicationData::Current().LocalFolder().Path().c_str()); }

    // A folder that cannot be made is reported with its own error: ignored, it surfaced as the file's
    // "path not found" (3), hiding the real cause.
    void make_dirs(std::wstring const& file)
    {
        for (size_t i = local_folder().size() + 1; (i = file.find(L'\\', i)) != std::wstring::npos; ++i)
            if (!CreateDirectoryW(file.substr(0, i).c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
            {
                DWORD err = GetLastError();
                throw hresult_error(HRESULT_FROM_WIN32(err), L"cannot make the folder " + hstring(file.substr(0, i)) +
                                                                 L" (Windows error " + to_hstring((uint32_t)err) + L")");
            }
    }

    // What the app's storage has left, as Windows reports it for LocalState; "" if it will not say.
    std::wstring free_space()
    {
        ULARGE_INTEGER avail{}, total{};
        if (!GetDiskFreeSpaceExW(local_folder().c_str(), &avail, &total, nullptr))
            return L"";
        wchar_t s[96];
        swprintf(s, 96, L"%.1f GB free of %.1f GB", avail.QuadPart / 1e9, total.QuadPart / 1e9);
        return s;
    }

    uint64_t file_size(std::wstring const& path)
    {
        WIN32_FILE_ATTRIBUTE_DATA a;
        if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a))
            return UINT64_MAX;
        return ((uint64_t)a.nFileSizeHigh << 32) | a.nFileSizeLow;
    }

    std::wstring url_for(std::wstring const& base, std::wstring const& path)
    {
        std::wstring u = base + L"/file";
        size_t from = 0;
        for (;;)
        {
            size_t slash = path.find(L'/', from);
            u += L"/" + std::wstring(Uri::EscapeComponent(path.substr(from, slash - from)).c_str());
            if (slash == std::wstring::npos)
                return u;
            from = slash + 1;
        }
    }

    // An operation's result, or a timeout error (the operation cancelled) after secs. Without it a
    // stalled connection held its download forever, and with every download held the copy stopped.
    constexpr int CONNECT_SECS = 20, IDLE_SECS = 30;
    template <typename Op>
    auto within(Op const& op, int secs)
    {
        if (op.wait_for(std::chrono::seconds(secs)) == AsyncStatus::Started)
        {
            op.Cancel();
            throw hresult_error(HRESULT_FROM_WIN32(ERROR_TIMEOUT), L"no answer for " + to_hstring(secs) + L" s");
        }
        return op.GetResults();
    }

    struct FileHandle // closed however copy_one leaves: an open .part blocked its own retries
    {
        HANDLE h;
        ~FileHandle()
        {
            if (h != INVALID_HANDLE_VALUE)
                CloseHandle(h);
        }
    };

    std::atomic<uint64_t> g_retries;

    // One file into place: skipped if complete, resumed from its .part if one is there. 1 on success.
    bool copy_one(HttpClient const& http, std::wstring const& base, Entry const& e, uint64_t& counted)
    {
        std::wstring dst = local_folder() + L"\\SquareEnix\\" + e.path, part;
        std::replace(dst.begin(), dst.end(), L'/', L'\\');
        if (file_size(dst) == e.size)
        {
            g_bytes_done += e.size, counted += e.size;
            return true;
        }
        part = dst + L".part";
        make_dirs(dst);
        uint64_t have = file_size(part);
        if (have == UINT64_MAX || have > e.size)
            have = 0;
        HttpRequestMessage req(HttpMethod::Get(), Uri(url_for(base, e.path)));
        if (have)
            req.Headers().TryAppendWithoutValidation(L"Range", L"bytes=" + to_hstring(have) + L"-");
        HttpResponseMessage resp = within(http.SendRequestAsync(req, HttpCompletionOption::ResponseHeadersRead), CONNECT_SECS);
        if (!resp.IsSuccessStatusCode())
            throw hresult_error(E_FAIL, L"HTTP " + to_hstring((int32_t)resp.StatusCode()) + L" for " + e.path);
        if (resp.StatusCode() != HttpStatusCode::PartialContent)
            have = 0; // the whole file came: start over
        uint64_t got = have;
        bool ok = true;
        {
            FileHandle f{ CreateFile2(part.c_str(), GENERIC_WRITE, 0, have ? OPEN_EXISTING : CREATE_ALWAYS, nullptr) };
            if (f.h == INVALID_HANDLE_VALUE)
            {
                DWORD err = GetLastError();
                throw hresult_error(HRESULT_FROM_WIN32(err), L"cannot write " + e.path + L" (Windows error " + to_hstring((uint32_t)err) + L")");
            }
            LARGE_INTEGER end{};
            end.QuadPart = (LONGLONG)have;
            SetFilePointerEx(f.h, end, nullptr, FILE_BEGIN);
            g_bytes_done += have, counted += have;
            IInputStream in = within(resp.Content().ReadAsInputStreamAsync(), CONNECT_SECS);
            Buffer buf(1 << 20);
            for (;;)
            {
                IBuffer r = within(in.ReadAsync(buf, buf.Capacity(), InputStreamOptions::Partial), IDLE_SECS);
                if (!r.Length())
                    break;
                DWORD wrote = 0;
                if (!WriteFile(f.h, r.data(), r.Length(), &wrote, nullptr) || wrote != r.Length())
                {
                    DWORD err = GetLastError();
                    throw hresult_error(HRESULT_FROM_WIN32(err), L"writing " + e.path + L" failed (Windows error " + to_hstring((uint32_t)err) + L")");
                }
                got += wrote;
                g_bytes_done += wrote, counted += wrote;
            }
            in.Close();
            resp.Close(); // its connection back to the pool now, not whenever the object goes
        }
        if (!ok || got != e.size)
            throw hresult_error(E_FAIL, L"short copy of " + e.path);
        if (!MoveFileExW(part.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING))
            throw hresult_error(HRESULT_FROM_WIN32(GetLastError()), L"cannot place " + e.path);
        return true;
    }

    // The whole copy; on a worker thread. The error, or "" when every file is in place.
    std::wstring copy_all(std::wstring base)
    {
        HttpBaseProtocolFilter filter;
        filter.CacheControl().ReadBehavior(HttpCacheReadBehavior::NoCache); // no second 14 GB in the cache
        filter.CacheControl().WriteBehavior(HttpCacheWriteBehavior::NoCache);
        filter.MaxConnectionsPerServer(WORKERS); // one each: the default is fewer
        HttpClient http(filter);
        std::vector<Entry> files;
        try
        {
            std::wstring manifest = within(http.GetStringAsync(Uri(base + L"/manifest")), 60).c_str();
            size_t at = 0;
            while (at < manifest.size())
            {
                size_t nl = manifest.find(L'\n', at), tab = manifest.find(L'\t', at);
                if (nl == std::wstring::npos)
                    nl = manifest.size();
                if (tab < nl)
                    files.push_back({ std::stoull(manifest.substr(at, tab - at)), manifest.substr(tab + 1, nl - tab - 1) });
                at = nl + 1;
            }
        }
        catch (...)
        {
            return L"No answer from " + base + L". Is tools/serve_game.py running there, and is the address right?";
        }
        if (files.empty())
            return L"That computer shares no files.";
        uint64_t total = 0;
        for (auto const& e : files)
            total += e.size;
        g_bytes_total = total, g_files_total = files.size();

        std::atomic<size_t> next{ 0 };
        std::vector<std::thread> workers;
        for (int w = 0; w < WORKERS; ++w)
            workers.emplace_back([&] {
                for (size_t i; (i = next++) < files.size();)
                {
                    bool done = false;
                    for (int attempt = 0; attempt < 3 && !done; ++attempt)
                    {
                        uint64_t counted = 0; // this attempt's bytes, taken back if it fails
                        try
                        {
                            done = copy_one(http, base, files[i], counted);
                        }
                        catch (hresult_error const& ex)
                        {
                            ++g_retries;
                            g_bytes_done -= counted;
                            std::lock_guard<std::mutex> hold(g_error_lock);
                            g_error = ex.message().c_str();
                        }
                    }
                    if (done)
                        ++g_files_done;
                    else
                        ++g_failed;
                }
            });
        for (auto& t : workers)
            t.join();
        if (g_failed)
        {
            std::lock_guard<std::mutex> hold(g_error_lock);
            return std::to_wstring(g_failed.load()) + L" file(s) could not be copied (" + g_error +
                   L"). Copy again to finish: what is done is kept.";
        }
        return L"";
    }

    std::wstring gb(uint64_t b)
    {
        wchar_t s[32];
        swprintf(s, 32, L"%.1f", b / 1e9);
        return s;
    }
}

std::wstring CopiedGameFolder() { return local_folder() + L"\\SquareEnix\\FINAL FANTASY XI"; }

std::wstring PackagedGameFolder()
{
    try
    {
        std::wstring root = Windows::ApplicationModel::Package::Current().InstalledLocation().Path().c_str();
        std::wstring game = root + L"\\SquareEnix\\FINAL FANTASY XI";
        if (GetFileAttributesW((game + L"\\FFXiMain.dll").c_str()) != INVALID_FILE_ATTRIBUTES)
            return game;
    }
    catch (hresult_error const&)
    {
    }
    return L"";
}

bool HaveCopiedGame()
{
    return GetFileAttributesW((CopiedGameFolder() + L"\\FFXiMain.dll").c_str()) != INVALID_FILE_ATTRIBUTES &&
           GetFileAttributesW((local_folder() + L"\\SquareEnix\\PlayOnlineViewer").c_str()) != INVALID_FILE_ATTRIBUTES;
}

UIElement GameCopy::Build(std::function<void(std::wstring const&)> on_done)
{
    m_on_done = std::move(on_done);
    StackPanel panel;
    panel.Spacing(8);
    TextBlock title;
    title.Text(L"Copy the game from a computer");
    title.FontSize(16);
    panel.Children().Append(title);
    TextBlock how;
    how.Text(L"On a computer on this network with FINAL FANTASY XI installed, run tools/serve_game.py "
             L"and enter the address it shows. The copy (about 14 GB) goes into this app's storage.");
    how.TextWrapping(TextWrapping::Wrap);
    how.Opacity(0.75);
    panel.Children().Append(how);

    StackPanel row;
    row.Orientation(Orientation::Horizontal);
    row.Spacing(12);
    m_address = TextBox();
    m_address.PlaceholderText(L"192.168.0.78:8765");
    // the address used last, else the usual one
    m_address.Text(unbox_value_or<hstring>(ApplicationData::Current().LocalSettings().Values().TryLookup(L"copy_from"),
        L"192.168.0.78:8765"));
    m_address.Width(260);
    m_address.IsSpellCheckEnabled(false);
    m_copy = Button();
    m_copy.Content(box_value(HaveCopiedGame() ? L"Copy again" : L"Copy"));
    m_copy.Click([this](auto&&, auto&&) { Start(); });
    row.Children().Append(m_address);
    row.Children().Append(m_copy);
    panel.Children().Append(row);

    m_bar = ProgressBar();
    m_bar.Maximum(1000);
    m_bar.Visibility(Visibility::Collapsed);
    panel.Children().Append(m_bar);
    m_status = TextBlock();
    m_status.TextWrapping(TextWrapping::Wrap);
    if (HaveCopiedGame())
        m_status.Text(L"A copy is in place. Copy again to bring it up to date: only missing files are fetched.");
    panel.Children().Append(m_status);

    m_timer = DispatcherTimer();
    m_timer.Interval(std::chrono::milliseconds(500));
    m_timer.Tick([this](auto&&, auto&&) { ShowProgress(); });
    return panel;
}

namespace
{
    // the latest failure, shown as it happens: a copy that keeps failing never reaches its summary
    hstring last_error()
    {
        std::lock_guard<std::mutex> hold(g_error_lock);
        return g_error.empty() ? hstring() : hstring(L"\nLast error: " + g_error);
    }
}

void GameCopy::ShowProgress()
{
    uint64_t done = g_bytes_done, total = g_bytes_total;
    if (!total)
        return;
    m_bar.Value(1000.0 * (double)done / (double)total);
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - g_started).count();
    wchar_t rate[32];
    swprintf(rate, 32, L"%.0f MB/s", secs > 0 ? done / 1e6 / secs : 0.0);
    m_status.Text(L"Copying: " + hstring(gb(done)) + L" of " + hstring(gb(total)) + L" GB, " +
                  to_hstring(g_files_done.load()) + L" of " + to_hstring(g_files_total.load()) + L" files, " + rate +
                  (g_retries ? L", " + to_hstring(g_retries.load()) + L" retried" : L"") +
                  (m_space.empty() ? hstring() : hstring(L"\nStorage at the start: " + m_space + L"; now: " + free_space())) + last_error());
}

fire_and_forget GameCopy::Start()
{
    if (g_running.exchange(true))
        co_return;
    std::wstring addr = m_address.Text().c_str();
    ApplicationData::Current().LocalSettings().Values().Insert(L"copy_from", box_value(m_address.Text()));
    while (!addr.empty() && (addr.back() == L' ' || addr.back() == L'/'))
        addr.pop_back();
    if (addr.empty())
    {
        m_status.Text(L"Enter the address serve_game.py shows, such as 192.168.1.20:8765.");
        g_running = false;
        co_return;
    }
    if (addr.find(L"://") == std::wstring::npos)
        addr = L"http://" + addr;
    if (addr.find(L':', 7) == std::wstring::npos)
        addr += L":8765";
    g_bytes_done = g_bytes_total = g_files_done = g_files_total = g_failed = g_retries = 0;
    g_started = std::chrono::steady_clock::now();
    m_copy.IsEnabled(false);
    m_address.IsEnabled(false);
    m_bar.Visibility(Visibility::Visible);
    m_status.Text(L"Asking " + hstring(addr) + L" for its file list...");
    m_space = free_space();
    m_timer.Start();

    CoreDispatcher ui = Window::Current().Dispatcher();
    co_await resume_background();
    std::wstring error = copy_all(addr);
    co_await resume_foreground(ui);

    m_timer.Stop();
    ShowProgress();
    m_copy.IsEnabled(true);
    m_address.IsEnabled(true);
    g_running = false;
    if (!error.empty())
    {
        m_status.Text(error);
        co_return;
    }
    m_copy.Content(box_value(L"Copy again"));
    m_status.Text(L"Copied: " + hstring(gb(g_bytes_total)) + L" GB in " + to_hstring(g_files_total.load()) +
                  L" files. The game folder is now the copy.");
    if (m_on_done)
        m_on_done(CopiedGameFolder());
}

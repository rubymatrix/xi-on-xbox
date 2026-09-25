#include "pch.h"

#include "Session.h"

extern "C"
{
#include "polsession.h"
}

namespace
{
    constexpr uint16_t LSB_AUTH_PORT = 54231; // xi_connect's sign-in port (host/lsb_login.c)

    std::mutex g_lock; // g_pol, between SignIn/SignOut and the keepalive thread
    PolSession g_pol;
    bool g_open;
    std::thread g_keepalive;
    std::atomic<bool> g_stop;

    // The retail Viewer's session is dropped at 400 s without an answer to the server's PING.
    void keepalive()
    {
        while (!g_stop)
        {
            {
                std::lock_guard<std::mutex> hold(g_lock);
                if (!g_open)
                    return;
                if (!pol_pump(&g_pol, 0))
                {
                    g_open = false;
                    return;
                }
            }
            for (int i = 0; i < 20 && !g_stop; ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    }

    bool net_ready()
    {
        static bool ok = pol_net_init() != 0;
        return ok;
    }
}

void SignOut()
{
    g_stop = true;
    if (g_keepalive.joinable())
        g_keepalive.join();
    std::lock_guard<std::mutex> hold(g_lock);
    if (g_open)
        pol_signout(&g_pol);
    g_open = false;
}

bool SignedIn()
{
    std::lock_guard<std::mutex> hold(g_lock);
    return g_open;
}

SignInResult SignIn(LoginDetails const& d)
{
    SignInResult r;
    if (!net_ready())
    {
        r.message = "The network could not be started.";
        return r;
    }
    if (d.mode == LoginMode::Direct)
    {
        PolSocket s = pol_tcp_connect(d.server.c_str(), LSB_AUTH_PORT, 5000);
        if (s == POL_NO_SOCKET)
        {
            r.message = "No answer from " + d.server + " on port " + std::to_string(LSB_AUTH_PORT) +
                        ". Is the server running, with direct login enabled?";
            return r;
        }
        pol_close(s);
        r.ok = true;
        r.message = d.server + " answers. The game signs in as " + d.id + " when it starts.";
        return r;
    }

    SignOut();
    g_stop = false;
    std::lock_guard<std::mutex> hold(g_lock);
    if (!pol_signin(&g_pol, d.server.c_str(), POL_PORT_IRC, d.id.c_str(), d.password.c_str()))
    {
        r.message = std::string("Sign-in failed: ") + g_pol.error;
        return r;
    }
    uint8_t v[16];
    if (!pol_ffxi_session_value(&g_pol, POL_PORT_POLPRO, v))
    {
        r.message = std::string("Signed in, but the server gave no FINAL FANTASY XI session: ") + g_pol.error;
        pol_signout(&g_pol);
        return r;
    }
    char hex[33];
    for (int i = 0; i < 16; ++i)
        snprintf(hex + 2 * i, 3, "%02x", v[i]);
    g_open = true;
    g_keepalive = std::thread(keepalive);
    r.ok = true;
    r.session_hex = hex;
    r.message = "Signed in to PlayOnline as " + std::string(g_pol.pol_id) + ".";
    return r;
}

std::vector<std::string> HostArguments(LoginDetails const& d, SignInResult const& r)
{
    if (d.mode == LoginMode::PlayOnline)
        return { "--session", r.session_hex, "--lobby", d.server };
    std::vector<std::string> a = { "--server", d.server, "--user", d.id, "--pass", d.password };
    if (!d.otp.empty())
        a.insert(a.end(), { "--otp", d.otp });
    return a;
}

#include "pch.h"

#include "Session.h"

namespace
{
    constexpr uint16_t LSB_AUTH_PORT = 54231; // xi_connect's sign-in port (host/lsb_login.c)

    bool net_ready()
    {
        static bool ok = [] {
            WSADATA w;
            return WSAStartup(MAKEWORD(2, 2), &w) == 0;
        }();
        return ok;
    }

    // Whether host:port takes a TCP connection within timeout_ms.
    bool answers(std::string const& host, uint16_t port, int timeout_ms)
    {
        addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res)
            return false;
        SOCKET s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (s == INVALID_SOCKET)
        {
            freeaddrinfo(res);
            return false;
        }
        u_long nonblocking = 1;
        ioctlsocket(s, FIONBIO, &nonblocking);
        bool ok = connect(s, res->ai_addr, (int)res->ai_addrlen) == 0;
        freeaddrinfo(res);
        if (!ok && WSAGetLastError() == WSAEWOULDBLOCK)
        {
            fd_set w, e;
            FD_ZERO(&w);
            FD_ZERO(&e);
            FD_SET(s, &w);
            FD_SET(s, &e);
            timeval tv{ timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
            int err = 0, len = sizeof err;
            ok = select(0, nullptr, &w, &e, &tv) == 1 && FD_ISSET(s, &w) &&
                 getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&err, &len) == 0 && !err;
        }
        closesocket(s);
        return ok;
    }
}

SignInResult SignIn(LoginDetails const& d)
{
    SignInResult r;
    if (!net_ready())
    {
        r.message = "The network could not be started.";
        return r;
    }
    if (!answers(d.server, LSB_AUTH_PORT, 5000))
    {
        r.message = "No answer from " + d.server + " on port " + std::to_string(LSB_AUTH_PORT) +
                    ". Is the server running, with direct login enabled?";
        return r;
    }
    r.ok = true;
    r.message = d.server + " answers. The game signs in as " + d.id + " when it starts.";
    return r;
}

std::vector<std::string> HostArguments(LoginDetails const& d)
{
    std::vector<std::string> a = { "--server", d.server, "--user", d.id, "--pass", d.password };
    if (!d.otp.empty())
        a.insert(a.end(), { "--otp", d.otp });
    return a;
}

// Signing in before the game starts, in the two ways a server can want it:
//
//   PlayOnline: the retail sign-in (FFXI-Signet's client/pol: sqIrc on 51240, then polpro on 51220)
//     gives the FFXI session value V, which the host takes as --session. The PlayOnline session
//     stays open, answering the server's keepalive, for as long as the game runs.
//   Direct: a LandSandBoat server with no PlayOnline behind it. The host signs in itself, as
//     xiloader does (FFXIRecompile's host/lsb_login.c); here we only check the server answers.
#pragma once

#include <string>
#include <vector>

enum class LoginMode
{
    PlayOnline,
    Direct,
};

struct LoginDetails
{
    LoginMode mode = LoginMode::PlayOnline;
    std::string server;   // the PlayOnline server (also the lobby), or the LandSandBoat server
    std::string id;       // PlayOnline ID ("NAAB3165"), or the LandSandBoat account
    std::string password;
    std::string otp;      // Direct only: the two-factor code, if the account has one
};

struct SignInResult
{
    bool ok = false;
    std::string message;
    std::string session_hex; // PlayOnline: V as 32 hex digits
};

// Blocking: call off the UI thread. A PlayOnline sign-in replaces any earlier one.
SignInResult SignIn(LoginDetails const& d);
// Closes the PlayOnline session, if one is open.
void SignOut();
// Whether the PlayOnline session is still open (the server can drop it).
bool SignedIn();

// The host's sign-in arguments for this login (FFXIRecompile's host/host64.c).
std::vector<std::string> HostArguments(LoginDetails const& d, SignInResult const& r);

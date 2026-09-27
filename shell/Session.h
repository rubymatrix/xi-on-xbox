// Signing in before the game starts: a LandSandBoat server. The host signs in itself, as
// xiloader does (FFXIRecompile's host/lsb_login.c); here we only check the server answers.
#pragma once

#include <string>
#include <vector>

struct LoginDetails
{
    std::string server;   // the LandSandBoat server
    std::string id;       // the LandSandBoat account
    std::string password;
    std::string otp;      // the two-factor code, if the account has one
};

struct SignInResult
{
    bool ok = false;
    std::string message;
};

// Blocking: call off the UI thread.
SignInResult SignIn(LoginDetails const& d);

// The host's sign-in arguments for this login (FFXIRecompile's host/host64.c).
std::vector<std::string> HostArguments(LoginDetails const& d);

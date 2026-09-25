// What the login screen remembers between runs: the mode, server and ID in the app's local
// settings, and the password - only when asked to - in the Windows credential locker.
#pragma once

#include "Session.h"

struct SavedLogin
{
    LoginDetails details; // the password is empty unless it was remembered
    bool remember_password = false;
    std::string game_dir; // the FINAL FANTASY XI folder
};

SavedLogin LoadLogin();
void SaveLogin(LoginDetails const& d, bool remember_password, std::string const& game_dir);
// The frame rate to run the game at: 30 (as it shipped, the default) or 60.
int LoadFps();
void SaveFps(int fps);
// Whether to profile the game (off by default: measuring costs the game thread time).
bool LoadProfile();
void SaveProfile(bool on);
// The game's resolution: "" for the screen's (the default), else "WIDTHxHEIGHT".
std::string LoadResolution();
void SaveResolution(std::string const& text);
// The ID last used with a mode (the PlayOnline ID and the direct account are kept apart).
std::string SavedId(LoginMode m);

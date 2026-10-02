// The resolution the game renders at: the screen's by default, or one the player picks.
#pragma once

#include <string>
#include <utility>
#include <vector>

using Resolution = std::pair<int, int>; // width, height in pixels

// The screen's resolution: on an Xbox, the TV's output mode (HdmiDisplayInformation: 1080p, 1440p,
// 4K...); elsewhere, the app's view in pixels. Call on the UI thread.
Resolution ScreenResolution();

// What the login screen offers besides "match the screen".
std::vector<Resolution> ResolutionChoices();

// "1920x1080" <-> {1920, 1080}; {0, 0} for anything else (and for "", which means the screen's).
std::string ToText(Resolution r);
Resolution ParseResolution(std::string const& text);

// The size of the square target the game draws the world into before it fits the window ("0003"/"0004",
// the background resolution): the window's width rounded up to a power of two, from 1024 to 2048. The
// game's own default of 4096 is 8 times the pixels of a 1080p frame, more than an Xbox One's GPU has.
int BackgroundResolution(Resolution r);

// Sets the game's resolution in a settings.reg (REGEDIT4): the window ("0001"/"0002"), the
// interface ("0037"/"0038": half size from 1080 lines up, so text stays legible; full size below)
// and the background (BackgroundResolution).
// Everything else in the file - the game writes its own changes there - is left as it is.
bool SetGameResolution(std::wstring const& reg_path, Resolution r);

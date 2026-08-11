// The backends the cards talk to, in one bag.
//
// Same shape (and same reason) as DockManager::Shared: the window hands every
// one of these to QML as a context property, and passing them as a struct keeps
// the constructor from growing a parameter per section. A null member means the
// section is unavailable and its card says so instead of crashing — which is
// also what makes a probe with an empty struct possible.

#pragma once

class AppearanceControl;
class AudioControl;
class BatteryControl;
class BrightnessControl;
class DesktopEntryIndex;
class DockLink;
class MprisControl;
class NetworkControl;
class PowerControl;
class VirtualDesktops;
class ScreenBrightness;
class WallpaperControl;

struct CmBackends
{
    DesktopEntryIndex *apps = nullptr;
    AudioControl *audio = nullptr;
    BatteryControl *battery = nullptr;
    BrightnessControl *brightness = nullptr;
    ScreenBrightness *screenBrightness = nullptr;
    NetworkControl *network = nullptr;
    WallpaperControl *wallpaper = nullptr;
    PowerControl *power = nullptr;
    MprisControl *mpris = nullptr;
    DockLink *dock = nullptr;
    AppearanceControl *appearance = nullptr;
    VirtualDesktops *desktops = nullptr;
};

#pragma once

#include <string>

// One plain-text settings file, written next to the .asi on first run with every option listed,
// explained and set to its default.
struct Settings
{
    bool off  = false;
    int  port = 13172;   // FiveM's CEF remote debugging port

    // How long the panel stays on screen after what it shows last changed, in milliseconds. 0
    // keeps it up for as long as you are in an emergency vehicle.
    int holdMs = 0;

    // Where the OFF / 1 / 2 / 3 slide knob sits for lights without the siren. The game knows
    // three states, off, lights and siren; off is OFF and siren is 3.
    int lightsStage = 2;

    // Which key lights up for each siren tone, tone 1 first: sub, sbs, wail, yelp, wig, bumr or
    // none. A tone past the end of the list lights nothing.
    std::string toneKeys = "wail,yelp,sbs,wig,bumr,sub";

    // Which vehicle state lights which of the panel's other keys, as key:source pairs. Keys: any
    // of the panel's but stby. Sources: cruise, handbrake, headlights, highbeams, leftsignal,
    // rightsignal, hazards, locked, engine, none.
    std::string keys = "lrco:handbrake,warn:hazards,dim:headlights,flood:none,radio:none,"
                       "takedown:highbeams,leftalley:leftsignal,rightalley:rightsignal,"
                       "crimeject:none,gunlock:locked";

    // Whether the key legends are backlit only while the engine runs (engine), or always.
    std::string backlight = "engine";

    // How opaque the panel is, 0.05 to 1.
    double alpha = 1.0;

    int reconnectMs = 2000;
    int heartbeatMs = 4000;
};

extern Settings g_set;

// Reads the file if present. Never writes and never blocks; safe to call under the loader lock.
void loadSettings();

// Creates the documented default file when it does not exist. Runs on the worker thread.
void writeDefaultSettingsIfMissing();

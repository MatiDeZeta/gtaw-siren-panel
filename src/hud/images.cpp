#include "hud/images.h"

// Keep in step with the KB_ lines in gtaw-siren-panel.rc and with PANEL_LAYERS in the script.
namespace hud {

const char* const kPanelLayers[] = {
    "base", "backlight",
    "lrco", "warn", "dim", "flood",
    "stby", "radio", "sub", "sbs", "wail", "yelp", "wig", "bumr",
    "leftalley", "takedown", "rightalley", "crimeject", "gunlock",
    "led1", "led2", "led3", "knob0", "knob1", "knob2", "knob3",
    nullptr,
};

}  // namespace hud

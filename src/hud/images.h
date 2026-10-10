#pragma once

// The Siren Master panel's layers, bottom first, as the injected script names them. Each is the
// PNG assets/kabel/<name>.png, compiled in by gtaw-siren-panel.rc as the resource KB_<NAME>.
// Null terminated.
namespace hud {

extern const char* const kPanelLayers[];

}  // namespace hud

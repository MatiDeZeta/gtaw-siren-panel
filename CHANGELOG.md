# Changelog

## v1.0.0

The Kabel Siren Master panel from the LEO edition of [gtaw-oldhud](https://github.com/MatiDeZeta/gtaw-oldhud),
on its own, for players who want to keep GTA World's current HUD.

- **Only the panel.** In an emergency vehicle a Kabel Manufacturing Siren Master control head is
  drawn on top of the speedometer. Nothing of the game's HUD is hidden, moved or restyled: the
  plugin adds one element to the page and one stylesheet that only styles that element.
- **It follows the speedometer.** The panel's right edge lines up with the speedometer's and it
  sits just above it, wherever the game's own layout put it. Before the speedometer is on the page
  it goes where the speedometer sits at the default layout.
- **Always up, or a five second pop-up.** By default the panel stays on screen the whole time you
  are in the vehicle. `hold_ms = 5000` shows it whenever anything on it changes — the lights, the
  siren or its tone, or any key — and fades it out five seconds later.
- **GUN LOCK follows the door locks**, as the speedometer's lock icon reports them. `LRCO` follows
  the handbrake, `WARN` the hazards, `DIM` the headlights, `TAKE DOWN` the high beams and the
  alleys the turn signals; `keys` remaps any of them.
- **Placed with `/hudlayout`.** While the game's layout editor is open the panel can be dragged,
  scaled with the wheel and hidden with its cross. That is kept in
  `gtaw-siren-panel.layout.txt`, measured from its place on the speedometer, so it keeps
  following it.
- **Cleaner keys.** Each key's light is now taken only from the pixels that belong to the key,
  with a soft edge, instead of from a square cut round it. The faint square that showed round
  every key, brightest round a lit one, and the seams between neighbouring keys are gone.
- **Its own files.** `gtaw-siren-panel.asi` writes `gtaw-siren-panel.settings.txt`,
  `gtaw-siren-panel.layout.txt` and `gtaw-siren-panel.log`, and runs in its own private context.
  It logs a warning when `gtaw-oldhud-leo.asi` is in the same folder, since that one draws the
  same panel.

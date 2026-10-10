# Siren Master panel images

The PNG files in this folder are the layers of the Kabel Manufacturing Siren Master control head,
cut out of [`art/siren-master.jpg`](../../art/siren-master.jpg) by
[`tools/make_panel.py`](../../tools/make_panel.py). The panel and the photograph are this
project's own work and are covered by the MIT licence in the repository root.

Regenerate them with `python3 tools/make_panel.py`, which needs Pillow and numpy. Nothing else in
the repository does: the build and the tests use the PNGs as they are, and
`tools/check_assets.py` checks each one before it is compiled in.

| File | Layer |
|---|---|
| `base.png` | the panel unpowered: every key dark, LEDs off, the slot empty |
| `backlight.png` | the legends backlit blue and the rings softly lit, while the engine runs |
| `lrco.png`, `warn.png`, `dim.png`, `flood.png` | the top row's keys lit |
| `stby.png`, `radio.png`, `sub.png`, `sbs.png`, `wail.png`, `yelp.png`, `wig.png`, `bumr.png` | the middle row's keys lit |
| `leftalley.png`, `takedown.png`, `rightalley.png`, `crimeject.png`, `gunlock.png` | the bottom row's keys lit |
| `led1.png`, `led2.png`, `led3.png` | the stage LEDs lit |
| `knob0.png` … `knob3.png` | the slide knob at OFF, 1, 2 and 3 |

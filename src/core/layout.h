#pragma once

#include <string>

// Where the panel sits, as placed with the game's /hudlayout editor: one line, as
// "panel = <x> <y> <scale> <hidden>", with x and y as fractions of the screen, measured from
// its place on top of the speedometer. The injected
// script edits it inside the editor's session and hands the text back through the heartbeat,
// and it is kept next to the .asi so it survives the session. The game's own layout is per
// account and lives on the server; this one is per machine, since nothing is ever sent.
namespace layout {

// True for text in exactly that shape and nothing else. Every line the script hands back and
// every line read off the disk goes through this, so the file can never carry anything but
// one short numeric line.
bool isValid(const std::string& text);

// The file's text, or empty when there is no file or it does not validate.
std::string load();

// Writes the text if it validates. Empty text removes the layout (an empty file is written,
// which is what a Reset in the editor produces).
bool save(const std::string& text);

}  // namespace layout

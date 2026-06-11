#pragma once

// Inline ImGui directory navigator for the New/Open Project dialogs: an
// editable absolute path plus a clickable list of subdirectories. No native
// file-dialog dependency.

#include <string>

struct DirBrowser {
    std::string path; // current absolute directory

    // Resets to `start` (or $HOME / current dir when empty or invalid).
    void reset(const std::string& start = "");

    // Draws the path field + subdirectory list. Call inside an ImGui window.
    void draw(const char* id, float listHeight = 200.0f);
};

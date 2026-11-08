#pragma once

#include <string>

// Saved reading percentage, or -1 when unavailable. Never builds reading caches.
// `chapter`, when given, receives the TOC title of the saved position (EPUB only).
int loadBookProgress(const std::string& path, std::string* chapter = nullptr);

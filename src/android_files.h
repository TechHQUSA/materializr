#pragma once
#include <string>

// Android Storage Access Framework + share bridge (JNI into MaterializrActivity).
// All Android-only: on desktop these are inline no-ops, so FileDialogs.cpp can
// reference them and the desktop file picker (portable-file-dialogs) is untouched.
namespace materializr {

#if defined(__ANDROID__)

// Launch the system open / "save as" pickers. mimeCsv is comma-separated MIME
// types (or "*/*"). The result is delivered asynchronously — poll with
// androidPollFileResult() each frame. Returns false if the activity is missing.
bool androidStartOpenDocument(const std::string& mimeCsv);
bool androidStartCreateDocument(const std::string& suggestedName, const std::string& mime);

// Poll the pending picker. Returns true once a result is in: `outValue` is then
// the temp path to read (open), or "ok" (save: write your temp, then commit), or
// "" if the user cancelled. Returns false while still pending.
bool androidPollFileResult(std::string& outValue);

// After an open picker the chosen document has already been copied to a temp
// path. After a save picker, write your data to a temp file and call this to copy
// it into the user's chosen destination. Returns true on success.
bool androidCommitSave(const std::string& tempPath);

// Hand a just-written file to the system share sheet (copies it through the
// FileProvider cache; no storage permission needed).
void androidShareFile(const std::string& path, const std::string& mime);

#else  // desktop: no-ops (the calls live behind #if __ANDROID__ anyway)

inline bool androidStartOpenDocument(const std::string&) { return false; }
inline bool androidStartCreateDocument(const std::string&, const std::string&) { return false; }
inline bool androidPollFileResult(std::string&) { return false; }
inline bool androidCommitSave(const std::string&) { return false; }
inline void androidShareFile(const std::string&, const std::string&) {}

#endif

} // namespace materializr

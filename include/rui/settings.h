// The settings file: an ini of [sections] and `key = value` lines, each key
// bound to one of the game's variables. The game registers its keys (and
// rui::RegisterSettings the overlay's own) before Load; the file is written
// in the order they were registered, a section at its first key's place.
//
// A key's default is its variable's value when it's registered. Load reads
// what the file has over that: unknown keys and sections are ignored, a
// value a key doesn't accept keeps the default, so the file survives keys
// being added or removed.
//
// A change made in the panel (any registered key's value differs after the
// frame) runs the OnChanged hooks and saves half a second after the last
// change; a change made elsewhere calls Changed for the same. SavePending at
// exit writes one still waiting. The file is written next to itself and
// renamed over it: a crash mid-write keeps the old one.
//
// Threads: register and Load before the overlay starts (the main thread,
// say). After that the variables belong to the drawing thread, inside
// Mutex() (rui::NewFrame holds it while the pages and hooks run);
// SavePending takes it, from any thread.
#pragma once

#include <filesystem>
#include <functional>
#include <initializer_list>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Xlib.h (plume's swap chain on Linux, included before this) defines Bool as
// a macro, which would turn settings::Bool into nonsense. Plume undefines the
// rest of Xlib's clashing names the same way.
#ifdef Bool
#undef Bool
#endif

namespace rui::settings
{
    // "true"/"1" or "false"/"0".
    bool ParseBool(std::string_view text, bool& value);
    // A whole number (from the start of `text`).
    bool ParseInt(std::string_view text, int& value);
    // A decimal number, such as 1.25 (its start, as strtod reads it).
    bool ParseNumber(std::string_view text, double& value);

    // `read` takes the file's text (trimmed) and returns whether it took it;
    // `write` gives the text to write.
    using Reader = std::function<bool(std::string_view text)>;
    using Writer = std::function<std::string()>;

    void Custom(std::string_view section, std::string_view key, Reader read, Writer write);
    // true / false.
    void Bool(std::string_view section, std::string_view key, bool& value);
    // A number from `low` to `high` (clamped).
    void Int(std::string_view section, std::string_view key, int& value, int low, int high);
    // Text to the end of the line, at most `maxLength` bytes kept.
    void String(std::string_view section, std::string_view key, std::string& value, size_t maxLength = std::string::npos);
    // One of `names` (written as the first name of the value); `aliases`
    // are read as well (names from older versions).
    template <typename T>
    void Choice(std::string_view section, std::string_view key, T& value, std::initializer_list<std::pair<T, const char*>> names,
        std::initializer_list<std::pair<T, const char*>> aliases = {})
    {
        std::vector<std::pair<T, std::string>> read, written;
        for (const auto& [v, name] : names)
        {
            read.emplace_back(v, name);
            written.emplace_back(v, name);
        }
        for (const auto& [v, name] : aliases)
            read.emplace_back(v, name);
        T* target = &value;
        Custom(
            section, key,
            [target, read](std::string_view text) {
                for (const auto& [v, name] : read)
                {
                    if (text == name)
                    {
                        *target = v;
                        return true;
                    }
                }
                return false;
            },
            [target, written]() {
                for (const auto& [v, name] : written)
                {
                    if (*target == v)
                        return name;
                }
                return written.empty() ? std::string() : written.front().second;
            });
    }
    // A key read from older files but no longer written (`read` moves its
    // meaning to the keys that replaced it, in an AfterLoad hook say).
    void Obsolete(std::string_view section, std::string_view key, Reader read);
    // `oldKey` read as `key` (a key that was renamed; the new name is
    // written).
    void Alias(std::string_view section, std::string_view oldKey, std::string_view key);
    // After each Load (and its keys' readers).
    void AfterLoad(std::function<void()> hook);

    // Reads the file (missing: everything keeps its default) and remembers
    // it as the file to save to.
    void Load(const std::filesystem::path& path);
    const std::filesystem::path& Path();
    // Whether the last Load's file had the key with a value it took.
    bool WasRead(std::string_view section, std::string_view key);
    // The text a key was registered with (its default); empty when unknown.
    std::string DefaultText(std::string_view section, std::string_view key);

    // The variables in the file's form: what the panel's changes are
    // found by.
    std::string Snapshot();
    // Writes the file now; false when it couldn't be written.
    bool Save();
    // A change to save, half a second after the last one.
    void Changed();
    // The hooks a change made in the panel runs (the game applies its
    // settings), on the drawing thread before it's saved.
    void OnChanged(std::function<void()> hook);
    // Saves when a change is due (rui::NewFrame calls it each frame).
    void Update();
    // Writes a change not saved yet (at exit). Takes Mutex().
    void SavePending();

    enum class Status
    {
        Saved,
        Pending,  // changed, written half a second after the last change
        Failed,   // the last write failed
    };
    Status CurrentStatus();

    std::mutex& Mutex();
}

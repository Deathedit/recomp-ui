#include "internal.h"

#include <rui/settings.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>

namespace rui::settings
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        struct Key
        {
            std::string section, key;
            Reader read;
            Writer write;  // null: obsolete, not written
            std::string defaultText;
            bool wasRead = false;
        };

        // Registration and the file, in registration order.
        std::vector<Key>& Keys()
        {
            static std::vector<Key> keys;
            return keys;
        }
        std::vector<std::pair<std::string, std::string>> s_aliases;  // "section\nold" -> "section\nnew"
        std::vector<std::function<void()>> s_afterLoad, s_onChanged;
        std::filesystem::path s_path;

        std::mutex s_mutex;
        std::optional<Clock::time_point> s_saveAt;
        bool s_failed = false;

        std::string_view Trim(std::string_view text)
        {
            const size_t first = text.find_first_not_of(" \t\r");
            if (first == std::string_view::npos)
                return {};
            const size_t last = text.find_last_not_of(" \t\r");
            return text.substr(first, last - first + 1);
        }

        Key* Find(std::string_view section, std::string_view key)
        {
            for (Key& entry : Keys())
            {
                if (entry.section == section && entry.key == key)
                    return &entry;
            }
            return nullptr;
        }

        void Add(std::string_view section, std::string_view key, Reader read, Writer write)
        {
            Key entry{ std::string(section), std::string(key), std::move(read), std::move(write) };
            if (entry.write)
                entry.defaultText = entry.write();
            Keys().push_back(std::move(entry));
        }

        void Apply(std::string_view section, std::string_view key, std::string_view value)
        {
            std::string name = std::string(section) + "\n" + std::string(key);
            for (const auto& [from, to] : s_aliases)
            {
                if (from == name)
                    name = to;
            }
            const size_t split = name.find('\n');
            if (Key* entry = Find(std::string_view(name).substr(0, split), std::string_view(name).substr(split + 1)))
            {
                if (entry->read(value))
                    entry->wasRead = true;
            }
        }

        bool Write()
        {
            std::ostringstream text;
            std::vector<std::string> sections;
            for (const Key& entry : Keys())
            {
                if (entry.write && std::find(sections.begin(), sections.end(), entry.section) == sections.end())
                    sections.push_back(entry.section);
            }
            for (size_t i = 0; i < sections.size(); i++)
            {
                text << (i == 0 ? "" : "\n") << "[" << sections[i] << "]\n";
                for (const Key& entry : Keys())
                {
                    if (entry.write && entry.section == sections[i])
                        text << entry.key << " = " << entry.write() << "\n";
                }
            }

            std::error_code error;
            std::filesystem::create_directories(s_path.parent_path(), error);
            std::filesystem::path temporary = s_path;
            temporary += ".tmp";
            {
                std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
                if (!file)
                    return false;
                file << text.str();
                if (!file)
                    return false;
            }
            std::filesystem::rename(temporary, s_path, error);
            return !error;
        }

        // Inside s_mutex.
        void SaveLocked()
        {
            s_failed = !Write();
            if (s_failed)
                ::rui::detail::Log(LogKind::Info, std::format("couldn't write {}", s_path.string()));
            s_saveAt.reset();
        }
    }

    bool ParseBool(std::string_view text, bool& value)
    {
        if (text == "true" || text == "1")
            value = true;
        else if (text == "false" || text == "0")
            value = false;
        else
            return false;
        return true;
    }

    bool ParseInt(std::string_view text, int& value)
    {
        int parsed = 0;
        const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
        if (result.ec != std::errc())
            return false;
        value = parsed;
        return true;
    }

    bool ParseNumber(std::string_view text, double& value)
    {
        const std::string copy(text);
        char* end = nullptr;
        const double parsed = std::strtod(copy.c_str(), &end);
        if (end == copy.c_str())
            return false;
        value = parsed;
        return true;
    }

    void Custom(std::string_view section, std::string_view key, Reader read, Writer write)
    {
        Add(section, key, std::move(read), std::move(write));
    }

    void Bool(std::string_view section, std::string_view key, bool& value)
    {
        bool* target = &value;
        Add(section, key, [target](std::string_view text) { return ParseBool(text, *target); },
            [target] { return std::string(*target ? "true" : "false"); });
    }

    void Int(std::string_view section, std::string_view key, int& value, int low, int high)
    {
        int* target = &value;
        Add(
            section, key,
            [target, low, high](std::string_view text) {
                int parsed = 0;
                if (!ParseInt(text, parsed))
                    return false;
                *target = std::clamp(parsed, low, high);
                return true;
            },
            [target] { return std::to_string(*target); });
    }

    void String(std::string_view section, std::string_view key, std::string& value, size_t maxLength)
    {
        std::string* target = &value;
        Add(
            section, key,
            [target, maxLength](std::string_view text) {
                *target = std::string(text.substr(0, maxLength));
                return true;
            },
            [target] { return *target; });
    }

    void Obsolete(std::string_view section, std::string_view key, Reader read)
    {
        Add(section, key, std::move(read), nullptr);
    }

    void Alias(std::string_view section, std::string_view oldKey, std::string_view key)
    {
        s_aliases.emplace_back(std::string(section) + "\n" + std::string(oldKey), std::string(section) + "\n" + std::string(key));
    }

    void AfterLoad(std::function<void()> hook)
    {
        s_afterLoad.push_back(std::move(hook));
    }

    void Load(const std::filesystem::path& path)
    {
        s_path = path;
        for (Key& entry : Keys())
            entry.wasRead = false;
        std::ifstream file(path);
        std::string line, section;
        while (std::getline(file, line))
        {
            const std::string_view text = Trim(line);
            if (text.empty() || text[0] == '#' || text[0] == ';')
                continue;
            if (text.front() == '[' && text.back() == ']')
            {
                section = Trim(text.substr(1, text.size() - 2));
                continue;
            }
            const size_t equals = text.find('=');
            if (equals != std::string_view::npos)
                Apply(section, Trim(text.substr(0, equals)), Trim(text.substr(equals + 1)));
        }
        for (const auto& hook : s_afterLoad)
            hook();
    }

    const std::filesystem::path& Path()
    {
        return s_path;
    }

    bool WasRead(std::string_view section, std::string_view key)
    {
        const Key* entry = Find(section, key);
        return entry != nullptr && entry->wasRead;
    }

    std::string DefaultText(std::string_view section, std::string_view key)
    {
        const Key* entry = Find(section, key);
        return entry != nullptr ? entry->defaultText : std::string();
    }

    std::string Snapshot()
    {
        std::string text;
        for (const Key& entry : Keys())
        {
            if (entry.write)
            {
                text += entry.write();
                text += '\n';
            }
        }
        return text;
    }

    bool Save()
    {
        std::lock_guard lock(s_mutex);
        SaveLocked();
        return !s_failed;
    }

    void Changed()
    {
        s_saveAt = Clock::now() + std::chrono::milliseconds(500);
    }

    void OnChanged(std::function<void()> hook)
    {
        s_onChanged.push_back(std::move(hook));
    }

    void Update()
    {
        if (s_saveAt && Clock::now() >= *s_saveAt)
            SaveLocked();
    }

    void SavePending()
    {
        std::lock_guard lock(s_mutex);
        if (s_saveAt)
            SaveLocked();
    }

    Status CurrentStatus()
    {
        return s_failed ? Status::Failed : s_saveAt ? Status::Pending : Status::Saved;
    }

    std::mutex& Mutex()
    {
        return s_mutex;
    }
}

namespace rui::detail
{
    void RunSettingsChanged()
    {
        for (const auto& hook : settings::s_onChanged)
            hook();
        settings::Changed();
    }
}

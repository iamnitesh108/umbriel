#include "overview/app_icon_lookup.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

namespace umbriel {

  namespace {

    using Group = std::unordered_map<std::string, std::string>;

#ifdef UMBRIEL_SVG_ICONS
    constexpr std::string_view kExtensions[] = {".png", ".svg"};
#else
    constexpr std::string_view kExtensions[] = {".png"};
#endif

    std::string lowercase(std::string_view text) {
      std::string lower(text);
      std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      return lower;
    }

    std::string_view trim(std::string_view text) {
      const size_t first = text.find_first_not_of(" \t\r");
      if (first == std::string_view::npos) {
        return {};
      }
      return text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
    }

    // Groups of a desktop-entry style file (desktop entries and index.theme share the format).
    std::unordered_map<std::string, Group> readKeyFile(const std::filesystem::path& path) {
      std::unordered_map<std::string, Group> groups;
      std::ifstream file(path);
      Group* group = nullptr;
      for (std::string line; std::getline(file, line);) {
        const std::string_view entry = trim(line);
        if (entry.empty() || entry.front() == '#') {
          continue;
        }
        if (entry.front() == '[' && entry.back() == ']') {
          group = &groups[std::string(entry.substr(1, entry.size() - 2))];
          continue;
        }
        const size_t equals = entry.find('=');
        if (group != nullptr && equals != std::string_view::npos) {
          group->try_emplace(std::string(trim(entry.substr(0, equals))), trim(entry.substr(equals + 1)));
        }
      }
      return groups;
    }

    std::string_view value(const Group& group, const std::string& key) {
      const auto it = group.find(key);
      return it == group.end() ? std::string_view{} : std::string_view(it->second);
    }

    int number(const Group& group, const std::string& key, int fallback) {
      const std::string_view text = value(group, key);
      int result = fallback;
      if (std::from_chars(text.data(), text.data() + text.size(), result).ec != std::errc{}) {
        return fallback;
      }
      return result;
    }

    std::vector<std::string> split(std::string_view list) {
      std::vector<std::string> items;
      while (!list.empty()) {
        const size_t comma = list.find(',');
        if (const std::string_view item = trim(list.substr(0, comma)); !item.empty()) {
          items.emplace_back(item);
        }
        list = comma == std::string_view::npos ? std::string_view{} : list.substr(comma + 1);
      }
      return items;
    }

    // A name that stays inside the directory it is joined to.
    bool plainName(std::string_view name) {
      return !name.empty() && name != "." && name != ".." && !name.contains('/');
    }

    bool isFile(const std::filesystem::path& path) {
      std::error_code error;
      return std::filesystem::is_regular_file(path, error);
    }

    bool isDirectory(const std::filesystem::path& path) {
      std::error_code error;
      return std::filesystem::is_directory(path, error);
    }

    bool decodable(const std::filesystem::path& path) {
      return std::ranges::find(kExtensions, path.extension().string()) != std::end(kExtensions);
    }

    // The first decodable icon named `name` in `subdir` of any of `roots`.
    std::filesystem::path fileIn(
        const std::vector<std::filesystem::path>& roots, const std::filesystem::path& subdir, std::string_view name
    ) {
      for (const std::filesystem::path& root : roots) {
        for (const std::string_view extension : kExtensions) {
          if (std::filesystem::path path = root / subdir / (std::string(name) + std::string(extension)); isFile(path)) {
            return path;
          }
        }
      }
      return {};
    }

    // An Icon value names an icon without its extension, but some entries spell one out.
    std::string iconName(std::string_view icon) {
      for (const std::string_view extension : {".png", ".svg", ".xpm"}) {
        if (!icon.starts_with('/') && icon.size() > extension.size() && icon.ends_with(extension)) {
          return std::string(icon.substr(0, icon.size() - extension.size()));
        }
      }
      return std::string(icon);
    }

    // The words of an Exec line, with double quotes removed.
    std::vector<std::string> execWords(std::string_view exec) {
      std::vector<std::string> words;
      std::string word;
      bool quoted = false;
      bool started = false;
      for (const char c : exec) {
        if (c == '"') {
          quoted = !quoted;
          started = true;
        } else if (!quoted && (c == ' ' || c == '\t')) {
          if (started) {
            words.push_back(std::move(word));
            word.clear();
            started = false;
          }
        } else {
          word.push_back(c);
          started = true;
        }
      }
      if (started) {
        words.push_back(std::move(word));
      }
      return words;
    }

    // The program an Exec line runs, lowercased, past `env` and its assignments, or the app `flatpak run` starts.
    std::string execProgram(std::string_view exec) {
      const std::vector<std::string> words = execWords(exec);
      size_t index = 0;
      if (index < words.size() && std::filesystem::path(words[index]).filename() == "env") {
        ++index;
        while (index < words.size() && (words[index].contains('=') || words[index].starts_with('-'))) {
          ++index;
        }
      }
      if (index >= words.size()) {
        return {};
      }
      std::string program = std::filesystem::path(words[index]).filename().string();
      if (program == "flatpak" && index + 1 < words.size() && words[index + 1] == "run") {
        const auto app = std::find_if(
            words.begin() + static_cast<std::ptrdiff_t>(index) + 2, words.end(),
            [](const std::string& word) { return !word.starts_with('-'); }
        );
        return app == words.end() ? std::string{} : lowercase(*app);
      }
      return lowercase(program);
    }

    // `<width>x<height>` or `<width>x<height>@<scale>`, the names of a theme's size directories.
    std::optional<std::pair<int, int>> sizeDirectory(std::string_view name) {
      int size = 0;
      int height = 0;
      int scale = 1;
      const char* end = name.data() + name.size();
      auto parsed = std::from_chars(name.data(), end, size);
      if (parsed.ec != std::errc{} || parsed.ptr == end || *parsed.ptr != 'x') {
        return std::nullopt;
      }
      parsed = std::from_chars(parsed.ptr + 1, end, height);
      if (parsed.ec != std::errc{} || height != size) {
        return std::nullopt;
      }
      if (parsed.ptr != end) {
        if (*parsed.ptr != '@') {
          return std::nullopt;
        }
        parsed = std::from_chars(parsed.ptr + 1, end, scale);
        if (parsed.ec != std::errc{} || parsed.ptr != end) {
          return std::nullopt;
        }
      }
      if (size <= 0 || scale <= 0) {
        return std::nullopt;
      }
      return std::pair{size, scale};
    }

    std::filesystem::path envPath(const char* name) {
      const char* text = std::getenv(name);
      return text != nullptr && text[0] == '/' ? std::filesystem::path(text) : std::filesystem::path{};
    }

  } // namespace

  AppIconSearchPaths appIconSearchPaths() {
    const std::filesystem::path home = envPath("HOME");
    std::filesystem::path dataHome = envPath("XDG_DATA_HOME");
    if (dataHome.empty() && !home.empty()) {
      dataHome = home / ".local/share";
    }
    std::vector<std::filesystem::path> dataDirs;
    if (!dataHome.empty()) {
      dataDirs.push_back(dataHome);
    }
    const char* dirs = std::getenv("XDG_DATA_DIRS");
    std::string_view list = dirs != nullptr && dirs[0] != '\0' ? dirs : "/usr/local/share:/usr/share";
    while (!list.empty()) {
      const size_t colon = list.find(':');
      if (const std::string_view dir = list.substr(0, colon); dir.starts_with('/')) {
        dataDirs.emplace_back(dir);
      }
      list = colon == std::string_view::npos ? std::string_view{} : list.substr(colon + 1);
    }

    AppIconSearchPaths paths;
    if (!home.empty()) {
      paths.themes.push_back(home / ".icons");
    }
    for (const std::filesystem::path& dir : dataDirs) {
      paths.applications.push_back(dir / "applications");
      paths.themes.push_back(dir / "icons");
      paths.pixmaps.push_back(dir / "pixmaps");
    }
    return paths;
  }

  AppIconLookup::AppIconLookup(AppIconSearchPaths paths, std::string_view theme) : m_paths(std::move(paths)) {
    std::vector<std::string> visited;
    if (plainName(theme)) {
      loadTheme(std::string(theme), visited);
    }
    if (std::ranges::find(visited, "hicolor") == visited.end()) {
      loadTheme("hicolor", visited);
    }
  }

  void AppIconLookup::loadTheme(const std::string& name, std::vector<std::string>& visited) {
    if (!plainName(name) || std::ranges::find(visited, name) != visited.end()) {
      return;
    }
    visited.push_back(name);

    Theme theme;
    std::filesystem::path index;
    for (const std::filesystem::path& base : m_paths.themes) {
      if (isDirectory(base / name)) {
        theme.roots.push_back(base / name);
        if (index.empty() && isFile(base / name / "index.theme")) {
          index = base / name / "index.theme";
        }
      }
    }
    if (theme.roots.empty()) {
      return;
    }

    const auto groups = index.empty() ? std::unordered_map<std::string, Group>{} : readKeyFile(index);
    const auto header = groups.find("Icon Theme");
    if (header == groups.end()) {
      // Without an index, as when only applications installed into hicolor, the size directories name themselves.
      for (const std::filesystem::path& root : theme.roots) {
        std::error_code error;
        for (std::filesystem::directory_iterator entry(root, error), end; !error && entry != end;
             entry.increment(error)) {
          const std::string sizeName = entry->path().filename().string();
          const std::string path = sizeName + "/apps";
          if (!isDirectory(root / path) || std::ranges::any_of(theme.directories, [&](const Directory& directory) {
                return directory.path == path;
              })) {
            continue;
          }
          if (sizeName == "scalable") {
            theme.directories.push_back(
                {.path = path, .type = Directory::Type::Scalable, .size = 48, .minSize = 1, .maxSize = 512}
            );
          } else if (const auto size = sizeDirectory(sizeName)) {
            const int pixels = size->first * size->second;
            theme.directories.push_back({.path = path, .size = pixels, .minSize = pixels, .maxSize = pixels});
          }
        }
      }
      m_themes.push_back(std::move(theme));
      return;
    }

    std::vector<std::string> names = split(value(header->second, "Directories"));
    std::ranges::move(split(value(header->second, "ScaledDirectories")), std::back_inserter(names));
    for (std::string& path : names) {
      const auto group = groups.find(path);
      if (group == groups.end()) {
        continue;
      }
      const Group& keys = group->second;
      const std::string_view context = value(keys, "Context");
      const int size = number(keys, "Size", 0);
      if (size <= 0 || (!context.empty() && context != "Applications")) {
        continue;
      }
      const int scale = std::max(1, number(keys, "Scale", 1));
      const std::string_view type = value(keys, "Type");
      theme.directories.push_back({
          .path = std::move(path),
          .type = type == "Fixed"  ? Directory::Type::Fixed
              : type == "Scalable" ? Directory::Type::Scalable
                                   : Directory::Type::Threshold,
          .size = size * scale,
          .minSize = number(keys, "MinSize", size) * scale,
          .maxSize = number(keys, "MaxSize", size) * scale,
          .threshold = number(keys, "Threshold", 2) * scale,
      });
    }
    m_themes.push_back(std::move(theme));

    for (const std::string& parent : split(value(header->second, "Inherits"))) {
      loadTheme(parent, visited);
    }
  }

  const std::vector<AppIconLookup::DesktopEntry>& AppIconLookup::desktopEntries() {
    if (m_entries) {
      return *m_entries;
    }
    m_entries.emplace();
    // A desktop ID found in an earlier directory shadows the same ID later on, even when that entry is hidden.
    std::unordered_set<std::string> seen;
    for (const std::filesystem::path& dir : m_paths.applications) {
      std::vector<std::pair<std::string, std::filesystem::path>> files;
      std::error_code error;
      for (std::filesystem::recursive_directory_iterator entry(dir, error), end; !error && entry != end;
           entry.increment(error)) {
        if (entry->path().extension() != ".desktop") {
          continue;
        }
        std::string id = entry->path().lexically_relative(dir).replace_extension().string();
        std::ranges::replace(id, '/', '-');
        files.emplace_back(lowercase(id), entry->path());
      }
      std::ranges::sort(files);
      for (const auto& [id, path] : files) {
        if (!seen.insert(id).second) {
          continue;
        }
        const auto groups = readKeyFile(path);
        const auto group = groups.find("Desktop Entry");
        if (group == groups.end()) {
          continue;
        }
        const Group& keys = group->second;
        const std::string_view type = value(keys, "Type");
        if ((!type.empty() && type != "Application") || value(keys, "Hidden") == "true") {
          continue;
        }
        m_entries->push_back({
            .id = id,
            .wmClass = lowercase(value(keys, "StartupWMClass")),
            .exec = execProgram(value(keys, "Exec")),
            .name = lowercase(value(keys, "Name")),
            .icon = std::string(value(keys, "Icon")),
            .shown = value(keys, "NoDisplay") != "true",
        });
      }
    }
    return *m_entries;
  }

  std::string AppIconLookup::sandboxedAppId(pid_t pid) const {
    const std::filesystem::path process = m_paths.proc / std::to_string(pid);
    const auto flatpak = readKeyFile(process / "root/.flatpak-info");
    if (const auto group = flatpak.find("Application"); group != flatpak.end()) {
      if (const std::string_view name = value(group->second, "name"); !name.empty()) {
        return lowercase(name);
      }
    }
    // Snap confines each app under the AppArmor label `snap.<snap>.<app>`, and names its entry `<snap>_<app>`.
    for (const char* attribute : {"attr/apparmor/current", "attr/current"}) {
      std::ifstream file(process / attribute);
      std::string label;
      if (!(file >> label) || !label.starts_with("snap.")) {
        continue;
      }
      const std::string_view rest = std::string_view(label).substr(5);
      const size_t dot = rest.find('.');
      if (dot != std::string_view::npos && dot > 0 && dot + 1 < rest.size() && !rest.substr(dot + 1).contains('.')) {
        return lowercase(std::string(rest.substr(0, dot)) + "_" + std::string(rest.substr(dot + 1)));
      }
    }
    return {};
  }

  std::optional<std::string> AppIconLookup::entryIcon(std::string_view appId, pid_t pid) {
    // An entry named after the app id is the common case, and needs no scan of every entry.
    for (const std::string& id : {std::string(appId), lowercase(appId)}) {
      for (const std::filesystem::path& dir : m_paths.applications) {
        const std::filesystem::path path = dir / (id + ".desktop");
        if (!isFile(path)) {
          continue;
        }
        const auto groups = readKeyFile(path);
        const auto group = groups.find("Desktop Entry");
        if (group != groups.end() && value(group->second, "Hidden") != "true") {
          return std::string(value(group->second, "Icon"));
        }
      }
    }

    const std::string id = lowercase(appId);
    const size_t dot = id.rfind('.');
    const std::string tail = dot == std::string::npos ? std::string{} : id.substr(dot + 1);
    const std::string sandboxed = pid > 0 ? sandboxedAppId(pid) : std::string{};
    const std::function<bool(const DesktopEntry&)> rules[] = {
        [&](const DesktopEntry& entry) { return entry.id == id || entry.wmClass == id; },
        [&](const DesktopEntry& entry) { return !sandboxed.empty() && entry.id == sandboxed; },
        [&](const DesktopEntry& entry) { return !tail.empty() && (entry.id == tail || entry.wmClass == tail); },
        [&](const DesktopEntry& entry) {
          return entry.exec == id || entry.name == id || (!tail.empty() && (entry.exec == tail || entry.name == tail));
        },
    };
    const std::vector<DesktopEntry>& entries = desktopEntries();
    for (const auto& rule : rules) {
      const DesktopEntry* hidden = nullptr;
      for (const DesktopEntry& entry : entries) {
        if (!rule(entry)) {
          continue;
        }
        if (entry.shown) {
          return entry.icon;
        }
        if (hidden == nullptr) {
          hidden = &entry;
        }
      }
      if (hidden != nullptr) {
        return hidden->icon;
      }
    }
    return std::nullopt;
  }

  std::vector<std::string> AppIconLookup::iconNames(std::string_view appId, pid_t pid) {
    std::vector<std::string> names;
    const auto add = [&names](std::string name) {
      if (!name.empty() && std::ranges::find(names, name) == names.end()) {
        names.push_back(std::move(name));
      }
    };
    if (const std::optional<std::string> icon = entryIcon(appId, pid)) {
      add(iconName(*icon));
    }
    add(std::string(appId));
    const std::string lower = lowercase(appId);
    add(lower);
    if (const size_t dot = lower.rfind('.'); dot != std::string::npos) {
      add(lower.substr(dot + 1));
    }
    return names;
  }

  std::filesystem::path AppIconLookup::findInTheme(const Theme& theme, std::string_view name, int size) const {
    std::filesystem::path best;
    int bestDistance = std::numeric_limits<int>::max();
    int bestSize = 0;
    for (const Directory& directory : theme.directories) {
      int distance = 0;
      switch (directory.type) {
      case Directory::Type::Fixed:
        distance = std::abs(size - directory.size);
        break;
      case Directory::Type::Scalable:
        distance = size < directory.minSize ? directory.minSize - size : std::max(0, size - directory.maxSize);
        break;
      case Directory::Type::Threshold:
        distance = std::abs(size - directory.size) <= directory.threshold ? 0 : std::abs(size - directory.size);
        break;
      }
      // Equal distances prefer the larger image: downscaling keeps detail that upscaling cannot invent.
      if (distance > bestDistance || (distance == bestDistance && directory.size <= bestSize)) {
        continue;
      }
      if (std::filesystem::path path = fileIn(theme.roots, directory.path, name); !path.empty()) {
        best = std::move(path);
        bestDistance = distance;
        bestSize = directory.size;
      }
    }
    return best;
  }

  std::filesystem::path AppIconLookup::find(std::string_view appId, pid_t pid, int size) {
    if (!plainName(appId)) {
      return {};
    }
    const std::vector<std::string> names = iconNames(appId, pid);
    for (const std::string& name : names) {
      if (name.starts_with('/')) {
        if (decodable(name) && isFile(name)) {
          return name;
        }
        continue;
      }
      if (!plainName(name)) {
        continue;
      }
      for (const Theme& theme : m_themes) {
        if (std::filesystem::path path = findInTheme(theme, name, size); !path.empty()) {
          return path;
        }
      }
    }
    for (const std::string& name : names) {
      if (!plainName(name)) {
        continue;
      }
      if (std::filesystem::path path = fileIn(m_paths.pixmaps, {}, name); !path.empty()) {
        return path;
      }
    }
    return {};
  }

} // namespace umbriel

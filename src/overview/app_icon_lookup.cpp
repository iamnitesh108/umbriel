#include "overview/app_icon_lookup.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <limits>
#include <system_error>
#include <unordered_map>

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
      std::error_code error;
      if (std::filesystem::is_directory(base / name, error)) {
        theme.roots.push_back(base / name);
        if (index.empty() && isFile(base / name / "index.theme")) {
          index = base / name / "index.theme";
        }
      }
    }
    if (index.empty()) {
      return;
    }

    const auto groups = readKeyFile(index);
    const auto header = groups.find("Icon Theme");
    if (header == groups.end()) {
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

  std::vector<std::string> AppIconLookup::iconNames(std::string_view appId) {
    std::vector<std::string> ids{std::string(appId)};
    if (std::string lower = lowercase(appId); lower != ids.front()) {
      ids.push_back(std::move(lower));
    }

    std::vector<std::string> names;
    for (const std::string& id : ids) {
      for (const std::filesystem::path& dir : m_paths.applications) {
        const std::filesystem::path entry = dir / (id + ".desktop");
        if (!isFile(entry)) {
          continue;
        }
        const auto groups = readKeyFile(entry);
        if (const auto group = groups.find("Desktop Entry"); group != groups.end()) {
          if (const std::string_view icon = value(group->second, "Icon"); !icon.empty()) {
            names.emplace_back(icon);
          }
        }
        break;
      }
      if (!names.empty()) {
        break;
      }
    }
    if (names.empty()) {
      if (std::string icon = wmClassIcon(appId); !icon.empty()) {
        names.push_back(std::move(icon));
      }
    }
    std::ranges::move(ids, std::back_inserter(names));
    return names;
  }

  std::string AppIconLookup::wmClassIcon(std::string_view appId) {
    if (!m_wmClassIcons) {
      m_wmClassIcons.emplace();
      for (const std::filesystem::path& dir : m_paths.applications) {
        std::error_code error;
        for (std::filesystem::directory_iterator entry(dir, error), end; !error && entry != end;
             entry.increment(error)) {
          if (entry->path().extension() != ".desktop") {
            continue;
          }
          const auto groups = readKeyFile(entry->path());
          const auto group = groups.find("Desktop Entry");
          if (group == groups.end()) {
            continue;
          }
          const std::string_view wmClass = value(group->second, "StartupWMClass");
          const std::string_view icon = value(group->second, "Icon");
          if (!wmClass.empty() && !icon.empty()) {
            m_wmClassIcons->try_emplace(lowercase(wmClass), icon);
          }
        }
      }
    }
    const auto icon = m_wmClassIcons->find(lowercase(appId));
    return icon == m_wmClassIcons->end() ? std::string{} : icon->second;
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

  std::filesystem::path AppIconLookup::find(std::string_view appId, int size) {
    if (!plainName(appId)) {
      return {};
    }
    const std::vector<std::string> names = iconNames(appId);
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

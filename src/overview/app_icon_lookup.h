#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace umbriel {

  // Directories an icon search reads, in priority order.
  struct AppIconSearchPaths {
    // Directories holding `<app id>.desktop` entries.
    std::vector<std::filesystem::path> applications;
    // Icon theme base directories, each holding `<theme>/index.theme`.
    std::vector<std::filesystem::path> themes;
    // Unthemed fallbacks such as /usr/share/pixmaps.
    std::vector<std::filesystem::path> pixmaps;
  };

  // The XDG search paths from $HOME, $XDG_DATA_HOME, and $XDG_DATA_DIRS.
  [[nodiscard]] AppIconSearchPaths appIconSearchPaths();

  // Resolves application icons the way desktop entries and XDG icon themes describe them: the app's desktop entry
  // names the icon, `theme` and the themes it inherits are searched before hicolor, then the unthemed fallbacks. Only
  // application directories of each theme are searched. Theme indexes are parsed on construction and held until
  // destruction, so keep a lookup only while resolving.
  class AppIconLookup {
  public:
    AppIconLookup(AppIconSearchPaths paths, std::string_view theme);

    // The PNG file for `appId` closest to `size` pixels, or empty when there is none.
    [[nodiscard]] std::filesystem::path find(std::string_view appId, int size) const;

  private:
    struct Directory {
      enum class Type : std::uint8_t { Fixed, Scalable, Threshold };
      std::string path;
      Type type = Type::Threshold;
      int size = 0;
      int minSize = 0;
      int maxSize = 0;
      int threshold = 2;
    };

    struct Theme {
      std::vector<std::filesystem::path> roots;
      std::vector<Directory> directories;
    };

    [[nodiscard]] std::vector<std::string> iconNames(std::string_view appId) const;
    [[nodiscard]] std::filesystem::path findInTheme(const Theme& theme, std::string_view name, int size) const;
    void loadTheme(const std::string& name, std::vector<std::string>& visited);

    AppIconSearchPaths m_paths;
    std::vector<Theme> m_themes;
  };

} // namespace umbriel

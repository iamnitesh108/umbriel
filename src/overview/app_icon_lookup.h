#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <vector>

namespace umbriel {

  // Directories an icon search reads, in priority order.
  struct AppIconSearchPaths {
    // Directories holding desktop entries, subdirectories included.
    std::vector<std::filesystem::path> applications;
    // Icon theme base directories, each holding `<theme>/index.theme`.
    std::vector<std::filesystem::path> themes;
    // Unthemed fallbacks such as /usr/share/pixmaps.
    std::vector<std::filesystem::path> pixmaps;
    // Where process information lives, to recognize Flatpak and Snap clients.
    std::filesystem::path proc = "/proc";
  };

  // The XDG search paths from $HOME, $XDG_DATA_HOME, and $XDG_DATA_DIRS.
  [[nodiscard]] AppIconSearchPaths appIconSearchPaths();

  // Resolves application icons the way desktop entries and XDG icon themes describe them. A window's desktop entry is
  // found by the first of these that matches, ignoring case: its desktop ID or StartupWMClass equals the app id; it is
  // the Flatpak or Snap app the window's process belongs to; its desktop ID or StartupWMClass equals the last part of
  // a reverse-DNS app id; its Exec program or Name equals the app id or that last part. Within one rule an entry
  // shown in menus wins over a NoDisplay one. The entry's Icon is searched in `theme`, the themes it inherits, and
  // hicolor, then in the unthemed fallbacks, followed by names derived from the app id. Only application directories
  // of each theme are searched. Theme indexes are parsed on construction and the desktop entries on the first app no
  // entry is named after, both held until destruction, so keep a lookup only while resolving.
  class AppIconLookup {
  public:
    AppIconLookup(AppIconSearchPaths paths, std::string_view theme);

    // The PNG file, or SVG file in builds that decode SVG, closest to `size` pixels for the window with `appId` whose
    // client process is `pid` (0 when unknown), or empty when there is none.
    [[nodiscard]] std::filesystem::path find(std::string_view appId, pid_t pid, int size);
    // The file for `icon` closest to `size` pixels: an icon name searched like an application's, an absolute path, or a
    // path under ~/. Empty when there is no decodable file.
    [[nodiscard]] std::filesystem::path findIcon(std::string_view icon, int size) const;

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

    // The fields of a desktop entry the rules compare, lowercased, and the Icon it names.
    struct DesktopEntry {
      std::string id;
      std::string wmClass;
      std::string exec;
      std::string name;
      std::string icon;
      bool shown = true;
    };

    [[nodiscard]] std::vector<std::string> iconNames(std::string_view appId, pid_t pid);
    [[nodiscard]] std::optional<std::string> entryIcon(std::string_view appId, pid_t pid);
    // The desktop ID of the Flatpak or Snap app `pid` runs, lowercased, or empty.
    [[nodiscard]] std::string sandboxedAppId(pid_t pid) const;
    [[nodiscard]] const std::vector<DesktopEntry>& desktopEntries();
    [[nodiscard]] std::filesystem::path findInTheme(const Theme& theme, std::string_view name, int size) const;
    void loadTheme(const std::string& name, std::vector<std::string>& visited);

    AppIconSearchPaths m_paths;
    std::vector<Theme> m_themes;
    std::optional<std::vector<DesktopEntry>> m_entries;
  };

} // namespace umbriel

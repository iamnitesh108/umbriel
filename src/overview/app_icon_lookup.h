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

  // Modification times of the desktop entry directories; installing an application changes one.
  [[nodiscard]] std::vector<std::filesystem::file_time_type> desktopEntryStamp(const AppIconSearchPaths& paths);

  // Finds a window's icon through its desktop entry and the XDG icon themes. The entry is the first that matches,
  // ignoring case: desktop ID or StartupWMClass equal to the app id, the Flatpak or Snap app of the window's process,
  // the same two for the app id's last reverse-DNS part, then the Exec program or Name. Indexes are parsed on first
  // use and kept, so hold a lookup only while resolving.
  class AppIconLookup {
  public:
    AppIconLookup(AppIconSearchPaths paths, std::string_view theme);

    // The icon file for the window with `appId` and process `pid` (0 when unknown) closest to `size` pixels, or empty.
    [[nodiscard]] std::filesystem::path find(std::string_view appId, pid_t pid, int size);
    // The file for `icon`, an icon name, an absolute path, or a path under ~/, closest to `size` pixels, or empty.
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
      // Holds application icons: its Context is Applications or unset.
      bool applications = true;
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

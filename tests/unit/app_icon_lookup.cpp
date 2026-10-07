#include "overview/app_icon_lookup.h"

#include "check.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <unistd.h>

using umbriel::AppIconLookup;
using umbriel::AppIconSearchPaths;

namespace {

  // A data directory with applications/, icons/, and pixmaps/, removed with the fixture.
  class IconTree {
  public:
    explicit IconTree(std::string_view name)
        : m_base(
              std::filesystem::temp_directory_path()
              / std::format("umbriel-app-icon-lookup-{}-{}", name, std::to_string(getpid()))
          ) {
      std::filesystem::remove_all(m_base);
      std::filesystem::create_directories(m_base);
    }

    ~IconTree() { std::filesystem::remove_all(m_base); }

    IconTree(const IconTree&) = delete;
    IconTree& operator=(const IconTree&) = delete;

    [[nodiscard]] std::filesystem::path path(std::string_view relative) const { return m_base / relative; }

    std::filesystem::path write(std::string_view relative, std::string_view contents = {}) const {
      const std::filesystem::path file = path(relative);
      std::filesystem::create_directories(file.parent_path());
      std::ofstream(file) << contents;
      return file;
    }

    void desktop(std::string_view id, std::string_view icon) const {
      write(std::format("applications/{}.desktop", id), std::format("[Desktop Entry]\nName=App\nIcon={}\n", icon));
    }

    // index.theme with `dirs`, each a "path:Size:Type:Context" quadruple.
    void
    theme(std::string_view name, std::initializer_list<std::string_view> dirs, std::string_view inherits = {}) const {
      std::string directories;
      std::string groups;
      for (const std::string_view dir : dirs) {
        const size_t first = dir.find(':');
        const size_t second = dir.find(':', first + 1);
        const size_t third = dir.find(':', second + 1);
        const std::string_view subdir = dir.substr(0, first);
        directories += std::format("{}{}", directories.empty() ? "" : ",", subdir);
        groups += std::format(
            "\n[{}]\nSize={}\nType={}\nContext={}\n", subdir, dir.substr(first + 1, second - first - 1),
            dir.substr(second + 1, third - second - 1), dir.substr(third + 1)
        );
      }
      write(
          std::format("icons/{}/index.theme", name),
          std::format("[Icon Theme]\nName={}\nInherits={}\nDirectories={}\n{}", name, inherits, directories, groups)
      );
    }

    [[nodiscard]] AppIconLookup lookup(std::string_view theme = "hicolor") const {
      return {
          AppIconSearchPaths{
              .applications = {path("applications"), path("system-applications")},
              .themes = {path("icons")},
              .pixmaps = {path("pixmaps")},
              .proc = path("proc"),
          },
          theme,
      };
    }

  private:
    std::filesystem::path m_base;
  };

} // namespace

UMBRIEL_TEST(desktopEntryNamesTheIcon) {
  const IconTree tree("desktop");
  tree.theme("hicolor", {"48x48/apps:48:Threshold:Applications"});
  tree.desktop("org.example.App", "example-icon");
  const auto icon = tree.write("icons/hicolor/48x48/apps/example-icon.png");
  tree.write("icons/hicolor/48x48/apps/org.example.App.png");
  CHECK_EQ(tree.lookup().find("org.example.App", 0, 48), icon);
}

UMBRIEL_TEST(appIdNamesTheIconWithoutADesktopEntry) {
  const IconTree tree("app-id");
  tree.theme("hicolor", {"48x48/apps:48:Threshold:Applications"});
  const auto icon = tree.write("icons/hicolor/48x48/apps/example.png");
  CHECK_EQ(tree.lookup().find("example", 0, 48), icon);
}

UMBRIEL_TEST(lowercaseAppIdFindsTheDesktopEntry) {
  const IconTree tree("lowercase");
  tree.theme("hicolor", {"48x48/apps:48:Threshold:Applications"});
  tree.desktop("example", "example-icon");
  const auto icon = tree.write("icons/hicolor/48x48/apps/example-icon.png");
  CHECK_EQ(tree.lookup().find("Example", 0, 48), icon);
}

UMBRIEL_TEST(absoluteIconPathIsUsedAsIs) {
  const IconTree tree("absolute");
  const auto icon = tree.write("elsewhere/example.png");
  tree.desktop("example", icon.string());
  CHECK_EQ(tree.lookup().find("example", 0, 48), icon);
}

UMBRIEL_TEST(closestSizeWinsAndTiesPreferLarger) {
  const IconTree tree("sizes");
  tree.theme(
      "hicolor",
      {"16x16/apps:16:Threshold:Applications", "32x32/apps:32:Threshold:Applications",
       "64x64/apps:64:Threshold:Applications"}
  );
  tree.write("icons/hicolor/16x16/apps/example.png");
  const auto small = tree.write("icons/hicolor/32x32/apps/example.png");
  const auto large = tree.write("icons/hicolor/64x64/apps/example.png");
  CHECK_EQ(tree.lookup().find("example", 0, 30), small);
  CHECK_EQ(tree.lookup().find("example", 0, 48), large);
  CHECK_EQ(tree.lookup().find("example", 0, 128), large);
}

UMBRIEL_TEST(onlyApplicationDirectoriesAreSearched) {
  const IconTree tree("context");
  tree.theme("hicolor", {"48x48/devices:48:Threshold:Devices", "32x32/apps:32:Threshold:Applications"});
  tree.write("icons/hicolor/48x48/devices/example.png");
  const auto icon = tree.write("icons/hicolor/32x32/apps/example.png");
  CHECK_EQ(tree.lookup().find("example", 0, 48), icon);
}

UMBRIEL_TEST(themeAndItsParentsComeBeforeHicolor) {
  const IconTree tree("inherits");
  tree.theme("hicolor", {"48x48/apps:48:Threshold:Applications"});
  tree.theme("child", {"48x48/apps:48:Threshold:Applications"}, "parent");
  tree.theme("parent", {"48x48/apps:48:Threshold:Applications"});
  tree.write("icons/hicolor/48x48/apps/first.png");
  tree.write("icons/hicolor/48x48/apps/second.png");
  const auto own = tree.write("icons/child/48x48/apps/first.png");
  const auto inherited = tree.write("icons/parent/48x48/apps/second.png");
  const auto fallback = tree.write("icons/hicolor/48x48/apps/third.png");
  AppIconLookup lookup = tree.lookup("child");
  CHECK_EQ(lookup.find("first", 0, 48), own);
  CHECK_EQ(lookup.find("second", 0, 48), inherited);
  CHECK_EQ(lookup.find("third", 0, 48), fallback);
}

UMBRIEL_TEST(pixmapsAreTheLastResort) {
  const IconTree tree("pixmaps");
  const auto icon = tree.write("pixmaps/example.png");
  CHECK_EQ(tree.lookup().find("example", 0, 48), icon);
}

UMBRIEL_TEST(namesNeverLeaveTheirDirectory) {
  const IconTree tree("escape");
  tree.write("example.png");
  tree.write("applications/example.desktop", "[Desktop Entry]\nIcon=../example\n");
  CHECK(tree.lookup().find("../example", 0, 48).empty());
  CHECK(tree.lookup().find("example", 0, 48).empty());
  CHECK(tree.lookup("../icons").find("example", 0, 48).empty());
}

UMBRIEL_TEST(missingIconIsEmpty) {
  const IconTree tree("missing");
  tree.theme("hicolor", {"48x48/apps:48:Threshold:Applications"});
  CHECK(tree.lookup().find("example", 0, 48).empty());
  CHECK(tree.lookup().find("", 0, 48).empty());
}

UMBRIEL_TEST(scalableDirectoryCoversItsSize) {
  const IconTree tree("scalable");
  tree.theme("hicolor", {"16x16/apps:16:Threshold:Applications", "scalable/apps:48:Scalable:Applications"});
  tree.write("icons/hicolor/16x16/apps/example.png");
  const auto icon = tree.write("icons/hicolor/scalable/apps/example.png");
  CHECK_EQ(tree.lookup().find("example", 0, 48), icon);
}

UMBRIEL_TEST(startupWmClassFindsTheDesktopEntry) {
  const IconTree tree("wm-class");
  tree.theme("hicolor", {"48x48/apps:48:Threshold:Applications"});
  tree.write(
      "applications/vendor-example-1234.desktop", "[Desktop Entry]\nIcon=example-icon\nStartupWMClass=Example\n"
  );
  tree.write("applications/other.desktop", "[Desktop Entry]\nIcon=other-icon\nStartupWMClass=other\n");
  const auto icon = tree.write("icons/hicolor/48x48/apps/example-icon.png");
  tree.write("icons/hicolor/48x48/apps/other-icon.png");
  AppIconLookup lookup = tree.lookup();
  CHECK_EQ(lookup.find("example", 0, 48), icon);
  CHECK(lookup.find("unrelated", 0, 48).empty());
}

UMBRIEL_TEST(desktopEntryNamedAfterAppIdBeatsStartupWmClass) {
  const IconTree tree("wm-class-order");
  tree.theme("hicolor", {"48x48/apps:48:Threshold:Applications"});
  tree.desktop("example", "named-icon");
  tree.write("applications/vendor.desktop", "[Desktop Entry]\nIcon=class-icon\nStartupWMClass=example\n");
  const auto icon = tree.write("icons/hicolor/48x48/apps/named-icon.png");
  tree.write("icons/hicolor/48x48/apps/class-icon.png");
  CHECK_EQ(tree.lookup().find("example", 0, 48), icon);
}

#ifdef UMBRIEL_SVG_ICONS
UMBRIEL_TEST(scalableSvgCoversItsSize) {
  const IconTree tree("svg");
  tree.theme("hicolor", {"scalable/apps:48:Scalable:Applications", "16x16/apps:16:Threshold:Applications"});
  const auto icon = tree.write("icons/hicolor/scalable/apps/example.svg");
  tree.write("icons/hicolor/16x16/apps/example.png");
  CHECK_EQ(tree.lookup().find("example", 0, 48), icon);
}

UMBRIEL_TEST(absoluteSvgIconPathIsUsed) {
  const IconTree tree("absolute-svg");
  const auto icon = tree.write("elsewhere/example.svg");
  tree.desktop("example", icon.string());
  CHECK_EQ(tree.lookup().find("example", 0, 48), icon);
}
#else
UMBRIEL_TEST(svgIconsAreSkipped) {
  const IconTree tree("svg");
  tree.theme("hicolor", {"scalable/apps:48:Scalable:Applications", "16x16/apps:16:Threshold:Applications"});
  tree.write("icons/hicolor/scalable/apps/example.svg");
  const auto icon = tree.write("icons/hicolor/16x16/apps/example.png");
  CHECK_EQ(tree.lookup().find("example", 0, 48), icon);
}
#endif

UMBRIEL_TEST(reverseDnsTailFindsTheDesktopEntry) {
  const IconTree tree("tail");
  tree.theme("hicolor", {"48x48/apps:48:Threshold:Applications"});
  // Obsidian's entry: neither its desktop ID nor its StartupWMClass is the app id it reports.
  tree.write("applications/obsidian.desktop", "[Desktop Entry]\nIcon=obsidian-icon\nStartupWMClass=md.Obsidian\n");
  const auto icon = tree.write("icons/hicolor/48x48/apps/obsidian-icon.png");
  CHECK_EQ(tree.lookup().find("md.obsidian.Obsidian", 0, 48), icon);
}

UMBRIEL_TEST(reverseDnsTailMatchesStartupWmClass) {
  const IconTree tree("tail-class");
  tree.theme("hicolor", {"48x48/apps:48:Threshold:Applications"});
  tree.write("applications/vendor.desktop", "[Desktop Entry]\nIcon=vendor-icon\nStartupWMClass=Tool\n");
  const auto icon = tree.write("icons/hicolor/48x48/apps/vendor-icon.png");
  CHECK_EQ(tree.lookup().find("com.example.Tool", 0, 48), icon);
}

UMBRIEL_TEST(execProgramFindsTheDesktopEntry) {
  const IconTree tree("exec");
  tree.theme("hicolor", {"48x48/apps:48:Threshold:Applications"});
  tree.write(
      "applications/vendor-browser-1.desktop",
      "[Desktop Entry]\nIcon=browser-icon\nExec=env A=1 /opt/b/Browser --x %U\n"
  );
  tree.write(
      "applications/vendor-wrapped-2.desktop",
      "[Desktop Entry]\nIcon=wrapped-icon\nExec=\"/opt/with space/wrapped\" %F\n"
  );
  tree.write(
      "applications/vendor-sandboxed-3.desktop",
      "[Desktop Entry]\nIcon=sandboxed-icon\nExec=flatpak run --branch=stable org.example.Sandboxed\n"
  );
  const auto browser = tree.write("icons/hicolor/48x48/apps/browser-icon.png");
  const auto wrapped = tree.write("icons/hicolor/48x48/apps/wrapped-icon.png");
  const auto sandboxed = tree.write("icons/hicolor/48x48/apps/sandboxed-icon.png");
  AppIconLookup lookup = tree.lookup();
  CHECK_EQ(lookup.find("browser", 0, 48), browser);
  CHECK_EQ(lookup.find("wrapped", 0, 48), wrapped);
  CHECK_EQ(lookup.find("org.example.Sandboxed", 0, 48), sandboxed);
}

UMBRIEL_TEST(nameFindsTheDesktopEntry) {
  const IconTree tree("name");
  tree.theme("hicolor", {"48x48/apps:48:Threshold:Applications"});
  tree.write("applications/x-1.desktop", "[Desktop Entry]\nName=Notes\nIcon=notes-icon\nExec=/opt/x/run\n");
  const auto icon = tree.write("icons/hicolor/48x48/apps/notes-icon.png");
  CHECK_EQ(tree.lookup().find("notes", 0, 48), icon);
}

UMBRIEL_TEST(strongerRuleWinsOverEarlierEntry) {
  const IconTree tree("rules");
  tree.theme("hicolor", {"48x48/apps:48:Threshold:Applications"});
  // `a-name` only shares the Name, `z-class` shares the StartupWMClass: the class match wins despite sorting later.
  tree.write("applications/a-name.desktop", "[Desktop Entry]\nName=Tool\nIcon=by-name\n");
  tree.write("applications/z-class.desktop", "[Desktop Entry]\nStartupWMClass=tool\nIcon=by-class\n");
  tree.write("icons/hicolor/48x48/apps/by-name.png");
  const auto icon = tree.write("icons/hicolor/48x48/apps/by-class.png");
  CHECK_EQ(tree.lookup().find("tool", 0, 48), icon);
}

UMBRIEL_TEST(shownEntryWinsOverNoDisplayAndHiddenIsSkipped) {
  const IconTree tree("visibility");
  tree.theme("hicolor", {"48x48/apps:48:Threshold:Applications"});
  tree.write("applications/a-handler.desktop", "[Desktop Entry]\nExec=editor\nIcon=handler\nNoDisplay=true\n");
  tree.write("applications/b-removed.desktop", "[Desktop Entry]\nExec=editor\nIcon=removed\nHidden=true\n");
  tree.write("applications/c-editor.desktop", "[Desktop Entry]\nExec=editor\nIcon=editor-icon\n");
  tree.write("applications/d-helper.desktop", "[Desktop Entry]\nExec=helper\nIcon=helper-icon\nNoDisplay=true\n");
  tree.write("icons/hicolor/48x48/apps/handler.png");
  tree.write("icons/hicolor/48x48/apps/removed.png");
  const auto editor = tree.write("icons/hicolor/48x48/apps/editor-icon.png");
  const auto helper = tree.write("icons/hicolor/48x48/apps/helper-icon.png");
  AppIconLookup lookup = tree.lookup();
  CHECK_EQ(lookup.find("editor", 0, 48), editor);
  CHECK_EQ(lookup.find("helper", 0, 48), helper);
}

UMBRIEL_TEST(subdirectoryEntriesUseTheirDesktopId) {
  const IconTree tree("subdir");
  tree.theme("hicolor", {"48x48/apps:48:Threshold:Applications"});
  tree.write("applications/kde/viewer.desktop", "[Desktop Entry]\nIcon=viewer-icon\n");
  const auto icon = tree.write("icons/hicolor/48x48/apps/viewer-icon.png");
  CHECK_EQ(tree.lookup().find("kde-viewer", 0, 48), icon);
}

UMBRIEL_TEST(flatpakProcessFindsItsEntry) {
  const IconTree tree("flatpak");
  tree.theme("hicolor", {"48x48/apps:48:Threshold:Applications"});
  tree.write("applications/org.example.Real.desktop", "[Desktop Entry]\nIcon=real-icon\n");
  tree.write("proc/42/root/.flatpak-info", "[Application]\nname=org.example.Real\nruntime=x\n");
  const auto icon = tree.write("icons/hicolor/48x48/apps/real-icon.png");
  AppIconLookup lookup = tree.lookup();
  CHECK_EQ(lookup.find("unrelated", 42, 48), icon);
  CHECK(lookup.find("unrelated", 43, 48).empty());
}

UMBRIEL_TEST(snapProcessFindsItsEntry) {
  const IconTree tree("snap");
  tree.theme("hicolor", {"48x48/apps:48:Threshold:Applications"});
  tree.write("applications/browser_browser.desktop", "[Desktop Entry]\nIcon=snap-icon\n");
  tree.write("proc/7/attr/current", "snap.browser.browser (enforce)\n");
  const auto icon = tree.write("icons/hicolor/48x48/apps/snap-icon.png");
  CHECK_EQ(tree.lookup().find("unrelated", 7, 48), icon);
}

UMBRIEL_TEST(iconWithExtensionIsFound) {
  const IconTree tree("extension");
  tree.theme("hicolor", {"48x48/apps:48:Threshold:Applications"});
  tree.desktop("example", "example-icon.png");
  const auto icon = tree.write("icons/hicolor/48x48/apps/example-icon.png");
  CHECK_EQ(tree.lookup().find("example", 0, 48), icon);
}

UMBRIEL_TEST(themeWithoutIndexUsesItsSizeDirectories) {
  const IconTree tree("no-index");
  tree.write("icons/hicolor/32x32/apps/example.png");
  const auto icon = tree.write("icons/hicolor/48x48/apps/example.png");
  tree.write("icons/hicolor/48x48/mimetypes/other.png");
  AppIconLookup lookup = tree.lookup();
  CHECK_EQ(lookup.find("example", 0, 48), icon);
  CHECK(lookup.find("other", 0, 48).empty());
}

UMBRIEL_TEST(entryIconInPixmapsBeatsAppIdNamedThemeIcon) {
  const IconTree tree("pixmap-entry");
  tree.theme("hicolor", {"48x48/apps:48:Threshold:Applications"});
  tree.desktop("example", "example-app");
  tree.write("icons/hicolor/48x48/apps/example.png");
  const auto icon = tree.write("pixmaps/example-app.png");
  CHECK_EQ(tree.lookup().find("example", 0, 48), icon);
}

UMBRIEL_TEST(hiddenEntryShadowsTheSameIdInLaterDirectories) {
  const IconTree tree("hidden-shadow");
  tree.theme("hicolor", {"48x48/apps:48:Threshold:Applications"});
  tree.write("applications/example.desktop", "[Desktop Entry]\nIcon=user-icon\nHidden=true\n");
  tree.write("system-applications/example.desktop", "[Desktop Entry]\nIcon=system-icon\n");
  tree.write("icons/hicolor/48x48/apps/user-icon.png");
  tree.write("icons/hicolor/48x48/apps/system-icon.png");
  const auto named = tree.write("icons/hicolor/48x48/apps/example.png");
  CHECK_EQ(tree.lookup().find("example", 0, 48), named);
}

UMBRIEL_TEST(findIconTakesNamesPathsAndHomePaths) {
  const IconTree tree("find-icon");
  tree.theme("hicolor", {"48x48/apps:48:Threshold:Applications"});
  const auto named = tree.write("icons/hicolor/48x48/apps/custom-name.png");
  const auto absolute = tree.write("elsewhere/custom.png");
  const auto home = tree.write("home/icons/custom.png");
  tree.write("elsewhere/custom.txt");
  const AppIconLookup lookup = tree.lookup();
  CHECK_EQ(lookup.findIcon("custom-name", 48), named);
  CHECK_EQ(lookup.findIcon(absolute.string(), 48), absolute);
  const char* previousHome = std::getenv("HOME");
  const std::string savedHome = previousHome != nullptr ? previousHome : "";
  setenv("HOME", tree.path("home").c_str(), 1);
  CHECK_EQ(lookup.findIcon("~/icons/custom.png", 48), home);
  setenv("HOME", savedHome.c_str(), 1);
  CHECK(lookup.findIcon("missing-name", 48).empty());
  CHECK(lookup.findIcon(tree.path("elsewhere/custom.txt").string(), 48).empty());
  CHECK(lookup.findIcon(tree.path("elsewhere/absent.png").string(), 48).empty());
  CHECK(lookup.findIcon("../custom-name", 48).empty());
  CHECK(lookup.findIcon("", 48).empty());
}

int main() { return RUN_TESTS(); }

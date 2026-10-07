# Workspaces Overview

The overview displays every workspace and lets you navigate, focus, close, and
move windows.

## Settings and behavior

```toml
[overview]
zoom = 0.5
scroll_factor_horizontal = 1.0
scroll_factor_vertical = 1.0
background_blur = true
workspace_wallpaper = true
shortcuts = true
shortcut_keys = "1234567890"
shortcut_size = 24
shortcut_position = [0.0, 0.0]
app_icons = false
icon_theme = "hicolor"
icon_size = 24
icon_position = [0.5, 0.5]
badge_background_opacity = 1.0
```

| Key | Default | Description |
| --- | --- | --- |
| `zoom` | `0.5` | Workspace preview scale from 0.1 to 0.75. |
| `scroll_factor_horizontal` | `1.0` | Horizontal gesture and wheel sensitivity. |
| `scroll_factor_vertical` | `1.0` | Vertical gesture and wheel sensitivity. |
| `background_blur` | `true` | Blur the desktop behind the overview. |
| `workspace_wallpaper` | `true` | Show each output's background inside its workspace previews. |
| `shortcuts` | `true` | Show and accept keyboard shortcut badges. |
| `shortcut_keys` | `"1234567890"` | Preferred keys for shortcut badges. |
| `shortcut_size` | `24` | Shortcut label text size in points, from 8 to 72. |
| `shortcut_position` | `[0.0, 0.0]` | Shortcut badge placement as `[x, y]` fractions from 0 to 1. |
| `app_icons` | `false` | Show each window's application icon in a badge of its own. |
| `icon_theme` | `"hicolor"` | Icon theme for application icons; icons it lacks come from `hicolor`. |
| `icon_size` | `24` | Application icon size in logical pixels, from 12 to 128. |
| `icon_position` | `[0.5, 0.5]` | Icon badge placement as `[x, y]` fractions from 0 to 1. |
| `badge_background_opacity` | `1.0` | Opacity of the fill behind badges, from 0 (no fill) to 1. |

Background blur uses `[appearance.blur]`. Preview backgrounds use
`colors.overview.workspace_background` when wallpaper mirroring is disabled or
no background surface is available.

### Open and navigate

Press `Mod+O` by default, or use an
[overview action](actions.md#overview). Opening the overview temporarily hides
scratchpads and pinned windows.

- Click a window to focus it and close the overview.
- Middle-click a window to close it.
- Drag a window to move it to another workspace.
- Use the wheel to navigate vertically and Shift+wheel horizontally.
- Use a four-finger swipe to open or close the overview.

Two-finger scrolling and three-finger swipes navigate continuously. Movement
along the output's [workspace axis](workspaces.md#workspace-axis) moves between
workspaces. Movement across it pans a scrolling workspace preview. Dwindle and
Master layouts have no strip to pan.

Touchpad `natural_scroll` controls gesture direction. The horizontal and
vertical overview factors adjust gesture distance and wheel sensitivity without
changing application scrolling.

#### Which window actions act on

One card carries the full focused-border color while the overview is open. It
is the target for focus, close, and other window actions. Each other workspace
keeps a fainter marker showing which card will receive focus when selected.

The current output follows the cursor. Output-changing actions move the cursor
to their destination as usual.

#### Keyboard shortcuts

Normal `[keybinds]` remain active in the overview. Directional focus actions
select neighboring cards, and workspace or output actions keep their normal
fallback behavior.

Shortcut badges provide direct, unmodified key sequences for visible window
cards. When there are more cards than keys, Umbriel creates multi-key labels.
Backspace removes the last key from a pending sequence. Escape clears the
sequence first and closes the overview when no sequence is pending.

Set `shortcuts = false` to disable badges. A normal keybind takes precedence
over a badge. `shortcut_keys` must contain at least two unique printable ASCII
characters.

#### Application icons

Set `app_icons = true` to show each window's application icon. The icon and the
shortcut label are separate badges, each with its own position and size.

Umbriel finds the icon through the window's desktop entry, ignoring case. It
takes the first entry that matches, in this order:

1. The entry's desktop ID or `StartupWMClass` is the window's app ID.
2. The entry belongs to the Flatpak or Snap app the window's process runs.
3. For a reverse-DNS app ID such as `md.obsidian.Obsidian`, the entry's desktop
   ID or `StartupWMClass` is its last part, `obsidian`.
4. The program in the entry's `Exec`, or its `Name`, is the app ID or that last
   part.

An entry shown in menus is preferred over a `NoDisplay` one, and `Hidden`
entries are ignored. The entry's icon is looked up in `icon_theme`, the themes
it inherits, and `hicolor`, then in `pixmaps` directories; when the entry names
none that exists, the app ID itself is tried as an icon name. PNG and SVG icons
are used; a build without SVG support uses PNG icons only. Each application is
looked up once per session and decoded only while the overview is open. With
`app_icons = false` no icon is looked up or loaded.

#### Badge placement

`shortcut_position` and `icon_position` place their badges as fractions of the
room the card leaves around them: `[0, 0]` is the top-left corner, `[1, 1]` the
bottom-right, `[0.5, 0.5]` the center, and any value between works, such as
`[0.6, 0.4]`. The card includes its tab bar. A badge stays inside the card's
margin, slides just far enough to stay visible when its card runs past the
output edge or under a panel, and shrinks on a card too small for it. A badge
that would cover more than a quarter of a tiny card is hidden.

The fill behind a badge uses `colors.overview.badge_background`, its alpha
scaled by `badge_background_opacity`. At `0` no fill is drawn and the label or
icon sits directly on the window preview.

### Move windows

Drag a window onto another workspace preview to move it. In a dynamic workspace
list, dropping into a gap creates a workspace at that position. Static
workspace inventories accept drops only onto existing previews.

The destination layout shows an insertion preview before the drop. Empty
dynamic workspaces may disappear immediately after their last window is moved
or closed.

### Appearance

Overview cards reuse each window's borders, corner radius, opacity, blur, and
color presentation. Shortcut badges use `colors.overview.badge`.

Configure overview colors under
[`[colors.overview]`](appearance.md#overview-colors).

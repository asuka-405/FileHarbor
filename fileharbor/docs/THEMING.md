# Theming the file manager

The file manager look is a **theme**: one small text file. You can edit a theme live in **Edit > Appearance**, or write the file by hand.

# Quick start

1. Open **Edit > Appearance**.
2. Pick a theme in the list. It is applied immediately.
3. Change any colour or number. The window updates as you go.
4. Press **Save as new theme**, type a name, done. Your theme now shows up in the list.

Built-in themes are read-only. Editing one and saving always creates your own copy.

# Where themes live

- Your themes: `~/.config/fileharbor/themes/`
- Built-in themes: the `themes` folder in FileHarbor's data directory

A theme called `ocean` is the file `ocean.ini`. If you give a theme the same file name as a built-in one, yours wins. Press **Open themes folder** in the Appearance window to jump there.

To share a theme, send the `.ini` file (and its `.css` file if it has one). To install one, drop the files in your themes folder.

# Built-in themes

Twelve themes ship with FileHarbor. Every one has a dark and a light variant; **Edit > Appearance > Mode** picks which one is used (System follows the desktop).

- **System** (default): follows the desktop's light or dark setting and takes the look of its default theme on COSMIC, GNOME, KDE Plasma, Cinnamon and Hyprland, with a neutral palette elsewhere. The accent is coral. Turn on **Use the desktop's accent colour** to use the one from your desktop settings instead.
- **FileHarbor**: warm neutrals with the coral accent.
- **Ocean**, **Forest**, **Dusk**, **Sand**, **Slate**: calm colour themes.
- **COSMIC**, **GNOME**, **KDE**, **Cinnamon**, **Hyprland**: palettes made to sit next to the default look of each desktop.

The desktop is detected from `XDG_CURRENT_DESKTOP`, the settings portal (`org.freedesktop.appearance`), COSMIC's config files, KDE's `kdeglobals` and the GNOME/Cinnamon settings. Choose **System GTK theme** to stop overriding colours at all.

# The file format

A theme can have a `[Dark]` and a `[Light]` group (preferred), or a single `[Colors]` group (older style, still supported).

```
[Theme]
Name=Ocean
Description=Deep blue-grey with a cyan accent.

[Dark]
window=#101419
surface=#191f27
surface_alt=#212933
surface_raised=#2a3541
text=#e8eaed
text_dim=#afb7c0
text_faint=#848e9a
accent=#4cc9f0
accent_text=#12100f
border=#303c4b
border_strong=#44556a
hover=#28323e
success=#5fd18a
warning=#f2b84b
error=#ff7b72
info=#6cb6ff

[Light]
; same keys

[Metrics]
radius=0
gap=8
border=0
```

Every key is optional. Anything left out falls back to the FileHarbor palette for that variant, so a theme can be as short as:

```
[Theme]
Name=Just Orange
[Dark]
accent=#ff9933
[Light]
accent=#b45f00
```

A theme with only `[Colors]` and `Dark=true|false` works as before and has a single variant.

# [Theme]

- `Name`: the name shown in the list.
- `Description`: one line shown under it.
- `Adaptive`: `true` makes the theme follow the desktop's mode and default look (the System theme does this).
- `Dark`: only for `[Colors]` themes. `true` or `false`. Defaults to `true`.

# Colours

Colours are written `#rrggbb`.

- `window`: the backdrop behind everything, also the title bar and tab bar.
- `surface`: the sidebar, the file area and the terminal panel.
- `surface_alt`: the toolbar, column headers, status bar and text fields.
- `surface_raised`: menus, popovers and dialogs.
- `text`, `text_dim`, `text_faint`: normal, secondary and hint or disabled text.
- `accent`: selection edge, current folder underline, active tab line, focus rings, progress bars and primary buttons. Selected rows use a soft tint of it.
- `accent_text`: text on top of the accent. If you change `accent` and leave this out, black or white is chosen for you.
- `border`, `border_strong`: separators and outlines, and the stronger outline for fields and buttons.
- `hover`: the highlight under the pointer.
- `success`, `warning`, `error`, `info`: status colours.

# Roundness and spaciousness

Two settings in **Edit > Appearance** apply on top of every theme. Their defaults leave the look as the theme made it.

- **Corner roundness**: *As the theme* (default), *Square* (0 px), *Soft* (4 px), *Rounded* (8 px), *Very round* (12 px).
- **Spaciousness**: *Compact*, *Default*, *Comfortable*, *Spacious*. Changes control height (28 / 32 / 36 / 40 px), row height, the padding steps and the gap between panels (the theme's `gap` minus 2, plus 2 or plus 4).

From a shell: `gsettings set in.archdev.FileHarbor.preferences fileharbor-roundness rounded`, and the same with `fileharbor-spaciousness`. They do not apply with **System GTK theme**.

# [Metrics]

Whole numbers, in pixels.

- `radius`: corner rounding of panels and buttons, 0 to 32. Default `0`. The roundness setting above overrides it.
- `gap`: space between the sidebar, file area and terminal, 0 to 32. Default `8`.
- `border`: outline thickness around panels, 0 to 6. Default `0`: panels are separated by tone, not lines.

# Making themes

The built-in `.ini` files are generated by `scripts/gen-themes.py` (it also checks text contrast). To make your own, copy one into `~/.config/fileharbor/themes/` and edit it, or change colours live in **Edit > Appearance** and press **Save as new theme**.

# Going further: extra CSS

For anything the palette cannot express, put GTK CSS next to the theme with the same name: `ocean.ini` is paired with `ocean.css`. It is loaded after the built-in styling, so it can override anything. Every palette colour is available to it as `@ns_<key>`, for example `@ns_window`, `@ns_surface_raised`, `@ns_text_faint`, `@ns_accent` or `@ns_error`.

Useful selectors, all scoped to the file manager window:

- `.nemo-window` the window itself
- `.sidebar` the places sidebar
- `.nemo-window-pane` the file area (toolbar and file view)
- `.nemo-window-pane toolbar` the toolbar
- `.path-bar button` the path bar crumbs
- `.fileharbor-titlebar` the title bar that holds the menus
- `.fileharbor-wc`, `.fileharbor-wc-min`, `.fileharbor-wc-max`, `.fileharbor-wc-close` the window buttons
- `.fileharbor-tabstrip`, `.fileharbor-tab`, `.fileharbor-tab.active` the tab bar under the title bar
- `.fileharbor-terminal` the embedded terminal panel
- `statusbar` the status bar
- `treeview.view`, `iconview` the file lists

Example, a thick accent line under the toolbar and bigger text in the sidebar:

```
.nemo-window-pane toolbar { border-bottom: 3px solid @ns_accent; }
.sidebar treeview.view { font-size: 110%; }
```

CSS errors are printed to the terminal the file manager was started from, and the previous look stays in place.

# Tips

- Developer mode (see `DEVELOPER.md`) reloads `~/.config/fileharbor/dev.css` as you save it, which is the quickest way to try CSS.
- Start the file manager with `NEMO_CLASSIC=1` to disable all theming for one run, for example if a theme is unreadable.
- Choose **System GTK theme** in the list to stop overriding colours altogether.
- Keep at least 4.5:1 contrast between `text` and `surface`, and between `accent_text` and `accent`, so text stays readable.

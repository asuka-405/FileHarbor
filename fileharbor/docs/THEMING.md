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
- Built-in themes: the `themes` folder inside Nemo's data directory

A theme called `ocean` is the file `ocean.ini`. If you give a theme the same file name as a built-in one, yours wins. Press **Open themes folder** in the Appearance window to jump there.

To share a theme, send the `.ini` file (and its `.css` file if it has one). To install one, drop the files in your themes folder.

# The file format

```
[Theme]
Name=Ocean
Dark=true

[Colors]
window=#0d1b2a
surface=#1b263b
surface_alt=#233047
text=#e0e1dd
text_dim=#8d99ae
accent=#4cc9f0
accent_text=#06202b
border=#2f3e58
hover=#2a3a55

[Metrics]
radius=0
gap=8
border=1
```

Every key is optional. Anything you leave out falls back to the COSMIC default for dark or light, so a theme can be as short as:

```
[Theme]
Name=Just Orange
[Colors]
accent=#ff9933
```

# [Theme]

- `Name`: the name shown in the list.
- `Dark`: `true` or `false`. Chooses the fallback palette and tells GTK to use its dark widgets. Defaults to `true`.
- `FollowCosmic`: `true` makes the theme follow the COSMIC desktop: its light or dark mode and the accent colour you picked in COSMIC Settings. Colours you set explicitly in the file still win.

# [Colors]

Colours are written `#rrggbb`.

- `window`: the backdrop behind everything, also used by the title bar and tab bar.
- `surface`: the sidebar, the file area and the terminal panel.
- `surface_alt`: the toolbar, column headers, tab bar, status bar and text fields.
- `text`: normal text.
- `text_dim`: secondary text such as the status bar and inactive tabs.
- `accent`: the accent edge on selected rows, the current folder underline in the path bar, the active tab's top line, focused text fields, progress bars and primary buttons. Selected rows use a soft tint of it.
- `accent_text`: text drawn on top of the accent. If you change `accent` and leave this out, a readable black or white is chosen for you.
- `border`: outlines around panels and separators.
- `hover`: the highlight under the pointer.

# [Metrics]

Whole numbers, in pixels.

- `radius`: corner rounding of panels and buttons. `0` means square corners, which is the default.
- `gap`: the space between the sidebar, file area and terminal. The default is `10`.
- `border`: outline thickness around panels. The default is `0`: panels are separated by tone, not lines. Raise it for a line-art look.

# Going further: extra CSS

For anything the palette cannot express, put GTK CSS next to the theme with the same name: `ocean.ini` is paired with `ocean.css`. It is loaded after the built-in styling, so it can override anything. The palette colours are available to it as `@ns_window`, `@ns_surface`, `@ns_surface_alt`, `@ns_text`, `@ns_text_dim`, `@ns_accent`, `@ns_accent_text`, `@ns_border` and `@ns_hover`.

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

- Start the file manager with `NEMO_CLASSIC=1` to disable all theming for one run, for example if a theme is unreadable.
- Choose **System GTK theme** in the list to stop overriding colours altogether.
- Keep at least 4.5:1 contrast between `text` and `surface`, and between `accent_text` and `accent`, so text stays readable.

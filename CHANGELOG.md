# Changelog

All notable changes to FileHarbor. The version is in the `VERSION` file; FileHarbor 0.x is based on
Nemo 6.8.3.

## Unreleased

### Interface
- Renamed the app identity to FileHarbor / `in.archdev.FileHarbor` (binary `fileharbor`, with a `nemo`
  symlink); existing settings are copied over on first start.
- New design system: spacing scale, control heights, type scale, states and focus rings.
- 12 themes with dark and light variants, a Mode choice and an adaptive System theme (default) that detects
  COSMIC, GNOME, KDE Plasma, Cinnamon and Hyprland; coral accent by default, optional desktop accent.
- Corner roundness and spaciousness settings (Edit > Appearance); the defaults keep the theme's own look.
- `fileharbor-setup`: one-time setup of settings, the Storage Sense service and desktop integration, for one
  account or the whole system, with `--dry-run` and `--undo`.
- Developer mode: debug log, GTK inspector, `dev.css` hot reload and a developer panel.

### Fixes
- Storage Sense connects again from an installed copy (falls back to starting the service directly).
- The launcher script removes entries left by earlier installs, so only one FileHarbor entry remains.

## 0.1.0

First release.

### Interface
- COSMIC-style default theme that follows the desktop's light/dark mode and accent colour, square
  corners, 19 further colour options for the text editor and Markdown preview, and a live theme editor
  (Edit > Appearance) with a theming guide.
- Menus in the title bar, always-visible tab strip, breadcrumb bar and terminal at the top of the file area.
- Sidebar sized to its content (140 to 280 px), or a Miller-column navigator.
- Active-row highlighting in lists and sidebars as a flat tint, and contrast fixes across the window,
  menus, popovers, buttons and fields.
- First-run walkthrough.

### Terminal
- Embedded VTE terminal (F4, toolbar button), linked to the file view in both directions, including
  `ssh` sessions (the view follows into `sftp://`). Keyboard focus stays in the terminal while the shell
  drives navigation.

### Storage
- Storage page with a background indexing service: usage by type, treemap, folders, largest files,
  safe-to-clean items and recent growth, with delete, properties and expandable folders.

### Remote
- "Connect to a server" dialog for SFTP, FTP, FTPS, WebDAV, SMB and NFS, with saved servers in the sidebar.

### Built-in tools (open in tabs)
- Text editor: syntax highlighting for about 170 languages, search and replace, go to line, comment
  toggle, minimap, zoom, encodings and line endings, Prettier formatting, recovery copies.
- Markdown and MDX preview with a collapsible editor, front matter, callouts and code colouring.
- Images: viewer and editor with camera RAW, batch resize and convert.
- Media player for music and video, with trim, convert, extract audio and GIF.
- Developer tools: JSON, YAML, Base64, URL and HTML encoding, hashes, UUID/ULID, passwords, timestamps,
  number bases, colours, regex, JWT, diff, cron, text utilities.
- Double-clicking Markdown, text, pictures, video and music opens them in these tabs (each can be
  turned off). Context menus offer the tools that fit the selection.

### Extensions
- Extension folders (`extension.json`) add context-menu actions, sidebar entries, custom pages, Tools-menu
  entries and themes. Tools > Extensions turns them on and off. A sample extension is bundled.

### Packaging
- Arch Linux package recipe and a Flatpak manifest (see `packaging/`), release script, `scripts/` for
  building, running and testing.

### Image editor, releases, packaging and launcher
- Releases: publishing a production release on GitHub (not a pre-release, no `-alpha`/`-beta`/`-rc` tag) now builds and attaches the tarball, an Arch package and a Flatpak bundle, and updates the AUR package.
- Launcher: the desktop file is named FileHarbor and uses the new icon; `scripts/install-launcher.sh` registers a checkout.
- Image editor: **Remove BG** makes a plain-coloured background transparent (tolerance, edge shrink and softening, colour picker, live preview).
- Packaging: Prettier, LibRaw, gst-libav, catdoc and gvfs are now installed by default by the AUR package, the Flatpak and the Debian files.

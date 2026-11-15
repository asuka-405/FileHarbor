# One-time setup

FileHarbor opens its setup page by itself the first time it starts: a tab with the choices below. **Apply setup** applies them, **Not now** closes the tab (it comes back at the next start), **Don't show again** keeps the defaults. **Tools > FileHarbor setup** opens it again at any time. Ticking *Apply these as defaults for everyone on this computer* runs the system scope through `pkexec`, which asks for your password.

The page runs `fileharbor-setup`, the same program you can run in a terminal, so both do the same thing and `--undo` takes either back.

`fileharbor-setup` sets FileHarbor up in one go. It asks about each thing (Enter keeps the current value), applies the answers, and records what it did so it can be undone. From a checkout use `scripts/setup.sh` after `scripts/build.sh`.

# Scope

- **User** (default): this account only, no root.
- **System** (`sudo fileharbor-setup --system`): every account.

| | User | System |
| --- | --- | --- |
| Settings | `gsettings` in your dconf database | defaults in `/etc/dconf/db/local.d/00-fileharbor-setup`, `system-db:local` added to `/etc/dconf/profile/user`, then `dconf update`. Only the values you change are written; users can still override them. |
| Default for folders | `xdg-mime default` (`~/.config/mimeapps.list`) | `/etc/xdg/mimeapps.list` |
| Storage Sense | `systemctl --user` | `systemctl --global` |
| Launcher, icons, D-Bus services | registered under `~/.local/share` when run from a checkout; a package already installs them | not touched (the package or `meson install` does it) |
| Folders | `~/.config/fileharbor/themes`, `~/.local/share/fileharbor/extensions` | none |

# What it asks

- **Appearance**: theme, light or dark, desktop accent colour, corner roundness, spaciousness.
- **Browsing**: default folder view, single or double click, hidden files, folders first, reopen tabs.
- **Built-in tools**: editor, Markdown preview and media player for their file types, terminal panel, ssh following.
- **Developer mode**.
- **Storage Sense service**: *on-demand* (starts when the Storage page asks, the default), *always* (a systemd unit starts it at login) or *off*. `--root DIR` and `--threshold-mb N` change what it indexes and when it rescans (written as a systemd drop-in).
- **Desktop integration**: make FileHarbor the default for folders, and answer "Show in folder" requests (`org.freedesktop.FileManager1`).

Settings are copied from an earlier Nemo install automatically when FileHarbor first starts, not by this tool.

# Options

```
fileharbor-setup --dry-run                  show everything it would do, change nothing
fileharbor-setup --yes                      no questions; keep current values
fileharbor-setup --yes --storage always --theme ocean --mode dark --roundness rounded --spaciousness comfortable
fileharbor-setup --set show-hidden-files=true   set any one preference (repeatable)
fileharbor-setup --default-manager | --no-default-manager
fileharbor-setup --force                    run again (it refuses by default once it has run)
fileharbor-setup --status                   print what the last run did
fileharbor-setup --undo                     restore settings, default handlers, units and files it wrote
```

`--undo` never touches your own themes, extensions or settings it did not change.

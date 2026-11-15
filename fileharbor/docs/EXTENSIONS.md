# Making FileHarbor extensions

An extension is a folder. It can add **context-menu items**, **sidebar entries**, **custom pages**, **Tools menu entries** and **themes**. No compiling, no plugin API: everything is a small text file or a script in any language.

> Extensions run with your permissions. Only install ones you trust. Manage them in **Tools > Extensions…**

# Where extensions live

- Yours: `~/.local/share/fileharbor/extensions/<name>/`
- Shared: `<prefix>/share/fileharbor/extensions/<name>/`

If both have the same id, yours wins. Restart FileHarbor after adding or changing one. **Tools > Extensions… > Open extensions folder** takes you there.

A folder is picked up when it contains `extension.json`. The folder `system-glance` that ships with FileHarbor is a complete working example: copy it and change it.

# extension.json

```
{
  "id": "my-extension",
  "name": "My Extension",
  "version": "1.0",
  "description": "What it does, in one line.",
  "sidebar": [ { "name": "My page", "icon": "star-symbolic", "page": "home" } ],
  "pages":   [ { "id": "home", "title": "My page", "exec": ["python3", "$EXT/home.py"], "refresh": 10 } ],
  "tools":   [ { "name": "Do the thing", "icon": "system-run-symbolic", "exec": ["$EXT/thing.sh"] } ]
}
```

Everything except `id` and `name` is optional. `id` may use letters, digits, `-`, `_` and `.`.

`$EXT/` at the start of a command word means "this extension's folder", so scripts can travel with it. Commands are a list of words (no shell), or a single string that is split like a shell would.

# Context-menu items

Put standard Nemo action files in `actions/`. They appear in the right-click menu, with the usual conditions:

```
[Nemo Action]
Name=Convert to PDF
Exec=my-converter %F
Icon-Name=document-save-symbolic
Selection=notnone
Extensions=odt;docx;
```

`Selection` is `s` (one item), `m` (several), `notnone`, `any`. Filter with `Extensions=` or `Mimetypes=`. In `Exec`, `%f` is the file, `%F` all selected files, `%P` the current folder. The Nemo actions documentation describes every key.

# Sidebar entries

```
"sidebar": [
  { "name": "Projects", "icon": "folder-symbolic", "uri": "file:///home/me/Projects" },
  { "name": "My page",  "icon": "star-symbolic",   "page": "home" }
]
```

`uri` opens any location. `page` opens one of the extension's own pages.

# Pages

A page is a command that prints JSON. FileHarbor runs it, draws the result, and runs it again every `refresh` seconds (2 or more; leave it out to refresh only on request or after a button asks for it). The command runs in the extension's folder.

```
{
  "title": "Hello",
  "blocks": [
    { "type": "heading", "level": 1, "text": "Hello" },
    { "type": "text", "text": "Some words.", "dim": true },
    { "type": "progress", "value": 0.4, "label": "40%" },
    { "type": "list", "items": [
        { "title": "Home", "subtitle": "/home/me", "icon": "user-home-symbolic", "open": "/home/me" },
        { "title": "Run it", "trailing": "now", "exec": ["$EXT/run.sh"], "refresh": true }
    ] },
    { "type": "table", "columns": ["Name", "Size"], "rows": [["a", "1 MB"], ["b", "2 MB"]] },
    { "type": "button", "label": "Open Downloads", "open": "~/Downloads" },
    { "type": "image", "path": "/path/to/picture.png", "width": 400 },
    { "type": "separator" }
  ]
}
```

Block types: `heading` (`level` 1 or 2), `text`, `separator`, `progress`, `list`, `table`, `button`, `image`.

Anything with `open` goes to that folder or address in the file manager. Anything with `exec` runs that command (a list of words); add `"refresh": true` to redraw the page when it finishes.

While you build a page, run the script in a terminal: it must print one JSON object and exit. If it fails, the page shows the error text.

# Tools menu

`tools` entries appear in the **Tools** menu under the built-in tools. They run `exec` when chosen.

# Themes

Put theme files in `themes/` (see the theming guide: an `.ini` file, optionally with a `.css` next to it). They show up in **Edit > Appearance** and are read-only there; copy one to edit it.

# Tips

- Keep scripts quick: a page that takes seconds makes the file manager feel slow. Cache anything expensive.
- Test an action by running its `Exec` line in a terminal with a file path instead of `%F`.
- To remove an extension, delete its folder, or switch it off in **Tools > Extensions…**

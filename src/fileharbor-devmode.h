/* fileharbor-devmode.h - developer mode: everything FileHarbor development and debugging needs.
 * New file, not in upstream. See fileharbor/docs/DEVELOPER.md. */

#ifndef FILEHARBOR_DEVMODE_H
#define FILEHARBOR_DEVMODE_H

#include <glib.h>

/* TRUE when started with --developer or FILEHARBOR_DEV=1, or when "Developer mode" is switched on in the
 * Appearance window (gsettings: in.archdev.FileHarbor.preferences developer-mode). */
gboolean fh_devmode_enabled (void);

/* Installs the log writer as early as possible (before the settings exist), from the environment alone. */
void     fh_devmode_early_init (void);

/* Reads the setting, turns on what developer mode turns on, and follows later changes of the setting. */
void     fh_devmode_init (void);

/* ~/.cache/fileharbor/debug: debug.log (every GLib message, with level and domain) and theme.json
 * (the resolved theme and what was detected on the desktop), written while developer mode is on. */
char    *fh_devmode_debug_dir (void);

/* ~/.config/fileharbor/dev.css: extra CSS that is loaded after the theme and reloaded whenever the file is saved. */
char    *fh_devmode_css_path (void);

#endif

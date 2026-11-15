/* fileharbor-desktop-theme.h - what the desktop looks like: environment, light or dark, accent colour.
 * New file, not in upstream. */

#ifndef FILEHARBOR_DESKTOP_THEME_H
#define FILEHARBOR_DESKTOP_THEME_H

#include <glib-object.h>

/* "cosmic", "gnome", "kde", "cinnamon", "hyprland" or "generic" (also Xfce, MATE, Budgie, sway, niri, ...).
 * These are the ids of the built-in themes that match each desktop. */
const char *fh_desktop_id           (void);

/* The desktop's own light/dark setting: COSMIC's mode file, the XDG settings portal (GNOME, KDE, COSMIC,
 * Hyprland with a portal), GNOME's and Cinnamon's gsettings, KDE's kdeglobals, GTK's settings.ini, then GTK_THEME.
 * Defaults to dark when nothing can be read. */
gboolean    fh_desktop_prefers_dark (void);

/* The accent colour the user picked in the desktop settings as "#rrggbb", or NULL when there is none
 * (Cinnamon, Hyprland, older GNOME and KDE). Free with g_free(). */
char       *fh_desktop_accent       (void);

/* Calls func (after a short debounce) whenever any of the above may have changed. Call once. */
void        fh_desktop_watch        (GCallback func);

#endif

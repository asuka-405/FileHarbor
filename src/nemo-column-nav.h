/* nemo-column-nav.h - Miller-column folder navigator. New file, not in upstream Nemo.
 *
 * Replaces the places/tree sidebar: the whole path from / to the current
 * folder is shown as a row of columns, each listing its folders, so the
 * filesystem is always visible at a glance. Quick-access icons for the
 * common folders, Trash, Storage and mounted drives sit above the columns. */

#ifndef NEMO_COLUMN_NAV_H
#define NEMO_COLUMN_NAV_H

#include <gtk/gtk.h>
#include "nemo-window-types.h"

G_BEGIN_DECLS

#define NEMO_TYPE_COLUMN_NAV (nemo_column_nav_get_type ())
G_DECLARE_FINAL_TYPE (NemoColumnNav, nemo_column_nav, NEMO, COLUMN_NAV, GtkBox)

GtkWidget *nemo_column_nav_new (NemoWindow *window);

G_END_DECLS

#endif

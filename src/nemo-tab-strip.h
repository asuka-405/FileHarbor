/* nemo-tab-strip.h - window-wide tab bar. New file, not in upstream Nemo.
 *
 * Shows the tabs of the active pane in a strip at the very top of the window
 * (under the title bar) and drives the pane's hidden notebook. */

#ifndef NEMO_TAB_STRIP_H
#define NEMO_TAB_STRIP_H

#include <gtk/gtk.h>
#include "nemo-window-types.h"

G_BEGIN_DECLS

#define NEMO_TYPE_TAB_STRIP (nemo_tab_strip_get_type ())
G_DECLARE_FINAL_TYPE (NemoTabStrip, nemo_tab_strip, NEMO, TAB_STRIP, GtkBox)

GtkWidget *nemo_tab_strip_new     (NemoWindow *window);
void       nemo_tab_strip_attach  (NemoTabStrip *strip, GtkNotebook *notebook);
void       nemo_tab_strip_refresh (NemoTabStrip *strip);

G_END_DECLS

#endif

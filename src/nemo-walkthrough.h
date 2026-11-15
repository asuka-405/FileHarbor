/* nemo-walkthrough.h - first-run tour. New file, not in upstream Nemo. */

#ifndef NEMO_WALKTHROUGH_H
#define NEMO_WALKTHROUGH_H

#include <gtk/gtk.h>

void nemo_walkthrough_present (GtkWindow *parent);
void nemo_walkthrough_maybe_present (GtkWindow *parent);   /* only if it was never shown */

#endif

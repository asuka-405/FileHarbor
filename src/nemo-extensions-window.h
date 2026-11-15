/* nemo-extensions-window.h - manage FileHarbor extensions. New file, not in upstream Nemo. */

#ifndef NEMO_EXTENSIONS_WINDOW_H
#define NEMO_EXTENSIONS_WINDOW_H

#include <gtk/gtk.h>

void nemo_extensions_window_present (GtkWindow *parent);
void nemo_launch_tool               (const char *tool, GList *files);   /* files: GList of path strings */

#endif

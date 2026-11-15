/* fileharbor-python.h - run the Python tools inside the file manager. New file, not in upstream Nemo.
 *
 * The tools (fileharbor/tools/fhtools) are GTK widgets written in Python. The file manager embeds a Python
 * interpreter the first time one is opened and puts the widget in a tab, so no tool needs its own
 * window. Python is only started when a tool is used. */

#ifndef FILEHARBOR_PYTHON_H
#define FILEHARBOR_PYTHON_H

#include <gtk/gtk.h>

/* Creates the tool's widget. `files` is a NULL-terminated array of absolute paths (may be NULL).
 * Returns a widget that is owned by Python until it is added to a container, or NULL with `error` set. */
GtkWidget *fh_python_create_tool (const char *tool, char **files, GError **error);

#endif

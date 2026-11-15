/* nemo-terminal-panel.h - embedded terminal for the FileHarbor fork of Nemo.
 *
 * New file, not present in upstream Nemo. A VTE terminal that follows the
 * folder shown in the active view (like Dolphin's F4 panel) and, when the
 * shell reports its directory (OSC 7), makes the view follow the shell.
 */

#ifndef NEMO_TERMINAL_PANEL_H
#define NEMO_TERMINAL_PANEL_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define NEMO_TYPE_TERMINAL_PANEL (nemo_terminal_panel_get_type ())
G_DECLARE_FINAL_TYPE (NemoTerminalPanel, nemo_terminal_panel, NEMO, TERMINAL_PANEL, GtkBox)

GtkWidget *nemo_terminal_panel_new          (void);
void       nemo_terminal_panel_set_location (NemoTerminalPanel *panel, GFile *location);
void       nemo_terminal_panel_focus        (NemoTerminalPanel *panel);
void       nemo_terminal_panel_expect_location (NemoTerminalPanel *panel, GFile *location);

/* signal "shell-directory-changed" (GFile *directory): the user changed
 * directory inside the terminal and the shell reported it. */

G_END_DECLS

#endif

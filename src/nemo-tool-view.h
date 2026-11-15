/* nemo-tool-view.h - shows a built-in tool (image editor, terminal-free utilities...) in a tab.
 * New file, not in upstream Nemo. */

#ifndef NEMO_TOOL_VIEW_H
#define NEMO_TOOL_VIEW_H

#include "nemo-view.h"

#define NEMO_TYPE_TOOL_VIEW nemo_tool_view_get_type()
#define NEMO_TOOL_VIEW(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), NEMO_TYPE_TOOL_VIEW, NemoToolView))
#define NEMO_IS_TOOL_VIEW(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NEMO_TYPE_TOOL_VIEW))
#define NEMO_TOOL_VIEW_ID "OAFIID:Nemo_File_Manager_Tool_View"

typedef struct NemoToolViewDetails NemoToolViewDetails;

typedef struct {
	NemoView parent_instance;
	NemoToolViewDetails *details;
} NemoToolView;

typedef struct {
	NemoViewClass parent_class;
} NemoToolViewClass;

GType    nemo_tool_view_get_type (void);
void     nemo_tool_view_register (void);
gboolean nemo_uri_is_tool        (const char *uri);

/* Opens `tool` (an id from fh_tools_all) in a new tab of `window`. `paths` is a GList of char*; may be NULL. */
void     nemo_open_tool_tab      (GtkWidget *window, const char *tool, GList *paths);

/* Double-click hook: a single local Markdown, text, picture, video or audio file opens in its built-in tool tab. Returns TRUE if it did. */
gboolean nemo_tool_view_try_activate (GtkWidget *window, GList *files);

#endif

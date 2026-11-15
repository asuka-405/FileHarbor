/* fileharbor-extensions.h - find and describe FileHarbor extensions. New file, not in upstream Nemo.
 *
 * An extension is a folder with an extension.json (see fileharbor/docs/EXTENSIONS.md) in
 * ~/.local/share/fileharbor/extensions/ or <datadir>/fileharbor/extensions/. It can add context-menu
 * items (standard .nemo_action files in its actions/ folder), sidebar entries, custom pages, tools
 * in the Tools menu, and themes. Everything here is read-only discovery; nothing runs until the
 * user opens an item. */

#ifndef FILEHARBOR_EXTENSIONS_H
#define FILEHARBOR_EXTENSIONS_H

#include <glib.h>

G_BEGIN_DECLS

#define FH_PAGE_URI_PREFIX "x-fileharbor-page:///"

typedef struct {
	char *name;
	char *icon;
	char *uri;       /* what the sidebar entry opens */
} FhSidebarEntry;

typedef struct {
	char  *id;
	char  *title;
	char  *icon;
	char **argv;     /* command that prints the page as JSON */
	int    refresh;  /* seconds, 0 = only on request */
} FhPage;

typedef struct {
	char  *name;
	char  *icon;
	char **argv;
} FhTool;

typedef struct {
	char    *id;
	char    *name;
	char    *version;
	char    *description;
	char    *dir;          /* absolute path of the extension folder */
	gboolean builtin;      /* installed system-wide (not removable by the user) */
	gboolean enabled;
	GList   *sidebar;      /* FhSidebarEntry* */
	GList   *pages;        /* FhPage* */
	GList   *tools;        /* FhTool* */
	char    *error;        /* set when extension.json could not be used */
} FhExtension;

GList       *fh_extensions_get_all        (void);          /* of FhExtension*, cached; do not free */
void         fh_extensions_reload         (void);
GList       *fh_extensions_get_action_dirs (void);         /* of char*; enabled extensions only; free with g_list_free_full */
GList       *fh_extensions_get_theme_dirs  (void);         /* of char*; enabled extensions only */
FhPage      *fh_extensions_find_page      (const char *uri, FhExtension **ext_out);
void         fh_extensions_set_enabled    (const char *id, gboolean enabled);
char        *fh_extensions_user_dir       (void);

/* ---- built-in tools -------------------------------------------------------------------
 * The tools (fileharbor/tools/fhtools) open in tabs at x-fileharbor-tool:///<tool>?f=<path>&f=<path> */
#define FH_TOOL_URI_PREFIX "x-fileharbor-tool:///"

typedef struct {
	const char *id;       /* devtools, image, resize, markdown, music, video, hub */
	const char *label;
	const char *icon;
	const char *desc;
} FhToolInfo;

const FhToolInfo *fh_tools_all        (int *count);
const FhToolInfo *fh_tool_lookup      (const char *id);
char             *fh_tool_uri         (const char *tool, GList *paths);          /* paths: char* */
gboolean          fh_tool_uri_parse   (const char *uri, char **tool, char ***files);   /* free with g_free / g_strfreev */
/* A readable title for pages that are not real folders (Storage, tools, extension pages), else NULL. */
char             *fh_virtual_page_title (const char *uri);

typedef enum { FH_KIND_OTHER, FH_KIND_IMAGE, FH_KIND_VIDEO, FH_KIND_AUDIO, FH_KIND_MARKDOWN, FH_KIND_TEXT } FhFileKind;
/* What the built-in tools make of a file: MIME type first, the lower-cased path when the type is not known yet. */
FhFileKind        fh_file_kind        (const char *mime, const char *lower);
/* Text or source code, by MIME type and (when the type is not known yet) by name. `lower` is the lower-cased path. */
gboolean          fh_is_text_file     (const char *mime, const char *lower);

G_END_DECLS

#endif

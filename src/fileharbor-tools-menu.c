/* fileharbor-tools-menu.c - right-click items that open the built-in tools. New file, not in upstream Nemo.
 *
 * With files selected it offers only the tools that make sense for them (a picture: viewer, editor,
 * resize; a video: the video player; ...). With nothing selected it offers a Tools submenu. Items open
 * the tool in a new tab. */

#include <config.h>

#include "fileharbor-tools-menu.h"
#include "nemo-tool-view.h"

#include <glib/gi18n.h>
#include <libnemo-extension/nemo-extension-types.h>
#include <libnemo-extension/nemo-file-info.h>
#include <libnemo-extension/nemo-menu-provider.h>
#include <libnemo-private/nemo-module.h>
#include <libnemo-private/fileharbor-extensions.h>
#include <string.h>

typedef struct { GObject parent; } FhToolsMenu;
typedef struct { GObjectClass parent_class; } FhToolsMenuClass;

static void provider_iface_init (NemoMenuProviderInterface *iface);
static GType fh_tools_menu_get_type (void);
G_DEFINE_TYPE_WITH_CODE (FhToolsMenu, fh_tools_menu, G_TYPE_OBJECT,
			 G_IMPLEMENT_INTERFACE (NEMO_TYPE_MENU_PROVIDER, provider_iface_init))

static void fh_tools_menu_init (FhToolsMenu *self) { }
static void fh_tools_menu_class_init (FhToolsMenuClass *klass) { }

typedef struct {
	GtkWidget *window;
	char *tool;
	GList *paths;     /* char* */
} Launch;

static void
launch_free (gpointer data, GClosure *closure)
{
	Launch *l = data;

	g_free (l->tool);
	g_list_free_full (l->paths, g_free);
	g_free (l);
}

static void
on_activate (NemoMenuItem *item, gpointer data)
{
	Launch *l = data;

	nemo_open_tool_tab (l->window, l->tool, l->paths);
}

static NemoMenuItem *
make_item (GtkWidget *window, const char *tool, const char *label, GList *paths)
{
	const FhToolInfo *info = fh_tool_lookup (tool);
	char *name = g_strdup_printf ("FileHarbor::tool-%s", tool);
	NemoMenuItem *item = nemo_menu_item_new (name, label, info ? info->desc : "", info ? info->icon : NULL);
	Launch *l = g_new0 (Launch, 1);
	GList *p;

	l->window = window;
	l->tool = g_strdup (tool);
	for (p = paths; p != NULL; p = p->next)
		l->paths = g_list_append (l->paths, g_strdup (p->data));
	g_signal_connect_data (item, "activate", G_CALLBACK (on_activate), l, launch_free, 0);
	g_free (name);
	return item;
}

static GList *
get_file_items (NemoMenuProvider *provider, GtkWidget *window, GList *files)
{
	GList *items = NULL, *l, *paths = NULL;
	int n = 0, images = 0, videos = 0, audios = 0, markdowns = 0, texts = 0, dirs = 0;

	for (l = files; l != NULL; l = l->next) {
		NemoFileInfo *fi = l->data;
		char *uri = nemo_file_info_get_uri (fi), *mime = nemo_file_info_get_mime_type (fi), *path = NULL, *lower;
		GFile *f = g_file_new_for_uri (uri);

		path = g_file_get_path (f);
		g_object_unref (f);
		g_free (uri);
		if (path == NULL) {            /* remote or virtual: the tools work on local files */
			g_free (mime);
			continue;
		}
		n++;
		lower = g_ascii_strdown (path, -1);
		if (nemo_file_info_is_directory (fi))
			dirs++;
		else {
			switch (fh_file_kind (mime, lower)) {
			case FH_KIND_IMAGE:    images++;    break;
			case FH_KIND_VIDEO:    videos++;    break;
			case FH_KIND_AUDIO:    audios++;    break;
			case FH_KIND_MARKDOWN: markdowns++; break;
			case FH_KIND_TEXT:     texts++;     break;
			default: break;
			}
		}
		paths = g_list_append (paths, path);
		g_free (lower);
		g_free (mime);
	}
	if (n == 0) {
		g_list_free_full (paths, g_free);
		return NULL;
	}
	if (images == n) {
		if (n == 1) {
			items = g_list_append (items, make_item (window, "image", _("Open in Image Viewer"), paths));
			items = g_list_append (items, make_item (window, "resize", _("Resize Picture…"), paths));
		} else {
			items = g_list_append (items, make_item (window, "resize", _("Resize Pictures…"), paths));
			items = g_list_append (items, make_item (window, "image", _("Open in Image Viewer"), paths));
		}
	} else if (videos + audios == n && n > 0) {
		items = g_list_append (items, make_item (window, "media",
							 videos > 0 ? _("Open in Media Player") : _("Play in Media Player"), paths));
	} else if (markdowns == n && n == 1) {
		items = g_list_append (items, make_item (window, "preview", _("Preview Markdown"), paths));
		items = g_list_append (items, make_item (window, "editor", _("Edit in Text Editor"), paths));
	} else if (markdowns + texts == n && n == 1) {
		items = g_list_append (items, make_item (window, "editor", _("Open in Text Editor"), paths));
	} else if (images > 0 && images < n && images == n - dirs) {
		items = g_list_append (items, make_item (window, "resize", _("Resize Pictures…"), paths));
	}
	if (n == 1 && dirs == 0) {
		items = g_list_append (items, make_item (window, "devtools", _("File Hashes…"), paths));
		/* the hashes tool needs its file argument as --file=, which the tool page understands */
	}
	g_list_free_full (paths, g_free);
	return items;
}

static GList *
get_background_items (NemoMenuProvider *provider, GtkWidget *window, NemoFileInfo *current_folder)
{
	NemoMenuItem *tools = nemo_menu_item_new ("FileHarbor::tools", _("Tools"), _("Built-in tools"), "applications-utilities-symbolic");
	NemoMenu *menu = nemo_menu_new ();
	const FhToolInfo *all;
	int n, i;

	all = fh_tools_all (&n);
	for (i = 0; i < n; i++) {
		if (g_strcmp0 (all[i].id, "hub") == 0)
			break;                           /* everything after the hub is a legacy id */
		nemo_menu_append_item (menu, make_item (window, all[i].id, all[i].label, NULL));
	}
	nemo_menu_append_item (menu, nemo_menu_item_new_separator ("FileHarbor::tools-sep"));
	nemo_menu_append_item (menu, make_item (window, "hub", _("All tools"), NULL));
	nemo_menu_item_set_submenu (tools, menu);
	g_object_unref (menu);
	return g_list_append (NULL, tools);
}

static void
provider_iface_init (NemoMenuProviderInterface *iface)
{
	iface->get_file_items = get_file_items;
	iface->get_background_items = get_background_items;
}

void
fh_tools_menu_register (void)
{
	nemo_module_add_type (fh_tools_menu_get_type ());
}

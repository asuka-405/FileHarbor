/* nemo-tool-view.c - shows a built-in tool in a tab. New file, not in upstream Nemo. */

#include <config.h>

#include "nemo-tool-view.h"
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-file.h>

#include "nemo-view.h"
#include "nemo-view-factory.h"
#include "nemo-window.h"
#include "nemo-window-slot.h"
#include "fileharbor-python.h"
#include <libnemo-private/fileharbor-extensions.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>

struct NemoToolViewDetails {
	char *uri;
	gboolean started;
	GtkWidget *tool;
};

G_DEFINE_TYPE (NemoToolView, nemo_tool_view, NEMO_TYPE_VIEW)

gboolean
nemo_uri_is_tool (const char *uri)
{
	return uri != NULL && g_str_has_prefix (uri, FH_TOOL_URI_PREFIX);
}

void
nemo_open_tool_tab (GtkWidget *window, const char *tool, GList *paths)
{
	char *uri = fh_tool_uri (tool, paths);
	GFile *f = g_file_new_for_uri (uri);

	nemo_window_go_to_new_active_tab (NEMO_WINDOW (window), f);
	g_object_unref (f);
	g_free (uri);
}

gboolean
nemo_tool_view_try_activate (GtkWidget *window, GList *files)
{
	NemoFile *file;
	char *uri, *path, *lower, *mime;
	GFile *f;
	const char *tool = NULL;
	gboolean opened = FALSE, keep = FALSE;
	/* these have a better default: a browser, the launcher machinery */
	static const char *const keep_exts[] = { ".html", ".htm", ".xhtml", ".desktop", ".svg", NULL };
	int i;

	if (window == NULL || files == NULL || files->next != NULL)
		return FALSE;
	file = NEMO_FILE (files->data);
	if (nemo_file_is_directory (file))
		return FALSE;
	uri = nemo_file_get_uri (file);
	f = g_file_new_for_uri (uri);
	path = g_file_get_path (f);
	g_object_unref (f);
	g_free (uri);
	if (path == NULL)
		return FALSE;                    /* remote files: the tools work on local paths */
	lower = g_ascii_strdown (path, -1);
	mime = nemo_file_get_mime_type (file);
	for (i = 0; keep_exts[i] != NULL; i++)
		if (g_str_has_suffix (lower, keep_exts[i]))
			keep = TRUE;
	if (mime != NULL && g_str_has_prefix (mime, "text/html"))
		keep = TRUE;
	if (!keep && !nemo_file_is_executable (file)) {
		switch (fh_file_kind (mime, lower)) {
		case FH_KIND_MARKDOWN:
			if (g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_MARKDOWN_OPEN_IN_PREVIEW))
				tool = "preview";
			else if (g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_TEXT_OPEN_IN_EDITOR))
				tool = "editor";
			break;
		case FH_KIND_TEXT:
			if (g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_TEXT_OPEN_IN_EDITOR))
				tool = "editor";
			break;
		case FH_KIND_IMAGE:
			if (g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_MEDIA_OPEN_IN_BUILTIN))
				tool = "image";
			break;
		case FH_KIND_VIDEO:
		case FH_KIND_AUDIO:
			if (g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_MEDIA_OPEN_IN_BUILTIN))
				tool = "media";
			break;
		default:
			break;
		}
	}
	if (tool != NULL) {
		GList *paths = g_list_append (NULL, path);

		nemo_open_tool_tab (window, tool, paths);
		g_list_free (paths);
		opened = TRUE;
	}
	g_free (mime);
	g_free (lower);
	g_free (path);
	return opened;
}

static void
show_text (NemoToolView *self, const char *text, gboolean error)
{
	GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
	GtkWidget *l = gtk_label_new (text);
	GList *kids = gtk_container_get_children (GTK_CONTAINER (self)), *k;

	gtk_container_set_border_width (GTK_CONTAINER (box), 24);
	gtk_label_set_line_wrap (GTK_LABEL (l), TRUE);
	gtk_label_set_xalign (GTK_LABEL (l), 0.0);
	gtk_label_set_selectable (GTK_LABEL (l), TRUE);
	if (error)
		gtk_style_context_add_class (gtk_widget_get_style_context (l), "error");
	gtk_box_pack_start (GTK_BOX (box), l, FALSE, FALSE, 0);
	for (k = kids; k != NULL; k = k->next)
		gtk_widget_destroy (k->data);
	g_list_free (kids);
	gtk_container_add (GTK_CONTAINER (self), box);
	gtk_widget_show_all (box);
}

/* The slot gives the view its location a moment after it exists, so start the tool when it is known. */
static gboolean
start_tick (gpointer data)
{
	NemoToolView *self = data;
	int tries = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (self), "fh-tries"));
	NemoFile *file;
	char *uri, *tool = NULL, **files = NULL;
	GError *error = NULL;
	GtkWidget *w;
	GList *kids, *k;

	if (!NEMO_IS_TOOL_VIEW (self) || self->details->started) {
		g_object_unref (data);
		return G_SOURCE_REMOVE;
	}
	file = nemo_view_get_directory_as_file (NEMO_VIEW (self));
	if (file == NULL) {
		if (tries > 100) {
			g_object_unref (data);
			return G_SOURCE_REMOVE;
		}
		g_object_set_data (G_OBJECT (self), "fh-tries", GINT_TO_POINTER (tries + 1));
		return G_SOURCE_CONTINUE;
	}
	self->details->started = TRUE;
	uri = nemo_file_get_uri (file);
	if (!fh_tool_uri_parse (uri, &tool, &files)) {
		g_free (uri);
		g_object_unref (data);
		return G_SOURCE_REMOVE;
	}
	g_free (self->details->uri);
	self->details->uri = uri;
	w = fh_python_create_tool (tool, files, &error);
	if (w == NULL) {
		char *m = g_strdup_printf (_("Could not open this tool.\n\n%s\n\nThe tools need Python 3 with PyGObject, Pillow and python-markdown installed."),
					   error->message);

		show_text (self, m, TRUE);
		g_free (m);
		g_clear_error (&error);
	} else {
		kids = gtk_container_get_children (GTK_CONTAINER (self));
		for (k = kids; k != NULL; k = k->next)
			gtk_widget_destroy (k->data);
		g_list_free (kids);
		gtk_container_add (GTK_CONTAINER (self), w);
		gtk_widget_show_all (w);
		self->details->tool = w;
	}
	g_free (tool);
	g_strfreev (files);
	g_object_unref (data);
	return G_SOURCE_REMOVE;
}

/* ---- NemoView boilerplate: this view never lists files ---------------- */

static void   sv_add_file (NemoView *v, NemoFile *f, NemoDirectory *d) { }
static void   sv_nop (NemoView *v) { }
static void   sv_file_changed (NemoView *v, NemoFile *f, NemoDirectory *d) { }
static GList *sv_get_selection (NemoView *v) { return NULL; }
static guint  sv_item_count (NemoView *v) { return 0; }
static gboolean sv_is_empty (NemoView *v) { return TRUE; }
static void   sv_remove_file (NemoView *v, NemoFile *f, NemoDirectory *d) { }
static void   sv_set_selection (NemoView *v, GList *s) { nemo_view_notify_selection_changed (v); }
static void   sv_merge_menus (NemoView *v) { NEMO_VIEW_CLASS (nemo_tool_view_parent_class)->merge_menus (v); }
static void   sv_update_menus (NemoView *v) { NEMO_VIEW_CLASS (nemo_tool_view_parent_class)->update_menus (v); }
static void   sv_bump_zoom (NemoView *v, int inc) { }
static NemoZoomLevel sv_get_zoom (NemoView *v) { return NEMO_ZOOM_LEVEL_STANDARD; }
static void   sv_zoom_to (NemoView *v, NemoZoomLevel l) { }
static gboolean sv_can_zoom (NemoView *v) { return FALSE; }
static void   sv_start_renaming (NemoView *v, NemoFile *f, gboolean a) { }
static void   sv_click_policy (NemoView *v) { }
static int    sv_compare_files (NemoView *v, NemoFile *a, NemoFile *b) { return a < b ? -1 : (a > b); }
static gboolean sv_manual_layout (NemoView *v) { return FALSE; }
static void   sv_end_loading (NemoView *v, gboolean all) { }
static char  *sv_first_visible (NemoView *v) { return NULL; }
static void   sv_scroll_to (NemoView *v, const char *uri) { }
static void   sv_sort_dirs_first (NemoView *v) { }
static void   sv_load_error (NemoView *v, GError *e) { }
static const char *sv_get_id (NemoView *v) { return NEMO_TOOL_VIEW_ID; }

static void
nemo_tool_view_finalize (GObject *object)
{
	g_free (NEMO_TOOL_VIEW (object)->details->uri);
	G_OBJECT_CLASS (nemo_tool_view_parent_class)->finalize (object);
}

static void
nemo_tool_view_class_init (NemoToolViewClass *klass)
{
	GObjectClass *oc = G_OBJECT_CLASS (klass);
	NemoViewClass *vc = NEMO_VIEW_CLASS (klass);

	g_type_class_add_private (klass, sizeof (NemoToolViewDetails));
	oc->finalize = nemo_tool_view_finalize;

	vc->add_file = sv_add_file;
	vc->begin_loading = sv_nop;
	vc->bump_zoom_level = sv_bump_zoom;
	vc->can_zoom_in = sv_can_zoom;
	vc->can_zoom_out = sv_can_zoom;
	vc->click_policy_changed = sv_click_policy;
	vc->clear = sv_nop;
	vc->file_changed = sv_file_changed;
	vc->get_selection = sv_get_selection;
	vc->get_selection_for_file_transfer = sv_get_selection;
	vc->get_item_count = sv_item_count;
	vc->is_empty = sv_is_empty;
	vc->remove_file = sv_remove_file;
	vc->merge_menus = sv_merge_menus;
	vc->update_menus = sv_update_menus;
	vc->reset_to_defaults = sv_nop;
	vc->restore_default_zoom_level = sv_nop;
	vc->reveal_selection = sv_nop;
	vc->select_all = sv_nop;
	vc->set_selection = sv_set_selection;
	vc->compare_files = sv_compare_files;
	vc->sort_directories_first_changed = sv_sort_dirs_first;
	vc->start_renaming_file = sv_start_renaming;
	vc->get_zoom_level = sv_get_zoom;
	vc->zoom_to_level = sv_zoom_to;
	vc->end_file_changes = sv_nop;
	vc->using_manual_layout = sv_manual_layout;
	vc->end_loading = sv_end_loading;
	vc->get_view_id = sv_get_id;
	vc->get_first_visible_file = sv_first_visible;
	vc->scroll_to_file = sv_scroll_to;
	vc->load_error = sv_load_error;
}

static void
nemo_tool_view_init (NemoToolView *self)
{
	self->details = G_TYPE_INSTANCE_GET_PRIVATE (self, NEMO_TYPE_TOOL_VIEW, NemoToolViewDetails);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (self), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	show_text (self, _("Loading…"), FALSE);
	gtk_widget_show_all (GTK_WIDGET (self));
	g_timeout_add (30, start_tick, g_object_ref (self));
}

static NemoView *
nemo_tool_view_create (NemoWindowSlot *slot)
{
	return NEMO_VIEW (g_object_new (NEMO_TYPE_TOOL_VIEW, "window-slot", slot, NULL));
}

static gboolean
nemo_tool_view_supports_uri (const char *uri, GFileType file_type, const char *mime_type)
{
	return nemo_uri_is_tool (uri);
}

static NemoViewInfo nemo_tool_view = {
	NEMO_TOOL_VIEW_ID,
	"Tool",
	"Tool",
	"_Tool",
	"The tool encountered an error.",
	"Display this location as a tool.",
	nemo_tool_view_create,
	nemo_tool_view_supports_uri
};

void
nemo_tool_view_register (void)
{
	nemo_view_factory_register (&nemo_tool_view);
}

/* nemo-ext-page-view.c - pages provided by FileHarbor extensions. New file, not in upstream Nemo. */

#include <config.h>

#include "nemo-ext-page-view.h"

#include "nemo-view.h"
#include "nemo-view-factory.h"
#include "nemo-window-slot.h"
#include <libnemo-private/fileharbor-extensions.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <json-glib/json-glib.h>
#include <string.h>

#define MAX_OUTPUT (4 * 1024 * 1024)

struct NemoExtPageViewDetails {
	char *uri;
	FhPage *page;
	FhExtension *ext;
	GtkWidget *root, *title, *content, *spinner;
	GCancellable *cancellable;
	guint refresh_id;
};

G_DEFINE_TYPE (NemoExtPageView, nemo_ext_page_view, NEMO_TYPE_VIEW)

gboolean
nemo_uri_is_ext_page (const char *uri)
{
	return uri != NULL && g_str_has_prefix (uri, FH_PAGE_URI_PREFIX);
}

static void load_page (NemoExtPageView *self);
static void sv_begin_loading (NemoView *view);

/* ---- actions from the page ---------------------------------------- */

static void
open_target (NemoExtPageView *self, const char *target)
{
	NemoWindowSlot *slot = nemo_view_get_nemo_window_slot (NEMO_VIEW (self));
	GFile *f;

	if (slot == NULL || target == NULL)
		return;
	f = strstr (target, "://") != NULL || g_str_has_prefix (target, "trash:") ? g_file_new_for_uri (target)
										   : g_file_new_for_path (target);
	nemo_window_slot_open_location (slot, f, 0);
	g_object_unref (f);
}

typedef struct {
	NemoExtPageView *self;
	JsonObject *action;     /* owned */
} ActionCtx;

static void
action_done (GObject *src, GAsyncResult *res, gpointer data)
{
	ActionCtx *c = data;
	GSubprocess *p = G_SUBPROCESS (src);

	g_subprocess_wait_finish (p, res, NULL);
	if (NEMO_IS_EXT_PAGE_VIEW (c->self) && json_object_has_member (c->action, "refresh") &&
	    json_object_get_boolean_member (c->action, "refresh"))
		load_page (c->self);
	json_object_unref (c->action);
	g_object_unref (c->self);
	g_free (c);
	g_object_unref (p);
}

static void
run_action (NemoExtPageView *self, JsonObject *a)
{
	if (json_object_has_member (a, "open")) {
		open_target (self, json_object_get_string_member (a, "open"));
	} else if (json_object_has_member (a, "exec") && JSON_NODE_HOLDS_ARRAY (json_object_get_member (a, "exec"))) {
		JsonArray *arr = json_object_get_array_member (a, "exec");
		guint n = json_array_get_length (arr), i;
		char **argv = g_new0 (char *, n + 1);
		GSubprocess *p;
		GError *error = NULL;

		for (i = 0; i < n; i++) {
			const char *s = json_array_get_string_element (arr, i);

			if (s != NULL && g_str_has_prefix (s, "$EXT/"))
				argv[i] = g_build_filename (self->details->ext->dir, s + 5, NULL);
			else
				argv[i] = g_strdup (s != NULL ? s : "");
		}
		p = g_subprocess_newv ((const char * const *) argv, G_SUBPROCESS_FLAGS_NONE, &error);
		if (p != NULL) {
			ActionCtx *c = g_new0 (ActionCtx, 1);

			c->self = g_object_ref (self);
			c->action = json_object_ref (a);
			g_subprocess_wait_async (p, NULL, action_done, c);
		} else {
			g_warning ("Extension action failed: %s", error->message);
			g_clear_error (&error);
		}
		g_strfreev (argv);
	}
}

static void
on_button_clicked (GtkWidget *w, gpointer data)
{
	JsonObject *a = g_object_get_data (G_OBJECT (w), "action");

	if (a != NULL)
		run_action (data, a);
}

static void
on_list_activated (GtkListBox *box, GtkListBoxRow *row, gpointer data)
{
	JsonObject *a = g_object_get_data (G_OBJECT (row), "action");

	if (a != NULL)
		run_action (data, a);
}

/* ---- rendering ------------------------------------------------------ */

static const char *
str (JsonObject *o, const char *k)
{
	return o != NULL && json_object_has_member (o, k) && JSON_NODE_HOLDS_VALUE (json_object_get_member (o, k))
		? json_object_get_string_member (o, k) : NULL;
}

static void
bind_action (GtkWidget *w, JsonObject *o)
{
	if (json_object_has_member (o, "open") || json_object_has_member (o, "exec"))
		g_object_set_data_full (G_OBJECT (w), "action", json_object_ref (o), (GDestroyNotify) json_object_unref);
}

static GtkWidget *
make_label (const char *text, const char *markup_fmt, double xalign)
{
	GtkWidget *l = gtk_label_new (NULL);

	if (markup_fmt != NULL) {
		char *m = g_markup_printf_escaped (markup_fmt, text);

		gtk_label_set_markup (GTK_LABEL (l), m);
		g_free (m);
	} else {
		gtk_label_set_text (GTK_LABEL (l), text);
	}
	gtk_label_set_xalign (GTK_LABEL (l), xalign);
	gtk_label_set_line_wrap (GTK_LABEL (l), TRUE);
	gtk_label_set_selectable (GTK_LABEL (l), FALSE);
	return l;
}

static void
render_block (NemoExtPageView *self, GtkWidget *box, JsonObject *b)
{
	const char *type = str (b, "type");
	GtkWidget *w = NULL;

	if (type == NULL)
		return;
	if (g_strcmp0 (type, "heading") == 0) {
		const char *fmt = json_object_has_member (b, "level") && json_object_get_int_member (b, "level") >= 2
			? "<span size='large' weight='bold'>%s</span>" : "<span size='x-large' weight='bold'>%s</span>";

		w = make_label (str (b, "text") ? str (b, "text") : "", fmt, 0.0);
	} else if (g_strcmp0 (type, "text") == 0) {
		w = make_label (str (b, "text") ? str (b, "text") : "", NULL, 0.0);
		if (json_object_has_member (b, "dim") && json_object_get_boolean_member (b, "dim"))
			gtk_style_context_add_class (gtk_widget_get_style_context (w), "dim-label");
	} else if (g_strcmp0 (type, "separator") == 0) {
		w = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
	} else if (g_strcmp0 (type, "progress") == 0) {
		w = gtk_progress_bar_new ();
		gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (w), CLAMP (json_object_has_member (b, "value") ? json_object_get_double_member (b, "value") : 0, 0, 1));
		if (str (b, "label") != NULL) {
			gtk_progress_bar_set_show_text (GTK_PROGRESS_BAR (w), TRUE);
			gtk_progress_bar_set_text (GTK_PROGRESS_BAR (w), str (b, "label"));
		}
	} else if (g_strcmp0 (type, "image") == 0 && str (b, "path") != NULL) {
		GdkPixbuf *pb = gdk_pixbuf_new_from_file_at_scale (str (b, "path"), json_object_has_member (b, "width") ? (int) json_object_get_int_member (b, "width") : 480, -1, TRUE, NULL);

		if (pb != NULL) {
			w = gtk_image_new_from_pixbuf (pb);
			gtk_widget_set_halign (w, GTK_ALIGN_START);
			g_object_unref (pb);
		}
	} else if (g_strcmp0 (type, "button") == 0) {
		w = gtk_button_new_with_label (str (b, "label") ? str (b, "label") : _("Run"));
		gtk_widget_set_halign (w, GTK_ALIGN_START);
		bind_action (w, b);
		g_signal_connect (w, "clicked", G_CALLBACK (on_button_clicked), self);
	} else if (g_strcmp0 (type, "list") == 0 && json_object_has_member (b, "items")) {
		JsonArray *items = json_object_get_array_member (b, "items");
		GtkWidget *lb = gtk_list_box_new ();
		guint i;

		gtk_list_box_set_selection_mode (GTK_LIST_BOX (lb), GTK_SELECTION_NONE);
		gtk_list_box_set_activate_on_single_click (GTK_LIST_BOX (lb), TRUE);
		for (i = 0; i < json_array_get_length (items) && i < 2000; i++) {
			JsonObject *it = json_array_get_object_element (items, i);
			GtkWidget *row, *h, *v;

			if (it == NULL)
				continue;
			row = gtk_list_box_row_new ();
			h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
			gtk_container_set_border_width (GTK_CONTAINER (h), 8);
			if (str (it, "icon") != NULL)
				gtk_box_pack_start (GTK_BOX (h), gtk_image_new_from_icon_name (str (it, "icon"), GTK_ICON_SIZE_LARGE_TOOLBAR), FALSE, FALSE, 0);
			v = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
			gtk_box_pack_start (GTK_BOX (v), make_label (str (it, "title") ? str (it, "title") : "", NULL, 0.0), FALSE, FALSE, 0);
			if (str (it, "subtitle") != NULL) {
				GtkWidget *sub = make_label (str (it, "subtitle"), NULL, 0.0);

				gtk_style_context_add_class (gtk_widget_get_style_context (sub), "dim-label");
				gtk_box_pack_start (GTK_BOX (v), sub, FALSE, FALSE, 0);
			}
			gtk_box_pack_start (GTK_BOX (h), v, TRUE, TRUE, 0);
			if (str (it, "trailing") != NULL)
				gtk_box_pack_end (GTK_BOX (h), make_label (str (it, "trailing"), NULL, 1.0), FALSE, FALSE, 0);
			gtk_container_add (GTK_CONTAINER (row), h);
			bind_action (row, it);
			gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), json_object_has_member (it, "open") || json_object_has_member (it, "exec"));
			gtk_container_add (GTK_CONTAINER (lb), row);
		}
		g_signal_connect (lb, "row-activated", G_CALLBACK (on_list_activated), self);
		w = lb;
	} else if (g_strcmp0 (type, "table") == 0 && json_object_has_member (b, "columns") && json_object_has_member (b, "rows")) {
		JsonArray *cols = json_object_get_array_member (b, "columns"), *rows = json_object_get_array_member (b, "rows");
		GtkWidget *grid = gtk_grid_new ();
		guint c, r;

		gtk_grid_set_column_spacing (GTK_GRID (grid), 24);
		gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
		for (c = 0; c < json_array_get_length (cols); c++) {
			const char *t = json_array_get_string_element (cols, c);

			gtk_grid_attach (GTK_GRID (grid), make_label (t ? t : "", "<b>%s</b>", 0.0), c, 0, 1, 1);
		}
		for (r = 0; r < json_array_get_length (rows) && r < 5000; r++) {
			JsonArray *row = json_array_get_array_element (rows, r);

			for (c = 0; row != NULL && c < json_array_get_length (row); c++) {
				JsonNode *n = json_array_get_element (row, c);
				char *s = JSON_NODE_HOLDS_VALUE (n) ? g_strdup (json_node_get_string (n) ? json_node_get_string (n) : "") : g_strdup ("");
				GtkWidget *l;

				if (JSON_NODE_HOLDS_VALUE (n) && json_node_get_string (n) == NULL) {
					g_free (s);
					s = json_to_string (n, FALSE);
				}
				l = make_label (s, NULL, 0.0);
				gtk_label_set_selectable (GTK_LABEL (l), TRUE);
				gtk_grid_attach (GTK_GRID (grid), l, c, r + 1, 1, 1);
				g_free (s);
			}
		}
		w = grid;
	}
	if (w != NULL)
		gtk_box_pack_start (GTK_BOX (box), w, FALSE, FALSE, 0);
}

static void
show_message (NemoExtPageView *self, const char *text, gboolean error)
{
	NemoExtPageViewDetails *d = self->details;
	GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
	GtkWidget *l = make_label (text, NULL, 0.0);

	if (error)
		gtk_style_context_add_class (gtk_widget_get_style_context (l), "error");
	gtk_box_pack_start (GTK_BOX (box), l, FALSE, FALSE, 0);
	if (gtk_bin_get_child (GTK_BIN (d->content)) != NULL)
		gtk_widget_destroy (gtk_bin_get_child (GTK_BIN (d->content)));
	gtk_container_add (GTK_CONTAINER (d->content), box);
	gtk_widget_show_all (box);
}

static void
render_json (NemoExtPageView *self, const char *data, gsize len)
{
	NemoExtPageViewDetails *d = self->details;
	JsonParser *parser = json_parser_new ();
	GError *error = NULL;

	if (!json_parser_load_from_data (parser, data, len, &error)) {
		char *m = g_strdup_printf (_("The page did not print valid JSON: %s"), error->message);

		show_message (self, m, TRUE);
		g_free (m);
		g_clear_error (&error);
	} else if (!JSON_NODE_HOLDS_OBJECT (json_parser_get_root (parser))) {
		show_message (self, _("The page must print a JSON object with a \"blocks\" list."), TRUE);
	} else {
		JsonObject *o = json_node_get_object (json_parser_get_root (parser));
		GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);
		JsonArray *blocks = json_object_has_member (o, "blocks") ? json_object_get_array_member (o, "blocks") : NULL;
		guint i;

		if (str (o, "title") != NULL)
			gtk_label_set_markup (GTK_LABEL (d->title), g_markup_printf_escaped ("<span size='x-large' weight='bold'>%s</span>", str (o, "title")));
		for (i = 0; blocks != NULL && i < json_array_get_length (blocks); i++) {
			JsonObject *b = json_array_get_object_element (blocks, i);

			if (b != NULL)
				render_block (self, box, b);
		}
		if (gtk_bin_get_child (GTK_BIN (d->content)) != NULL)
			gtk_widget_destroy (gtk_bin_get_child (GTK_BIN (d->content)));
		gtk_container_add (GTK_CONTAINER (d->content), box);
		gtk_widget_show_all (box);
	}
	g_object_unref (parser);
}

/* ---- running the page command --------------------------------------- */

static void
page_output_ready (GObject *src, GAsyncResult *res, gpointer data)
{
	NemoExtPageView *self = data;
	GBytes *out = NULL, *err = NULL;
	GError *error = NULL;
	gboolean ok = g_subprocess_communicate_finish (G_SUBPROCESS (src), res, &out, &err, &error);

	if (!NEMO_IS_EXT_PAGE_VIEW (self) || (error && g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))) {
		g_clear_error (&error);
		goto out;
	}
	gtk_spinner_stop (GTK_SPINNER (self->details->spinner));
	if (!ok) {
		char *m = g_strdup_printf (_("Could not run the page: %s"), error->message);

		show_message (self, m, TRUE);
		g_free (m);
		g_clear_error (&error);
	} else if (!g_subprocess_get_successful (G_SUBPROCESS (src))) {
		gsize el = 0;
		const char *e = err != NULL ? g_bytes_get_data (err, &el) : NULL;
		char *m = g_strdup_printf (_("The page command failed.\n\n%.*s"), (int) MIN (el, 600), e != NULL ? e : "");

		show_message (self, m, TRUE);
		g_free (m);
	} else {
		gsize len = 0;
		const char *d = g_bytes_get_data (out, &len);

		if (len > MAX_OUTPUT)
			show_message (self, _("The page produced too much output."), TRUE);
		else
			render_json (self, d, len);
	}
out:
	if (out) g_bytes_unref (out);
	if (err) g_bytes_unref (err);
	g_object_unref (self);
}

static void
load_page (NemoExtPageView *self)
{
	NemoExtPageViewDetails *d = self->details;
	GSubprocessLauncher *l;
	GSubprocess *p;
	GError *error = NULL;

	if (d->page == NULL)
		return;
	if (d->cancellable != NULL) {
		g_cancellable_cancel (d->cancellable);
		g_object_unref (d->cancellable);
	}
	d->cancellable = g_cancellable_new ();
	l = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE);
	g_subprocess_launcher_set_cwd (l, d->ext->dir);
	g_subprocess_launcher_setenv (l, "FILEHARBOR_EXTENSION_DIR", d->ext->dir, TRUE);
	g_subprocess_launcher_setenv (l, "FILEHARBOR_PAGE", d->page->id, TRUE);
	p = g_subprocess_launcher_spawnv (l, (const char * const *) d->page->argv, &error);
	g_object_unref (l);
	if (p == NULL) {
		char *m = g_strdup_printf (_("Could not start the page: %s"), error->message);

		show_message (self, m, TRUE);
		g_free (m);
		g_clear_error (&error);
		return;
	}
	gtk_spinner_start (GTK_SPINNER (d->spinner));
	g_subprocess_communicate_async (p, NULL, d->cancellable, page_output_ready, g_object_ref (self));
	g_object_unref (p);
}

static gboolean
refresh_timeout (gpointer data)
{
	load_page (data);
	return G_SOURCE_CONTINUE;
}

/* The slot sets the location after construction; pick the page from the current URI then. */
static void
sv_begin_loading (NemoView *view)
{
	NemoExtPageView *self = NEMO_EXT_PAGE_VIEW (view);
	NemoExtPageViewDetails *d = self->details;
	NemoFile *file = nemo_view_get_directory_as_file (view);
	char *uri = file != NULL ? nemo_file_get_uri (file) : NULL;

	if (uri == NULL || g_strcmp0 (uri, d->uri) == 0) {
		g_free (uri);
		return;
	}
	g_free (d->uri);
	d->uri = uri;
	d->page = fh_extensions_find_page (uri, &d->ext);
	if (d->page == NULL) {
		show_message (self, _("This page is not available. Its extension may have been removed or turned off."), TRUE);
		return;
	}
	gtk_label_set_markup (GTK_LABEL (d->title), g_markup_printf_escaped ("<span size='x-large' weight='bold'>%s</span>", d->page->title));
	if (d->refresh_id != 0) {
		g_source_remove (d->refresh_id);
		d->refresh_id = 0;
	}
	if (d->page->refresh >= 2)
		d->refresh_id = g_timeout_add_seconds (d->page->refresh, refresh_timeout, self);
	load_page (self);
}

static void on_refresh_clicked (GtkButton *b, gpointer data) { load_page (data); }

/* The location is known a moment after the view exists; for a page with no real folder behind it
 * the view's own "begin loading" may never fire, so also look for it ourselves. */
static gboolean
find_location_tick (gpointer data)
{
	NemoExtPageView *self = data;
	static const int max_tries = 60;
	int tries = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (self), "fh-tries"));

	if (!NEMO_IS_EXT_PAGE_VIEW (self))
		return G_SOURCE_REMOVE;
	if (self->details->page != NULL || tries > max_tries) {
		g_object_unref (self);
		return G_SOURCE_REMOVE;
	}
	g_object_set_data (G_OBJECT (self), "fh-tries", GINT_TO_POINTER (tries + 1));
	if (nemo_view_get_directory_as_file (NEMO_VIEW (self)) != NULL)
		sv_begin_loading (NEMO_VIEW (self));
	if (self->details->page == NULL)
		return G_SOURCE_CONTINUE;
	g_object_unref (self);
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
static void   sv_merge_menus (NemoView *v) { NEMO_VIEW_CLASS (nemo_ext_page_view_parent_class)->merge_menus (v); }
static void   sv_update_menus (NemoView *v) { NEMO_VIEW_CLASS (nemo_ext_page_view_parent_class)->update_menus (v); }
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
static const char *sv_get_id (NemoView *v) { return NEMO_EXT_PAGE_VIEW_ID; }

static void
nemo_ext_page_view_dispose (GObject *object)
{
	NemoExtPageViewDetails *d = NEMO_EXT_PAGE_VIEW (object)->details;

	if (d->cancellable != NULL) {
		g_cancellable_cancel (d->cancellable);
		g_clear_object (&d->cancellable);
	}
	if (d->refresh_id != 0) {
		g_source_remove (d->refresh_id);
		d->refresh_id = 0;
	}
	G_OBJECT_CLASS (nemo_ext_page_view_parent_class)->dispose (object);
}

static void
nemo_ext_page_view_finalize (GObject *object)
{
	g_free (NEMO_EXT_PAGE_VIEW (object)->details->uri);
	G_OBJECT_CLASS (nemo_ext_page_view_parent_class)->finalize (object);
}

static void
nemo_ext_page_view_class_init (NemoExtPageViewClass *klass)
{
	GObjectClass *oc = G_OBJECT_CLASS (klass);
	NemoViewClass *vc = NEMO_VIEW_CLASS (klass);

	g_type_class_add_private (klass, sizeof (NemoExtPageViewDetails));
	oc->dispose = nemo_ext_page_view_dispose;
	oc->finalize = nemo_ext_page_view_finalize;

	vc->add_file = sv_add_file;
	vc->begin_loading = sv_begin_loading;
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
nemo_ext_page_view_init (NemoExtPageView *self)
{
	NemoExtPageViewDetails *d;
	GtkWidget *head, *refresh;

	self->details = G_TYPE_INSTANCE_GET_PRIVATE (self, NEMO_TYPE_EXT_PAGE_VIEW, NemoExtPageViewDetails);
	d = self->details;
	d->root = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
	gtk_container_set_border_width (GTK_CONTAINER (d->root), 16);
	head = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
	d->title = gtk_label_new ("");
	gtk_label_set_xalign (GTK_LABEL (d->title), 0.0);
	d->spinner = gtk_spinner_new ();
	refresh = gtk_button_new_from_icon_name ("view-refresh-symbolic", GTK_ICON_SIZE_BUTTON);
	gtk_widget_set_tooltip_text (refresh, _("Refresh"));
	g_signal_connect (refresh, "clicked", G_CALLBACK (on_refresh_clicked), self);
	gtk_box_pack_start (GTK_BOX (head), d->title, TRUE, TRUE, 0);
	gtk_box_pack_end (GTK_BOX (head), refresh, FALSE, FALSE, 0);
	gtk_box_pack_end (GTK_BOX (head), d->spinner, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (d->root), head, FALSE, FALSE, 0);
	d->content = gtk_viewport_new (NULL, NULL);
	gtk_viewport_set_shadow_type (GTK_VIEWPORT (d->content), GTK_SHADOW_NONE);
	gtk_box_pack_start (GTK_BOX (d->root), d->content, TRUE, TRUE, 0);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (self), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_container_add (GTK_CONTAINER (self), d->root);
	gtk_widget_show_all (GTK_WIDGET (self));
	g_timeout_add (50, find_location_tick, g_object_ref (self));
}

static NemoView *
nemo_ext_page_view_create (NemoWindowSlot *slot)
{
	return NEMO_VIEW (g_object_new (NEMO_TYPE_EXT_PAGE_VIEW, "window-slot", slot, NULL));
}

static gboolean
nemo_ext_page_view_supports_uri (const char *uri, GFileType file_type, const char *mime_type)
{
	return nemo_uri_is_ext_page (uri);
}

static NemoViewInfo nemo_ext_page_view = {
	NEMO_EXT_PAGE_VIEW_ID,
	"Page",
	"Page",
	"_Page",
	"The page encountered an error.",
	"Display this location as an extension page.",
	nemo_ext_page_view_create,
	nemo_ext_page_view_supports_uri
};

void
nemo_ext_page_view_register (void)
{
	nemo_view_factory_register (&nemo_ext_page_view);
}

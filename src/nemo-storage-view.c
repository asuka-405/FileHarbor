/* nemo-storage-view.c - "Storage Sense" page for the FileHarbor fork of Nemo.
 *
 * New file, not present in upstream Nemo. It is a NemoView that shows no
 * files. Instead it builds a small dashboard from JSON served by
 * fileharbor-storage-daemon over D-Bus (see fileharbor/storage-sense/).
 */

#include <config.h>

#include "nemo-storage-view.h"

#include "nemo-view.h"
#include "nemo-view-factory.h"
#include "nemo-window-slot.h"

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <json-glib/json-glib.h>
#include <math.h>
#include <string.h>

#include <libnemo-private/nemo-file-operations.h>
#include <libnemo-private/nemo-file.h>
#include "nemo-properties-window.h"

#define SS_BUS_NAME "in.archdev.FileHarbor.StorageSense"
#define SS_OBJ_PATH "/in/archdev/FileHarbor/StorageSense"
#define SS_IFACE    "in.archdev.FileHarbor.StorageSense"

enum { PAGE_OVERVIEW, PAGE_FOLDERS, PAGE_LARGE, PAGE_CLEANUP, PAGE_GROWTH };

/* generic list columns shared by the tabs */
enum {
	COL_NAME,      /* markup-free primary text */
	COL_SUB,       /* secondary text (path / category) */
	COL_PERCENT,   /* 0..100 bar value */
	COL_BARTEXT,   /* text drawn on the bar (size) */
	COL_EXTRA,     /* extra text (items, age, kind) */
	COL_PATH,      /* absolute path, key for actions */
	COL_KIND,      /* cleanup kind */
	COL_CHECK,     /* cleanup checkbox */
	COL_SIZE,      /* raw size, bytes */
	N_COLS
};

struct NemoStorageViewDetails {
	GDBusProxy *proxy;
	GCancellable *cancellable;
	guint refresh_id;
	guint poll_id;
	gboolean scanning;

	GtkWidget *root_box;
	GtkWidget *subtitle;
	GtkWidget *usage_bar;
	GtkWidget *rescan_button;
	GtkWidget *spinner;
	GtkWidget *stack;
	GtkWidget *message;

	GtkListStore *cat_store;
	GtkListStore *top_store;
	GtkListStore *folder_store;
	GtkListStore *large_store;
	GtkTreeStore *cleanup_store;
	GtkListStore *growth_store;

	GtkWidget *timeline;
	GArray *timeline_pts;     /* gint64 pairs: ts, used */

	GtkWidget *folder_path_label;
	GtkWidget *folder_up_button;
	char *folder_path;
	char *folder_parent;
	char *root_path;

	GtkWidget *cleanup_total;
	int growth_hours;

	/* treemap */
	GtkWidget *map_area;
	GtkWidget *map_up_button;
	GtkWidget *map_path_label;
	struct TreeNode *map_root;
	char *map_path;
	char *map_parent;
	double map_w, map_h;
	struct TreeNode *map_hover;
};

static void nemo_storage_view_refresh_all (NemoStorageView *self);
static void nemo_storage_view_load_folder (NemoStorageView *self, const char *path);
static void load_map (NemoStorageView *self, const char *path);

G_DEFINE_TYPE (NemoStorageView, nemo_storage_view, NEMO_TYPE_VIEW)

/* ------------------------------------------------------------------ */
/* helpers                                                            */
/* ------------------------------------------------------------------ */

static char *
format_size (gint64 n)
{
	return g_format_size (n < 0 ? -n : n);
}

static char *
human_ago (gint64 ts)
{
	gint64 secs = g_get_real_time () / G_USEC_PER_SEC - ts;

	if (ts <= 0)
		return g_strdup (_("never"));
	if (secs < 60)
		return g_strdup (_("just now"));
	if (secs < 3600)
		return g_strdup_printf (_("%d min ago"), (int) (secs / 60));
	if (secs < 86400)
		return g_strdup_printf (_("%d h ago"), (int) (secs / 3600));
	return g_strdup_printf (_("%d days ago"), (int) (secs / 86400));
}

static gint64
jint (JsonObject *o, const char *k)
{
	return json_object_has_member (o, k) ? json_object_get_int_member (o, k) : 0;
}

static const char *
jstr (JsonObject *o, const char *k)
{
	return json_object_has_member (o, k) ? json_object_get_string_member (o, k) : "";
}

typedef void (*JsonReply) (NemoStorageView *self, JsonNode *root, gpointer data);

typedef struct {
	NemoStorageView *view;
	JsonReply cb;
	gpointer data;
} CallCtx;

static void
call_done (GObject *src, GAsyncResult *res, gpointer user_data)
{
	CallCtx *ctx = user_data;
	GError *error = NULL;
	GVariant *ret = g_dbus_proxy_call_finish (G_DBUS_PROXY (src), res, &error);

	if (ret == NULL) {
		g_clear_error (&error);   /* cancelled or daemon gone: ignore */
	} else {
		const char *json = NULL;

		g_variant_get (ret, "(&s)", &json);
		if (ctx->cb != NULL && json != NULL) {
			JsonParser *parser = json_parser_new ();

			if (json_parser_load_from_data (parser, json, -1, NULL))
				ctx->cb (ctx->view, json_parser_get_root (parser), ctx->data);
			g_object_unref (parser);
		}
		g_variant_unref (ret);
	}
	g_object_unref (ctx->view);
	g_free (ctx);
}

static void
call_json (NemoStorageView *self, const char *method, GVariant *params,
	   JsonReply cb, gpointer data)
{
	CallCtx *ctx;

	if (self->details->proxy == NULL) {
		if (params != NULL)
			g_variant_unref (g_variant_ref_sink (params));
		return;
	}
	ctx = g_new0 (CallCtx, 1);
	ctx->view = g_object_ref (self);
	ctx->cb = cb;
	ctx->data = data;
	g_dbus_proxy_call (self->details->proxy, method, params,
			   G_DBUS_CALL_FLAGS_NONE, 20000,
			   self->details->cancellable, call_done, ctx);
}

static GtkWidget *
make_tree (GtkTreeModel *store, const char *name_title, const char *bar_title,
	   const char *extra_title, gboolean with_sub)
{
	GtkWidget *tree = gtk_tree_view_new_with_model (store);
	GtkCellRenderer *r;
	GtkTreeViewColumn *c;

	gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (tree), TRUE);
	gtk_tree_selection_set_mode (gtk_tree_view_get_selection (GTK_TREE_VIEW (tree)), GTK_SELECTION_MULTIPLE);
	gtk_tree_view_set_search_column (GTK_TREE_VIEW (tree), COL_NAME);

	r = gtk_cell_renderer_text_new ();
	g_object_set (r, "ellipsize", PANGO_ELLIPSIZE_MIDDLE, NULL);
	c = gtk_tree_view_column_new_with_attributes (name_title, r, "text", COL_NAME, NULL);
	gtk_tree_view_column_set_expand (c, TRUE);
	gtk_tree_view_column_set_resizable (c, TRUE);
	gtk_tree_view_column_set_sort_column_id (c, COL_NAME);
	gtk_tree_view_append_column (GTK_TREE_VIEW (tree), c);

	if (with_sub) {
		r = gtk_cell_renderer_text_new ();
		g_object_set (r, "ellipsize", PANGO_ELLIPSIZE_START, "foreground-set", FALSE,
			      "scale", 0.9, NULL);
		c = gtk_tree_view_column_new_with_attributes (_("Location"), r, "text", COL_SUB, NULL);
		gtk_tree_view_column_set_expand (c, TRUE);
		gtk_tree_view_column_set_resizable (c, TRUE);
		gtk_tree_view_append_column (GTK_TREE_VIEW (tree), c);
	}

	if (bar_title != NULL) {
		r = gtk_cell_renderer_progress_new ();
		c = gtk_tree_view_column_new_with_attributes (bar_title, r,
							      "value", COL_PERCENT,
							      "text", COL_BARTEXT, NULL);
		gtk_tree_view_column_set_min_width (c, 200);
		gtk_tree_view_column_set_sort_column_id (c, COL_SIZE);
		gtk_tree_view_append_column (GTK_TREE_VIEW (tree), c);
	}

	if (extra_title != NULL) {
		r = gtk_cell_renderer_text_new ();
		c = gtk_tree_view_column_new_with_attributes (extra_title, r, "text", COL_EXTRA, NULL);
		gtk_tree_view_append_column (GTK_TREE_VIEW (tree), c);
	}
	return tree;
}

static GtkListStore *
make_store (void)
{
	return gtk_list_store_new (N_COLS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT,
				   G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
				   G_TYPE_BOOLEAN, G_TYPE_INT64);
}

static GtkWidget *
scrolled (GtkWidget *child)
{
	GtkWidget *sw = gtk_scrolled_window_new (NULL, NULL);

	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_hexpand (sw, TRUE);
	gtk_widget_set_vexpand (sw, TRUE);
	gtk_container_add (GTK_CONTAINER (sw), child);
	return sw;
}

static void
store_append (GtkListStore *s, const char *name, const char *sub, int percent,
	      gint64 size, const char *extra, const char *path, const char *kind,
	      gboolean check)
{
	GtkTreeIter it;
	char *sz = format_size (size);

	gtk_list_store_append (s, &it);
	gtk_list_store_set (s, &it, COL_NAME, name, COL_SUB, sub, COL_PERCENT, CLAMP (percent, 0, 100),
			    COL_BARTEXT, sz, COL_EXTRA, extra, COL_PATH, path, COL_KIND, kind,
			    COL_CHECK, check, COL_SIZE, size, -1);
	g_free (sz);
}

static void
tree_append (GtkTreeStore *s, GtkTreeIter *parent, const char *name, const char *sub, int percent,
	     gint64 size, const char *extra, const char *path, const char *kind, gboolean check,
	     GtkTreeIter *out)
{
	GtkTreeIter it;
	char *sz = format_size (size);

	gtk_tree_store_append (s, &it, parent);
	gtk_tree_store_set (s, &it, COL_NAME, name, COL_SUB, sub, COL_PERCENT, CLAMP (percent, 0, 100),
			    COL_BARTEXT, sz, COL_EXTRA, extra, COL_PATH, path, COL_KIND, kind,
			    COL_CHECK, check, COL_SIZE, size, -1);
	g_free (sz);
	if (out != NULL)
		*out = it;
}

static GtkWindow *
toplevel_of (NemoStorageView *self)
{
	GtkWidget *w = gtk_widget_get_toplevel (GTK_WIDGET (self));

	return GTK_IS_WINDOW (w) ? GTK_WINDOW (w) : NULL;
}

static gboolean
confirm (NemoStorageView *self, const char *title, const char *detail, const char *action)
{
	GtkWidget *d = gtk_message_dialog_new (toplevel_of (self), GTK_DIALOG_MODAL,
					       GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE, "%s", title);
	int r;

	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (d), "%s", detail);
	gtk_dialog_add_button (GTK_DIALOG (d), _("_Cancel"), GTK_RESPONSE_CANCEL);
	gtk_dialog_add_button (GTK_DIALOG (d), action, GTK_RESPONSE_ACCEPT);
	r = gtk_dialog_run (GTK_DIALOG (d));
	gtk_widget_destroy (d);
	return r == GTK_RESPONSE_ACCEPT;
}

static void
deleted_cb (GHashTable *debuting, gboolean user_cancel, gpointer data)
{
	NemoStorageView *self = data;

	if (NEMO_IS_STORAGE_VIEW (self))
		call_json (self, "Rescan", NULL, NULL, NULL);
	g_object_unref (self);
}

static void
open_path (NemoStorageView *self, const char *path, gboolean select_parent)
{
	NemoWindowSlot *slot = nemo_view_get_nemo_window_slot (NEMO_VIEW (self));
	char *target = select_parent ? g_path_get_dirname (path) : g_strdup (path);
	GFile *f = g_file_new_for_path (target);

	if (slot != NULL)
		nemo_window_slot_open_location (slot, f, 0);
	g_object_unref (f);
	g_free (target);
}

/* ------------------------------------------------------------------ */
/* data -> UI                                                         */
/* ------------------------------------------------------------------ */

static gboolean
timeline_draw (GtkWidget *w, cairo_t *cr, gpointer data)
{
	NemoStorageView *self = data;
	GArray *pts = self->details->timeline_pts;
	GtkStyleContext *ctx = gtk_widget_get_style_context (w);
	int width = gtk_widget_get_allocated_width (w);
	int height = gtk_widget_get_allocated_height (w);
	GdkRGBA fg, accent;
	gint64 tmin = G_MAXINT64, tmax = 0, umin = G_MAXINT64, umax = 0;
	guint i, n = pts ? pts->len / 2 : 0;

	gtk_style_context_get_color (ctx, GTK_STATE_FLAG_NORMAL, &fg);
	if (!gtk_style_context_lookup_color (ctx, "theme_selected_bg_color", &accent))
		accent = fg;

	if (n < 2) {
		cairo_set_source_rgba (cr, fg.red, fg.green, fg.blue, 0.6);
		cairo_move_to (cr, 12, height / 2.0);
		cairo_show_text (cr, _("Usage history appears after a few scans."));
		return FALSE;
	}
	for (i = 0; i < n; i++) {
		gint64 t = g_array_index (pts, gint64, i * 2);
		gint64 u = g_array_index (pts, gint64, i * 2 + 1);
		tmin = MIN (tmin, t); tmax = MAX (tmax, t);
		umin = MIN (umin, u); umax = MAX (umax, u);
	}
	if (umax == umin)
		umax = umin + 1;
	if (tmax == tmin)
		tmax = tmin + 1;

	cairo_set_line_width (cr, 2.0);
	for (i = 0; i < n; i++) {
		double x = 8 + (width - 16) * (double) (g_array_index (pts, gint64, i * 2) - tmin) / (tmax - tmin);
		double y = height - 8 - (height - 16) * (double) (g_array_index (pts, gint64, i * 2 + 1) - umin) / (umax - umin);

		if (i == 0)
			cairo_move_to (cr, x, y);
		else
			cairo_line_to (cr, x, y);
	}
	gdk_cairo_set_source_rgba (cr, &accent);
	cairo_stroke_preserve (cr);
	cairo_line_to (cr, width - 8, height - 8);
	cairo_line_to (cr, 8, height - 8);
	cairo_close_path (cr);
	cairo_set_source_rgba (cr, accent.red, accent.green, accent.blue, 0.18);
	cairo_fill (cr);
	return FALSE;
}

static void
on_overview (NemoStorageView *self, JsonNode *root, gpointer data)
{
	NemoStorageViewDetails *d = self->details;
	JsonObject *o = json_node_get_object (root);
	gint64 total = jint (o, "total"), used = jint (o, "used"), free_b = jint (o, "free");
	gint64 tracked = jint (o, "tracked");
	char *s_used, *s_total, *s_free, *s_tr, *ago, *text;
	JsonArray *arr;
	guint i;

	g_free (d->root_path);
	d->root_path = g_strdup (jstr (o, "root"));

	s_used = format_size (used); s_total = format_size (total);
	s_free = format_size (free_b); s_tr = format_size (tracked);
	ago = human_ago (jint (o, "last_scan"));

	text = g_strdup_printf (_("%s used of %s  ·  %s free"), s_used, s_total, s_free);
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (d->usage_bar),
				       total > 0 ? (double) used / total : 0);
	gtk_progress_bar_set_text (GTK_PROGRESS_BAR (d->usage_bar), text);
	g_free (text);

	text = g_strdup_printf (_("%s in %s  ·  last scan %s"), s_tr, d->root_path, ago);
	gtk_label_set_text (GTK_LABEL (d->subtitle), text);
	g_free (text);
	g_free (s_used); g_free (s_total); g_free (s_free); g_free (s_tr); g_free (ago);

	gtk_list_store_clear (d->cat_store);
	arr = json_object_get_array_member (o, "categories");
	for (i = 0; arr && i < json_array_get_length (arr); i++) {
		JsonObject *c = json_array_get_object_element (arr, i);
		gint64 size = jint (c, "size");
		char *items = g_strdup_printf (_("%ld files"), (long) jint (c, "nfiles"));

		store_append (d->cat_store, jstr (c, "cat"), "",
			      tracked > 0 ? (int) (100.0 * size / tracked) : 0, size, items, "", "", FALSE);
		g_free (items);
	}

	gtk_list_store_clear (d->top_store);
	arr = json_object_get_array_member (o, "top");
	for (i = 0; arr && i < json_array_get_length (arr); i++) {
		JsonObject *c = json_array_get_object_element (arr, i);
		gint64 size = jint (c, "total");
		char *items = g_strdup_printf (_("%ld files"), (long) jint (c, "nfiles"));

		store_append (d->top_store, jstr (c, "name"), jstr (c, "path"),
			      tracked > 0 ? (int) (100.0 * size / tracked) : 0, size, items,
			      jstr (c, "path"), "", FALSE);
		g_free (items);
	}

	if (d->timeline_pts != NULL)
		g_array_set_size (d->timeline_pts, 0);
	arr = json_object_get_array_member (o, "timeline");
	for (i = 0; arr && i < json_array_get_length (arr); i++) {
		JsonObject *c = json_array_get_object_element (arr, i);
		gint64 v[2] = { jint (c, "ts"), jint (c, "used") };

		g_array_append_vals (d->timeline_pts, v, 2);
	}
	gtk_widget_queue_draw (d->timeline);

	if (d->folder_path == NULL)
		nemo_storage_view_load_folder (self, d->root_path);
	if (d->map_path == NULL)
		load_map (self, d->root_path);
}

static void
on_children (NemoStorageView *self, JsonNode *root, gpointer data)
{
	NemoStorageViewDetails *d = self->details;
	JsonObject *o = json_node_get_object (root);
	JsonArray *arr = json_object_get_array_member (o, "children");
	gint64 total = jint (o, "total"), own = jint (o, "own");
	char *label;
	guint i;

	g_free (d->folder_path);
	d->folder_path = g_strdup (jstr (o, "path"));
	g_free (d->folder_parent);
	d->folder_parent = json_object_get_null_member (o, "parent") ? NULL : g_strdup (jstr (o, "parent"));

	label = g_strdup_printf ("%s", d->folder_path);
	gtk_label_set_text (GTK_LABEL (d->folder_path_label), label);
	g_free (label);
	gtk_widget_set_sensitive (d->folder_up_button, d->folder_parent != NULL);

	gtk_list_store_clear (d->folder_store);
	for (i = 0; arr && i < json_array_get_length (arr); i++) {
		JsonObject *c = json_array_get_object_element (arr, i);
		gint64 size = jint (c, "total");
		char *items = g_strdup_printf (_("%ld files"), (long) jint (c, "nfiles"));

		store_append (d->folder_store, jstr (c, "name"), jstr (c, "path"),
			      total > 0 ? (int) (100.0 * size / total) : 0, size, items,
			      jstr (c, "path"), "dir", FALSE);
		g_free (items);
	}
	if (own > 0) {
		char *items = g_strdup_printf (_("%ld files"), (long) jint (o, "nfiles"));

		store_append (d->folder_store, _("(files in this folder)"), d->folder_path,
			      total > 0 ? (int) (100.0 * own / total) : 0, own, items,
			      "", "files", FALSE);
		g_free (items);
	}
}

static void
on_largest (NemoStorageView *self, JsonNode *root, gpointer data)
{
	NemoStorageViewDetails *d = self->details;
	JsonArray *arr = json_node_get_array (root);
	gint64 biggest = 0;
	guint i;

	gtk_list_store_clear (d->large_store);
	for (i = 0; i < json_array_get_length (arr); i++) {
		JsonObject *c = json_array_get_object_element (arr, i);
		gint64 size = jint (c, "size");
		char *dir, *age, *extra;

		if (i == 0)
			biggest = MAX (size, 1);
		dir = g_path_get_dirname (jstr (c, "path"));
		age = human_ago ((gint64) json_object_get_double_member (c, "mtime"));
		extra = g_strdup_printf ("%s · %s", jstr (c, "cat"), age);
		store_append (d->large_store, jstr (c, "name"), dir,
			      (int) (100.0 * size / biggest), size, extra, jstr (c, "path"), "file", FALSE);
		g_free (dir); g_free (age); g_free (extra);
	}
}

static const char *
kind_label (const char *kind)
{
	if (g_strcmp0 (kind, "cache") == 0) return _("Cache");
	if (g_strcmp0 (kind, "dev") == 0) return _("Build artifacts");
	if (g_strcmp0 (kind, "trash") == 0) return _("Trash");
	if (g_strcmp0 (kind, "old-download") == 0) return _("Old download (90+ days)");
	return kind;
}

static void
update_cleanup_total (NemoStorageView *self)
{
	GtkTreeModel *m = GTK_TREE_MODEL (self->details->cleanup_store);
	GtkTreeIter it;
	gint64 sum = 0;
	char *s, *t;

	if (gtk_tree_model_get_iter_first (m, &it)) {
		do {
			gboolean on; gint64 size;

			gtk_tree_model_get (m, &it, COL_CHECK, &on, COL_SIZE, &size, -1);
			if (on)
				sum += size;
		} while (gtk_tree_model_iter_next (m, &it));
	}
	s = format_size (sum);
	t = g_strdup_printf (_("Selected: %s"), s);
	gtk_label_set_text (GTK_LABEL (self->details->cleanup_total), t);
	g_free (s); g_free (t);
}

static void
on_cleanup (NemoStorageView *self, JsonNode *root, gpointer data)
{
	NemoStorageViewDetails *d = self->details;
	JsonArray *arr = json_node_get_array (root);
	gint64 biggest = 0;
	guint i;

	gtk_tree_store_clear (d->cleanup_store);
	for (i = 0; i < json_array_get_length (arr); i++) {
		JsonObject *c = json_array_get_object_element (arr, i);
		gint64 size = jint (c, "size");
		const char *kind = jstr (c, "kind");
		GtkTreeIter it;

		if (i == 0)
			biggest = MAX (size, 1);
		tree_append (d->cleanup_store, NULL, jstr (c, "name"), jstr (c, "path"),
			     (int) (100.0 * size / biggest), size, kind_label (kind),
			     jstr (c, "path"), kind, FALSE, &it);
		/* everything except an old download is a folder: give it an expander */
		if (g_strcmp0 (kind, "old-download") != 0 && g_strcmp0 (kind, "trash") != 0)
			tree_append (d->cleanup_store, &it, "…", "", 0, 0, "", "", "placeholder", FALSE, NULL);
	}
	update_cleanup_total (self);
}

static void
on_growth (NemoStorageView *self, JsonNode *root, gpointer data)
{
	NemoStorageViewDetails *d = self->details;
	JsonArray *arr = json_node_get_array (root);
	gint64 biggest = 0;
	guint i;

	gtk_list_store_clear (d->growth_store);
	for (i = 0; i < json_array_get_length (arr); i++) {
		JsonObject *c = json_array_get_object_element (arr, i);
		gint64 delta = jint (c, "delta");
		char *sz = format_size (delta), *extra, *now;
		GtkTreeIter it;

		if (i == 0)
			biggest = MAX (llabs (delta), 1);
		now = format_size (jint (c, "total"));
		extra = g_strdup_printf (_("now %s"), now);
		gtk_list_store_append (d->growth_store, &it);
		{
			char *bar = g_strdup_printf ("%s%s", delta >= 0 ? "+" : "−", sz);

			gtk_list_store_set (d->growth_store, &it, COL_NAME, jstr (c, "name"),
					    COL_SUB, jstr (c, "path"),
					    COL_PERCENT, (int) (100.0 * llabs (delta) / biggest),
					    COL_BARTEXT, bar, COL_EXTRA, extra,
					    COL_PATH, jstr (c, "path"), COL_SIZE, llabs (delta), -1);
			g_free (bar);
		}
		g_free (sz); g_free (extra); g_free (now);
	}
}

static void
on_status (NemoStorageView *self, JsonNode *root, gpointer data)
{
	NemoStorageViewDetails *d = self->details;
	JsonObject *o = json_node_get_object (root);
	gboolean scanning = json_object_get_boolean_member (o, "scanning");

	if (scanning) {
		JsonObject *p = json_object_get_object_member (o, "progress");
		char *t = g_strdup_printf (_("Scanning… %ld files"), (long) jint (p, "files"));

		gtk_label_set_text (GTK_LABEL (d->subtitle), t);
		g_free (t);
	}
	if (d->scanning && !scanning)
		nemo_storage_view_refresh_all (self);
	d->scanning = scanning;
	gtk_widget_set_sensitive (d->rescan_button, !scanning);
	if (scanning) gtk_spinner_start (GTK_SPINNER (d->spinner));
	else gtk_spinner_stop (GTK_SPINNER (d->spinner));
}

static gboolean
poll_status (gpointer data)
{
	call_json (data, "GetStatus", NULL, on_status, NULL);
	return G_SOURCE_CONTINUE;
}

static void
nemo_storage_view_load_folder (NemoStorageView *self, const char *path)
{
	call_json (self, "GetChildren", g_variant_new ("(s)", path), on_children, NULL);
}

static void
nemo_storage_view_refresh_all (NemoStorageView *self)
{
	NemoStorageViewDetails *d = self->details;

	call_json (self, "GetOverview", NULL, on_overview, NULL);
	call_json (self, "GetLargestFiles", g_variant_new ("(i)", 300), on_largest, NULL);
	call_json (self, "GetCleanup", NULL, on_cleanup, NULL);
	call_json (self, "GetGrowth", g_variant_new ("(i)", d->growth_hours), on_growth, NULL);
	if (d->folder_path != NULL)
		nemo_storage_view_load_folder (self, d->folder_path);
	if (d->map_path != NULL)
		load_map (self, d->map_path);
}

static gboolean
refresh_timeout (gpointer data)
{
	NemoStorageView *self = data;

	self->details->refresh_id = 0;
	nemo_storage_view_refresh_all (self);
	return G_SOURCE_REMOVE;
}

/* ------------------------------------------------------------------ */
/* actions                                                            */
/* ------------------------------------------------------------------ */

static void
on_rescan_clicked (GtkButton *b, gpointer data)
{
	call_json (data, "Rescan", NULL, NULL, NULL);
	g_timeout_add_seconds (1, (GSourceFunc) poll_status, data);
}

static void
on_folder_activated (GtkTreeView *tv, GtkTreePath *p, GtkTreeViewColumn *c, gpointer data)
{
	GtkTreeModel *m = gtk_tree_view_get_model (tv);
	GtkTreeIter it;
	char *path = NULL, *kind = NULL;

	if (gtk_tree_model_get_iter (m, &it, p)) {
		gtk_tree_model_get (m, &it, COL_PATH, &path, COL_KIND, &kind, -1);
		if (path != NULL && path[0] != '\0' && g_strcmp0 (kind, "dir") == 0)
			nemo_storage_view_load_folder (data, path);
	}
	g_free (path); g_free (kind);
}

static void
on_top_activated (GtkTreeView *tv, GtkTreePath *p, GtkTreeViewColumn *c, gpointer data)
{
	NemoStorageView *self = data;
	GtkTreeModel *m = gtk_tree_view_get_model (tv);
	GtkTreeIter it;
	char *path = NULL;

	if (gtk_tree_model_get_iter (m, &it, p)) {
		gtk_tree_model_get (m, &it, COL_PATH, &path, -1);
		if (path != NULL && path[0] != '\0') {
			nemo_storage_view_load_folder (self, path);
			gtk_stack_set_visible_child_name (GTK_STACK (self->details->stack), "folders");
		}
	}
	g_free (path);
}

static void
on_up_clicked (GtkButton *b, gpointer data)
{
	NemoStorageView *self = data;

	if (self->details->folder_parent != NULL)
		nemo_storage_view_load_folder (self, self->details->folder_parent);
}

static GList *
selected_paths (GtkTreeView *tv)
{
	GtkTreeModel *m;
	GList *rows = gtk_tree_selection_get_selected_rows (gtk_tree_view_get_selection (tv), &m);
	GList *out = NULL, *l;

	for (l = rows; l != NULL; l = l->next) {
		GtkTreeIter it;
		char *path = NULL;

		if (gtk_tree_model_get_iter (m, &it, l->data))
			gtk_tree_model_get (m, &it, COL_PATH, &path, -1);
		if (path != NULL && path[0] != '\0')
			out = g_list_append (out, path);
		else
			g_free (path);
	}
	g_list_free_full (rows, (GDestroyNotify) gtk_tree_path_free);
	return out;
}

static char *
selected_path (GtkTreeView *tv)
{
	GList *l = selected_paths (tv);
	char *first = l != NULL ? g_strdup (l->data) : NULL;

	g_list_free_full (l, g_free);
	return first;
}

static void
trash_paths (NemoStorageView *self, GList *paths, gboolean permanent)
{
	guint n = g_list_length (paths);
	char *title, *detail, *base;
	GList *files = NULL, *l;

	if (n == 0)
		return;
	base = g_path_get_basename (paths->data);
	if (permanent)
		title = n == 1 ? g_strdup_printf (_("Permanently delete “%s”?"), base)
			       : g_strdup_printf (_("Permanently delete %u items?"), n);
	else
		title = n == 1 ? g_strdup_printf (_("Move “%s” to the Trash?"), base)
			       : g_strdup_printf (_("Move %u items to the Trash?"), n);
	detail = n == 1 ? g_strdup ((char *) paths->data)
			: g_strdup (permanent ? _("This cannot be undone.") : _("You can restore them from the Trash."));
	if (confirm (self, title, detail, permanent ? _("Delete permanently") : _("Move to Trash"))) {
		for (l = paths; l != NULL; l = l->next)
			files = g_list_prepend (files, g_file_new_for_path (l->data));
		if (permanent)
			nemo_file_operations_delete (files, toplevel_of (self), deleted_cb, g_object_ref (self));
		else
			nemo_file_operations_trash_or_delete (files, toplevel_of (self), deleted_cb, g_object_ref (self));
		g_list_free_full (files, g_object_unref);
	}
	g_free (base); g_free (title); g_free (detail);
}

static void
show_properties (NemoStorageView *self, const char *path)
{
	GFile *f = g_file_new_for_path (path);
	NemoFile *nf = nemo_file_get (f);
	GList *l = g_list_prepend (NULL, nf);

	nemo_properties_window_present (l, GTK_WIDGET (self), NULL);
	nemo_file_list_free (l);
	g_object_unref (f);
}

static void
on_trash_selected (GtkButton *b, gpointer data)
{
	NemoStorageView *self = data;
	GtkTreeView *tv = g_object_get_data (G_OBJECT (b), "tree");
	GList *paths = selected_paths (tv);

	trash_paths (self, paths, FALSE);
	g_list_free_full (paths, g_free);
}

static void
on_properties_selected (GtkButton *b, gpointer data)
{
	GtkTreeView *tv = g_object_get_data (G_OBJECT (b), "tree");
	char *path = selected_path (tv);

	if (path != NULL)
		show_properties (data, path);
	g_free (path);
}

/* ---- right-click menu, Delete key and trash column shared by the tabs ---- */

typedef struct {
	NemoStorageView *self;
	GtkTreeView *tree;
	gboolean select_parent;   /* "Show in Files" opens the containing folder */
} TreeActions;

static void
menu_act (GtkMenuItem *item, gpointer data)
{
	TreeActions *ta = g_object_get_data (G_OBJECT (item), "ta");
	const char *what = data;
	GList *paths = selected_paths (ta->tree);

	if (paths == NULL) {
		return;
	}
	if (g_strcmp0 (what, "trash") == 0)
		trash_paths (ta->self, paths, FALSE);
	else if (g_strcmp0 (what, "delete") == 0)
		trash_paths (ta->self, paths, TRUE);
	else if (g_strcmp0 (what, "show") == 0)
		open_path (ta->self, paths->data, ta->select_parent);
	else if (g_strcmp0 (what, "props") == 0)
		show_properties (ta->self, paths->data);
	g_list_free_full (paths, g_free);
}

static void
popup_actions_menu (TreeActions *ta, GdkEvent *ev)
{
	GtkWidget *menu = gtk_menu_new ();
	struct { const char *label; const char *what; } items[] = {
		{ N_("Show in Files"), "show" },
		{ N_("Properties"), "props" },
		{ NULL, NULL },
		{ N_("Move to Trash"), "trash" },
		{ N_("Delete Permanently…"), "delete" },
	};
	guint i;

	for (i = 0; i < G_N_ELEMENTS (items); i++) {
		GtkWidget *mi;

		if (items[i].label == NULL) {
			mi = gtk_separator_menu_item_new ();
		} else {
			mi = gtk_menu_item_new_with_label (_(items[i].label));
			g_object_set_data (G_OBJECT (mi), "ta", ta);
			g_signal_connect (mi, "activate", G_CALLBACK (menu_act), (gpointer) items[i].what);
		}
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), mi);
	}
	gtk_widget_show_all (menu);
	gtk_menu_popup_at_pointer (GTK_MENU (menu), ev);
}

static gboolean
tree_button_press (GtkWidget *w, GdkEventButton *ev, gpointer data)
{
	TreeActions *ta = data;
	GtkTreeView *tv = GTK_TREE_VIEW (w);
	GtkTreePath *path = NULL;
	GtkTreeViewColumn *col = NULL;

	if (ev->window != gtk_tree_view_get_bin_window (tv))
		return FALSE;
	if (!gtk_tree_view_get_path_at_pos (tv, ev->x, ev->y, &path, &col, NULL, NULL))
		return FALSE;
	if (ev->button == 3) {
		GtkTreeSelection *sel = gtk_tree_view_get_selection (tv);

		if (!gtk_tree_selection_path_is_selected (sel, path)) {
			gtk_tree_selection_unselect_all (sel);
			gtk_tree_selection_select_path (sel, path);
		}
		popup_actions_menu (ta, (GdkEvent *) ev);
		gtk_tree_path_free (path);
		return TRUE;
	}
	if (ev->button == 1 && col != NULL && g_object_get_data (G_OBJECT (col), "trash-column") != NULL) {
		GtkTreeSelection *sel = gtk_tree_view_get_selection (tv);
		GList *paths;

		gtk_tree_selection_unselect_all (sel);
		gtk_tree_selection_select_path (sel, path);
		paths = selected_paths (tv);
		trash_paths (ta->self, paths, FALSE);
		g_list_free_full (paths, g_free);
		gtk_tree_path_free (path);
		return TRUE;
	}
	gtk_tree_path_free (path);
	return FALSE;
}

static gboolean
tree_key_press (GtkWidget *w, GdkEventKey *ev, gpointer data)
{
	TreeActions *ta = data;

	if (ev->keyval == GDK_KEY_Delete || ev->keyval == GDK_KEY_KP_Delete) {
		GList *paths = selected_paths (ta->tree);

		trash_paths (ta->self, paths, (ev->state & GDK_SHIFT_MASK) != 0);
		g_list_free_full (paths, g_free);
		return TRUE;
	}
	return FALSE;
}

/* adds a trash-can column (click = move that row to the Trash) when with_column */
static void
attach_tree_actions (NemoStorageView *self, GtkWidget *tree, gboolean select_parent, gboolean with_column)
{
	TreeActions *ta = g_new0 (TreeActions, 1);

	ta->self = self;
	ta->tree = GTK_TREE_VIEW (tree);
	ta->select_parent = select_parent;
	g_object_set_data_full (G_OBJECT (tree), "tree-actions", ta, g_free);
	g_signal_connect (tree, "button-press-event", G_CALLBACK (tree_button_press), ta);
	g_signal_connect (tree, "key-press-event", G_CALLBACK (tree_key_press), ta);
	if (with_column) {
		GtkCellRenderer *r = gtk_cell_renderer_pixbuf_new ();
		GtkTreeViewColumn *c = gtk_tree_view_column_new ();

		g_object_set (r, "icon-name", "user-trash-symbolic", NULL);
		gtk_tree_view_column_pack_start (c, r, FALSE);
		gtk_tree_view_column_set_title (c, "");
		g_object_set_data (G_OBJECT (c), "trash-column", GINT_TO_POINTER (1));
		gtk_tree_view_append_column (GTK_TREE_VIEW (tree), c);
		gtk_widget_set_tooltip_text (tree, _("Click the trash icon, press Delete, or right-click for more"));
	}
}

static void
on_show_selected (GtkButton *b, gpointer data)
{
	GtkTreeView *tv = g_object_get_data (G_OBJECT (b), "tree");
	gboolean parent = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (b), "select-parent"));
	char *path = selected_path (tv);

	if (path != NULL)
		open_path (data, path, parent);
	g_free (path);
}

static void
on_open_folder_clicked (GtkButton *b, gpointer data)
{
	NemoStorageView *self = data;

	if (self->details->folder_path != NULL)
		open_path (self, self->details->folder_path, FALSE);
}

static void
on_cleanup_toggled (GtkCellRendererToggle *r, char *path_str, gpointer data)
{
	NemoStorageView *self = data;
	GtkTreeModel *m = GTK_TREE_MODEL (self->details->cleanup_store);
	GtkTreeIter it;

	if (gtk_tree_model_get_iter_from_string (m, &it, path_str)) {
		gboolean on;

		gtk_tree_model_get (m, &it, COL_CHECK, &on, -1);
		gtk_tree_store_set (self->details->cleanup_store, &it, COL_CHECK, !on, -1);
		update_cleanup_total (self);
	}
}

/* ---- Clean up tab: expand folders lazily, see file properties ---- */

typedef struct {
	gchar *name;
	gboolean is_dir;
	gint64 size;
} Entry;

static void
entry_free (Entry *e)
{
	g_free (e->name);
	g_free (e);
}

static int
entry_cmp (gconstpointer a, gconstpointer b)
{
	const Entry *x = *(Entry * const *) a, *y = *(Entry * const *) b;

	return x->size < y->size ? 1 : (x->size > y->size ? -1 : g_ascii_strcasecmp (x->name, y->name));
}

static void
fill_expanded (NemoStorageView *self, GtkTreeRowReference *ref, JsonObject *daemon_children)
{
	GtkTreeStore *store = self->details->cleanup_store;
	GtkTreeModel *m = GTK_TREE_MODEL (store);
	GtkTreePath *tp;
	GtkTreeIter parent, child;
	char *dir = NULL;
	GHashTable *dir_sizes = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	GPtrArray *entries = g_ptr_array_new_with_free_func ((GDestroyNotify) entry_free);
	GFile *gdir;
	GFileEnumerator *en;
	GFileInfo *info;
	gint64 biggest = 1;
	guint i;

	if (!gtk_tree_row_reference_valid (ref))
		goto out;
	tp = gtk_tree_row_reference_get_path (ref);
	gtk_tree_model_get_iter (m, &parent, tp);
	gtk_tree_path_free (tp);
	gtk_tree_model_get (m, &parent, COL_PATH, &dir, -1);
	if (dir == NULL || dir[0] == '\0')
		goto out;

	if (daemon_children != NULL && json_object_has_member (daemon_children, "children")) {
		JsonArray *arr = json_object_get_array_member (daemon_children, "children");

		for (i = 0; i < json_array_get_length (arr); i++) {
			JsonObject *c = json_array_get_object_element (arr, i);

			g_hash_table_insert (dir_sizes, g_strdup (jstr (c, "name")),
					     GSIZE_TO_POINTER ((gsize) jint (c, "total")));
		}
	}
	gdir = g_file_new_for_path (dir);
	en = g_file_enumerate_children (gdir, "standard::name,standard::type,standard::allocated-size,standard::size",
					G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, NULL);
	while (en != NULL && entries->len < 2000 && (info = g_file_enumerator_next_file (en, NULL, NULL)) != NULL) {
		Entry *e = g_new0 (Entry, 1);

		e->name = g_strdup (g_file_info_get_name (info));
		e->is_dir = g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY;
		e->size = e->is_dir ? (gint64) GPOINTER_TO_SIZE (g_hash_table_lookup (dir_sizes, e->name))
				    : (gint64) g_file_info_get_attribute_uint64 (info, "standard::allocated-size");
		if (!e->is_dir && e->size == 0)
			e->size = g_file_info_get_size (info);
		g_ptr_array_add (entries, e);
		g_object_unref (info);
	}
	g_clear_object (&en);
	g_object_unref (gdir);
	g_ptr_array_sort (entries, entry_cmp);

	/* drop the placeholder row, then add the real children */
	if (gtk_tree_model_iter_children (m, &child, &parent))
		gtk_tree_store_remove (store, &child);
	if (entries->len > 0)
		biggest = MAX (((Entry *) g_ptr_array_index (entries, 0))->size, 1);
	for (i = 0; i < entries->len && i < 300; i++) {
		Entry *e = g_ptr_array_index (entries, i);
		char *full = g_build_filename (dir, e->name, NULL);
		GtkTreeIter it;

		tree_append (store, &parent, e->name, "", (int) (100.0 * e->size / biggest), e->size,
			     e->is_dir ? _("Folder") : _("File"), full, e->is_dir ? "child-dir" : "child-file", FALSE, &it);
		if (e->is_dir)
			tree_append (store, &it, "…", "", 0, 0, "", "", "placeholder", FALSE, NULL);
		g_free (full);
	}
	if (entries->len == 0)
		tree_append (store, &parent, _("(empty)"), "", 0, 0, "", "", "empty", FALSE, NULL);
out:
	g_free (dir);
	g_hash_table_destroy (dir_sizes);
	g_ptr_array_free (entries, TRUE);
}

typedef struct { GtkTreeRowReference *ref; } ExpandCtx;

static void
on_children_for_expand (NemoStorageView *self, JsonNode *root, gpointer data)
{
	ExpandCtx *ctx = data;

	fill_expanded (self, ctx->ref, json_node_get_object (root));
	gtk_tree_row_reference_free (ctx->ref);
	g_free (ctx);
}

static void
on_cleanup_row_expanded (GtkTreeView *tv, GtkTreeIter *iter, GtkTreePath *path, gpointer data)
{
	NemoStorageView *self = data;
	GtkTreeModel *m = gtk_tree_view_get_model (tv);
	GtkTreeIter child;
	char *kind = NULL, *dir = NULL;
	ExpandCtx *ctx;

	if (!gtk_tree_model_iter_children (m, &child, iter))
		return;
	gtk_tree_model_get (m, &child, COL_KIND, &kind, -1);
	if (g_strcmp0 (kind, "placeholder") != 0) {
		g_free (kind);
		return;
	}
	g_free (kind);
	gtk_tree_model_get (m, iter, COL_PATH, &dir, -1);
	ctx = g_new0 (ExpandCtx, 1);
	ctx->ref = gtk_tree_row_reference_new (m, path);
	if (self->details->proxy != NULL) {
		/* the index knows folder sizes; files are listed straight from disk */
		call_json (self, "GetChildren", g_variant_new ("(s)", dir), on_children_for_expand, ctx);
	} else {
		fill_expanded (self, ctx->ref, NULL);
		gtk_tree_row_reference_free (ctx->ref);
		g_free (ctx);
	}
	g_free (dir);
}

static void
on_cleanup_row_activated (GtkTreeView *tv, GtkTreePath *path, GtkTreeViewColumn *c, gpointer data)
{
	GtkTreeModel *m = gtk_tree_view_get_model (tv);
	GtkTreeIter it;
	char *kind = NULL, *p = NULL;

	if (!gtk_tree_model_get_iter (m, &it, path))
		return;
	gtk_tree_model_get (m, &it, COL_KIND, &kind, COL_PATH, &p, -1);
	if (g_strcmp0 (kind, "child-file") == 0 || g_strcmp0 (kind, "old-download") == 0) {
		show_properties (data, p);   /* files: open their properties */
	} else if (gtk_tree_view_row_expanded (tv, path)) {
		gtk_tree_view_collapse_row (tv, path);
	} else {
		gtk_tree_view_expand_row (tv, path, FALSE);
	}
	g_free (kind); g_free (p);
}

/* the checkbox only makes sense on the top-level suggestions */
static void
toggle_cell_data (GtkTreeViewColumn *col, GtkCellRenderer *r, GtkTreeModel *m, GtkTreeIter *it, gpointer data)
{
	GtkTreePath *p = gtk_tree_model_get_path (m, it);

	g_object_set (r, "visible", gtk_tree_path_get_depth (p) == 1, NULL);
	gtk_tree_path_free (p);
}

static void
on_cleanup_run (GtkButton *b, gpointer data)
{
	NemoStorageView *self = data;
	GtkTreeModel *m = GTK_TREE_MODEL (self->details->cleanup_store);
	GtkTreeIter it;
	GList *perm = NULL, *trash = NULL;
	gboolean empty_trash = FALSE;
	gint64 sum = 0;
	char *s, *detail;

	if (gtk_tree_model_get_iter_first (m, &it)) {
		do {
			gboolean on; gint64 size; char *path, *kind;

			gtk_tree_model_get (m, &it, COL_CHECK, &on, COL_SIZE, &size,
					    COL_PATH, &path, COL_KIND, &kind, -1);
			if (on) {
				sum += size;
				if (g_strcmp0 (kind, "trash") == 0)
					empty_trash = TRUE;
				else if (g_strcmp0 (kind, "old-download") == 0)
					trash = g_list_prepend (trash, g_file_new_for_path (path));
				else
					perm = g_list_prepend (perm, g_file_new_for_path (path));
			}
			g_free (path); g_free (kind);
		} while (gtk_tree_model_iter_next (m, &it));
	}
	if (sum == 0)
		return;

	s = format_size (sum);
	detail = g_strdup_printf (_("Caches and build artifacts are deleted permanently "
				   "(they are re-created when needed). Old downloads go to the Trash. "
				   "This frees about %s."), s);
	if (confirm (self, _("Free up space?"), detail, _("Free up space"))) {
		if (perm != NULL)
			nemo_file_operations_delete (perm, toplevel_of (self), deleted_cb, g_object_ref (self));
		if (trash != NULL)
			nemo_file_operations_trash_or_delete (trash, toplevel_of (self), deleted_cb, g_object_ref (self));
		if (empty_trash)
			nemo_file_operations_empty_trash (GTK_WIDGET (self));
	}
	g_free (s); g_free (detail);
	g_list_free_full (perm, g_object_unref);
	g_list_free_full (trash, g_object_unref);
}

static void
on_cleanup_select_all (GtkButton *b, gpointer data)
{
	NemoStorageView *self = data;
	GtkTreeModel *m = GTK_TREE_MODEL (self->details->cleanup_store);
	GtkTreeIter it;

	if (gtk_tree_model_get_iter_first (m, &it)) {
		do {
			char *kind;

			gtk_tree_model_get (m, &it, COL_KIND, &kind, -1);
			/* "select all" skips old downloads: those are personal files */
			gtk_tree_store_set (self->details->cleanup_store, &it, COL_CHECK,
					    g_strcmp0 (kind, "old-download") != 0, -1);
			g_free (kind);
		} while (gtk_tree_model_iter_next (m, &it));
	}
	update_cleanup_total (self);
}

static void
on_growth_toggled (GtkToggleButton *b, gpointer data)
{
	NemoStorageView *self = data;

	if (gtk_toggle_button_get_active (b)) {
		self->details->growth_hours = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (b), "hours"));
		call_json (self, "GetGrowth", g_variant_new ("(i)", self->details->growth_hours),
			   on_growth, NULL);
	}
}

/* ------------------------------------------------------------------ */
/* treemap                                                            */
/* ------------------------------------------------------------------ */

typedef struct TreeNode {
	char *name;
	char *path;
	gint64 size;
	gboolean is_files;
	GPtrArray *kids;
	double x, y, w, h;      /* outer rectangle */
	double ix, iy, iw, ih;  /* where the children live */
	double hue;
	int depth;
} TreeNode;

static void
tree_free (TreeNode *n)
{
	guint i;

	if (n == NULL)
		return;
	for (i = 0; i < n->kids->len; i++)
		tree_free (g_ptr_array_index (n->kids, i));
	g_ptr_array_free (n->kids, TRUE);
	g_free (n->name);
	g_free (n->path);
	g_free (n);
}

static TreeNode *
tree_from_json (JsonObject *o, int depth, double hue)
{
	TreeNode *n = g_new0 (TreeNode, 1);
	JsonArray *arr = json_object_has_member (o, "children") ? json_object_get_array_member (o, "children") : NULL;
	guint i, count = arr ? json_array_get_length (arr) : 0;

	n->name = g_strdup (jstr (o, "name"));
	n->path = g_strdup (jstr (o, "path"));
	n->size = jint (o, "size");
	n->is_files = g_strcmp0 (jstr (o, "kind"), "files") == 0;
	n->kids = g_ptr_array_new ();
	n->depth = depth;
	n->hue = hue;
	for (i = 0; i < count; i++) {
		/* each top-level folder gets its own hue (golden-ratio spacing) */
		double h = depth == 0 ? fmod (0.07 + i * 0.382, 1.0) : hue;

		g_ptr_array_add (n->kids, tree_from_json (json_array_get_object_element (arr, i), depth + 1, h));
	}
	return n;
}

static double
row_worst (double *areas, int n, double side)
{
	double sum = 0, mx = 0, mn = G_MAXDOUBLE;
	int i;

	for (i = 0; i < n; i++) {
		sum += areas[i];
		mx = MAX (mx, areas[i]);
		mn = MIN (mn, areas[i]);
	}
	if (sum <= 0 || mn <= 0 || side <= 0)
		return G_MAXDOUBLE;
	return MAX (side * side * mx / (sum * sum), sum * sum / (side * side * mn));
}

/* squarified treemap (Bruls, Huizing, van Wijk) */
static void
tree_layout_children (TreeNode *parent)
{
	guint n = parent->kids->len, i = 0;
	double x = parent->ix, y = parent->iy, w = parent->iw, h = parent->ih;
	double total = 0, scale;
	double *areas;

	if (n == 0 || w < 2 || h < 2)
		return;
	for (i = 0; i < n; i++)
		total += ((TreeNode *) g_ptr_array_index (parent->kids, i))->size;
	if (total <= 0)
		return;
	scale = (w * h) / total;
	areas = g_new (double, n);
	for (i = 0; i < n; i++)
		areas[i] = ((TreeNode *) g_ptr_array_index (parent->kids, i))->size * scale;

	i = 0;
	while (i < n) {
		double side = MIN (w, h);
		guint end = i + 1;
		double sum, thick, pos;
		guint j;

		while (end < n && row_worst (areas + i, end - i + 1, side) <= row_worst (areas + i, end - i, side))
			end++;
		sum = 0;
		for (j = i; j < end; j++)
			sum += areas[j];
		if (w >= h) {               /* column on the left */
			thick = sum / h;
			pos = y;
			for (j = i; j < end; j++) {
				TreeNode *k = g_ptr_array_index (parent->kids, j);
				double len = areas[j] / thick;

				k->x = x; k->y = pos; k->w = thick; k->h = len;
				pos += len;
			}
			x += thick; w -= thick;
		} else {                    /* row along the top */
			thick = sum / w;
			pos = x;
			for (j = i; j < end; j++) {
				TreeNode *k = g_ptr_array_index (parent->kids, j);
				double len = areas[j] / thick;

				k->x = pos; k->y = y; k->w = len; k->h = thick;
				pos += len;
			}
			y += thick; h -= thick;
		}
		i = end;
	}
	g_free (areas);
}

static void
tree_layout (TreeNode *n)
{
	guint i;
	double header = (n->w > 70 && n->h > 40) ? 16 : 0;
	double pad = (n->w > 24 && n->h > 24) ? 2 : 0;

	n->ix = n->x + pad;
	n->iy = n->y + header + pad;
	n->iw = MAX (n->w - 2 * pad, 0);
	n->ih = MAX (n->h - header - 2 * pad, 0);
	tree_layout_children (n);
	for (i = 0; i < n->kids->len; i++)
		tree_layout (g_ptr_array_index (n->kids, i));
}

static TreeNode *
tree_hit (TreeNode *n, double px, double py)
{
	guint i;

	if (px < n->x || py < n->y || px > n->x + n->w || py > n->y + n->h)
		return NULL;
	for (i = 0; i < n->kids->len; i++) {
		TreeNode *hit = tree_hit (g_ptr_array_index (n->kids, i), px, py);

		if (hit != NULL)
			return hit;
	}
	return n;
}

static void
hsv_to_rgba (double h, double s, double v, GdkRGBA *c)
{
	double r, g, b;
	int i = (int) (h * 6);
	double f = h * 6 - i, p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);

	switch (i % 6) {
	case 0: r = v; g = t; b = p; break;
	case 1: r = q; g = v; b = p; break;
	case 2: r = p; g = v; b = t; break;
	case 3: r = p; g = q; b = v; break;
	case 4: r = t; g = p; b = v; break;
	default: r = v; g = p; b = q; break;
	}
	c->red = r; c->green = g; c->blue = b; c->alpha = 1.0;
}

static void
tree_draw (cairo_t *cr, PangoLayout *layout, TreeNode *n, TreeNode *hover, gint64 root_size)
{
	guint i;

	if (n->depth > 0 && n->w >= 1 && n->h >= 1) {
		GdkRGBA fill;
		double v = CLAMP (0.78 - n->depth * 0.12, 0.30, 0.78);

		if (n->is_files)
			hsv_to_rgba (0.0, 0.0, CLAMP (0.5 - n->depth * 0.07, 0.25, 0.5), &fill);
		else
			hsv_to_rgba (n->hue, 0.55, v, &fill);
		cairo_rectangle (cr, n->x + 0.5, n->y + 0.5, MAX (n->w - 1, 0), MAX (n->h - 1, 0));
		gdk_cairo_set_source_rgba (cr, &fill);
		cairo_fill_preserve (cr);
		if (n == hover)
			cairo_set_source_rgba (cr, 1, 1, 1, 0.95);
		else
			cairo_set_source_rgba (cr, 0, 0, 0, 0.55);
		cairo_set_line_width (cr, n == hover ? 2.0 : 1.0);
		cairo_stroke (cr);

		if (n->w > 54 && n->h > 15) {
			char *sz = g_format_size (n->size);
			char *text = g_strdup_printf ("%s  %s", n->name, sz);

			cairo_save (cr);
			cairo_rectangle (cr, n->x + 2, n->y, n->w - 4, MIN (n->h, 16));
			cairo_clip (cr);
			cairo_move_to (cr, n->x + 4, n->y + 1);
			pango_layout_set_text (layout, text, -1);
			cairo_set_source_rgba (cr, 0, 0, 0, 0.75);
			pango_cairo_show_layout (cr, layout);
			cairo_move_to (cr, n->x + 3, n->y);
			cairo_set_source_rgba (cr, 1, 1, 1, 0.97);
			pango_cairo_show_layout (cr, layout);
			cairo_restore (cr);
			g_free (sz);
			g_free (text);
		}
	}
	for (i = 0; i < n->kids->len; i++)
		tree_draw (cr, layout, g_ptr_array_index (n->kids, i), hover, root_size);
}

static void
map_relayout (NemoStorageView *self)
{
	NemoStorageViewDetails *d = self->details;
	int w = gtk_widget_get_allocated_width (d->map_area);
	int h = gtk_widget_get_allocated_height (d->map_area);

	if (d->map_root == NULL)
		return;
	d->map_root->x = 0; d->map_root->y = 0;
	d->map_root->w = w; d->map_root->h = h;
	d->map_root->depth = 0;
	/* the root fills the whole area with no header or padding */
	d->map_root->ix = 0; d->map_root->iy = 0; d->map_root->iw = w; d->map_root->ih = h;
	tree_layout_children (d->map_root);
	{
		guint i;

		for (i = 0; i < d->map_root->kids->len; i++)
			tree_layout (g_ptr_array_index (d->map_root->kids, i));
	}
	d->map_w = w;
	d->map_h = h;
}

static gboolean
map_draw (GtkWidget *w, cairo_t *cr, gpointer data)
{
	NemoStorageView *self = data;
	NemoStorageViewDetails *d = self->details;
	int width = gtk_widget_get_allocated_width (w), height = gtk_widget_get_allocated_height (w);
	PangoLayout *layout;
	PangoFontDescription *fd;

	cairo_set_source_rgba (cr, 0, 0, 0, 0.25);
	cairo_paint (cr);
	if (d->map_root == NULL || d->map_root->size <= 0 || d->map_root->kids->len == 0) {
		GtkStyleContext *ctx = gtk_widget_get_style_context (w);
		GdkRGBA fg;

		gtk_style_context_get_color (ctx, GTK_STATE_FLAG_NORMAL, &fg);
		gdk_cairo_set_source_rgba (cr, &fg);
		cairo_move_to (cr, 16, 24);
		cairo_show_text (cr, d->map_root == NULL ? _("Loading…") : _("Nothing to show in this folder."));
		return FALSE;
	}
	if (d->map_w != width || d->map_h != height)
		map_relayout (self);

	layout = pango_cairo_create_layout (cr);
	fd = pango_font_description_from_string ("Sans 9");
	pango_layout_set_font_description (layout, fd);
	pango_font_description_free (fd);
	tree_draw (cr, layout, d->map_root, d->map_hover, d->map_root->size);
	g_object_unref (layout);
	return FALSE;
}

static gboolean
map_motion (GtkWidget *w, GdkEventMotion *ev, gpointer data)
{
	NemoStorageView *self = data;
	NemoStorageViewDetails *d = self->details;
	TreeNode *hit = d->map_root ? tree_hit (d->map_root, ev->x, ev->y) : NULL;

	if (hit == d->map_root)
		hit = NULL;
	if (hit != d->map_hover) {
		d->map_hover = hit;
		if (hit != NULL) {
			char *sz = g_format_size (hit->size);
			char *tip = g_strdup_printf ("%s\n%s  ·  %.1f%% of %s",
						     hit->is_files ? _("Files and small folders") : (hit->path[0] ? hit->path : hit->name),
						     sz, d->map_root->size > 0 ? 100.0 * hit->size / d->map_root->size : 0.0,
						     d->map_root->name);

			gtk_widget_set_tooltip_text (w, tip);
			g_free (sz); g_free (tip);
		} else {
			gtk_widget_set_tooltip_text (w, NULL);
		}
		gtk_widget_queue_draw (w);
	}
	return FALSE;
}

static gboolean
map_leave (GtkWidget *w, GdkEventCrossing *ev, gpointer data)
{
	NemoStorageView *self = data;

	self->details->map_hover = NULL;
	gtk_widget_queue_draw (w);
	return FALSE;
}

static void load_map (NemoStorageView *self, const char *path);

static gboolean
map_button_press (GtkWidget *w, GdkEventButton *ev, gpointer data)
{
	NemoStorageView *self = data;
	NemoStorageViewDetails *d = self->details;
	TreeNode *hit = d->map_root ? tree_hit (d->map_root, ev->x, ev->y) : NULL;

	if (hit == NULL || hit == d->map_root || hit->is_files || hit->path[0] == '\0')
		return FALSE;
	if (ev->button == 1 && ev->type == GDK_BUTTON_PRESS) {
		char *path = g_strdup (hit->path);

		load_map (self, path);   /* zoom into that folder */
		g_free (path);
	} else if (ev->button == 3) {
		open_path (self, hit->path, FALSE);
	}
	return TRUE;
}

static void
on_tree (NemoStorageView *self, JsonNode *root, gpointer data)
{
	NemoStorageViewDetails *d = self->details;
	JsonObject *o = json_node_get_object (root);
	char *label;

	tree_free (d->map_root);
	d->map_root = tree_from_json (o, 0, 0.0);
	d->map_hover = NULL;
	d->map_w = 0;  /* force relayout on next draw */
	g_free (d->map_path);
	d->map_path = g_strdup (jstr (o, "path"));
	g_free (d->map_parent);
	d->map_parent = json_object_get_null_member (o, "parent") ? NULL : g_strdup (jstr (o, "parent"));
	{
		char *sz = g_format_size (d->map_root->size);

		label = g_strdup_printf ("%s  ·  %s", d->map_path, sz);
		g_free (sz);
	}
	gtk_label_set_text (GTK_LABEL (d->map_path_label), label);
	g_free (label);
	gtk_widget_set_sensitive (d->map_up_button, d->map_parent != NULL);
	gtk_widget_queue_draw (d->map_area);
}

static void
load_map (NemoStorageView *self, const char *path)
{
	call_json (self, "GetTree", g_variant_new ("(si)", path, 3), on_tree, NULL);
}

static void
on_map_up (GtkButton *b, gpointer data)
{
	NemoStorageView *self = data;

	if (self->details->map_parent != NULL)
		load_map (self, self->details->map_parent);
}

static void
on_map_open (GtkButton *b, gpointer data)
{
	NemoStorageView *self = data;

	if (self->details->map_path != NULL)
		open_path (self, self->details->map_path, FALSE);
}

static GtkWidget *
build_map (NemoStorageView *self)
{
	NemoStorageViewDetails *d = self->details;
	GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
	GtkWidget *nav = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	GtkWidget *open, *hint;

	d->map_up_button = gtk_button_new_from_icon_name ("go-up-symbolic", GTK_ICON_SIZE_BUTTON);
	g_signal_connect (d->map_up_button, "clicked", G_CALLBACK (on_map_up), self);
	d->map_path_label = gtk_label_new ("");
	gtk_label_set_ellipsize (GTK_LABEL (d->map_path_label), PANGO_ELLIPSIZE_START);
	gtk_widget_set_halign (d->map_path_label, GTK_ALIGN_START);
	gtk_widget_set_hexpand (d->map_path_label, TRUE);
	open = gtk_button_new_with_mnemonic (_("_Open in Files"));
	g_signal_connect (open, "clicked", G_CALLBACK (on_map_open), self);
	gtk_box_pack_start (GTK_BOX (nav), d->map_up_button, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (nav), d->map_path_label, TRUE, TRUE, 0);
	gtk_box_pack_end (GTK_BOX (nav), open, FALSE, FALSE, 0);

	d->map_area = gtk_drawing_area_new ();
	gtk_widget_set_hexpand (d->map_area, TRUE);
	gtk_widget_set_vexpand (d->map_area, TRUE);
	gtk_widget_set_size_request (d->map_area, 320, 260);
	gtk_widget_add_events (d->map_area, GDK_POINTER_MOTION_MASK | GDK_LEAVE_NOTIFY_MASK |
			       GDK_BUTTON_PRESS_MASK);
	g_signal_connect (d->map_area, "draw", G_CALLBACK (map_draw), self);
	g_signal_connect (d->map_area, "motion-notify-event", G_CALLBACK (map_motion), self);
	g_signal_connect (d->map_area, "leave-notify-event", G_CALLBACK (map_leave), self);
	g_signal_connect (d->map_area, "button-press-event", G_CALLBACK (map_button_press), self);

	hint = gtk_label_new (_("Each rectangle is a folder, sized by disk usage; folders nest inside their parent. "
				"Click to zoom in, right-click to open in Files."));
	gtk_widget_set_halign (hint, GTK_ALIGN_START);
	gtk_style_context_add_class (gtk_widget_get_style_context (hint), "dim-label");
	gtk_box_pack_start (GTK_BOX (box), nav, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box), d->map_area, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (box), hint, FALSE, FALSE, 0);
	return box;
}

/* ------------------------------------------------------------------ */
/* construction                                                       */
/* ------------------------------------------------------------------ */

static GtkWidget *
button_with_tree (const char *label, GCallback cb, gpointer data, GtkWidget *tree)
{
	GtkWidget *b = gtk_button_new_with_mnemonic (label);

	g_object_set_data (G_OBJECT (b), "tree", tree);
	g_signal_connect (b, "clicked", cb, data);
	return b;
}

static GtkWidget *
section_label (const char *text)
{
	GtkWidget *l = gtk_label_new (NULL);
	char *m = g_markup_printf_escaped ("<b>%s</b>", text);

	gtk_label_set_markup (GTK_LABEL (l), m);
	gtk_widget_set_halign (l, GTK_ALIGN_START);
	g_free (m);
	return l;
}

static GtkWidget *
build_overview (NemoStorageView *self)
{
	NemoStorageViewDetails *d = self->details;
	GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
	GtkWidget *cols = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
	GtkWidget *left = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
	GtkWidget *right = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
	GtkWidget *tree;

	d->timeline = gtk_drawing_area_new ();
	gtk_widget_set_size_request (d->timeline, -1, 110);
	g_signal_connect (d->timeline, "draw", G_CALLBACK (timeline_draw), self);
	gtk_box_pack_start (GTK_BOX (box), section_label (_("Used space, last 7 days")), FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box), d->timeline, FALSE, FALSE, 0);

	tree = make_tree (GTK_TREE_MODEL (d->cat_store), _("Type"), _("Size"), _("Files"), FALSE);
	gtk_box_pack_start (GTK_BOX (left), section_label (_("What is using space")), FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (left), scrolled (tree), TRUE, TRUE, 0);

	tree = make_tree (GTK_TREE_MODEL (d->top_store), _("Folder"), _("Size"), _("Files"), FALSE);
	g_signal_connect (tree, "row-activated", G_CALLBACK (on_top_activated), self);
	gtk_box_pack_start (GTK_BOX (right), section_label (_("Biggest folders (double-click to browse)")), FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (right), scrolled (tree), TRUE, TRUE, 0);

	gtk_box_pack_start (GTK_BOX (cols), left, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (cols), right, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (box), cols, TRUE, TRUE, 0);
	return box;
}

static GtkWidget *
build_folders (NemoStorageView *self)
{
	NemoStorageViewDetails *d = self->details;
	GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
	GtkWidget *nav = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	GtkWidget *actions = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	GtkWidget *tree, *open;

	d->folder_up_button = gtk_button_new_from_icon_name ("go-up-symbolic", GTK_ICON_SIZE_BUTTON);
	g_signal_connect (d->folder_up_button, "clicked", G_CALLBACK (on_up_clicked), self);
	d->folder_path_label = gtk_label_new ("");
	gtk_label_set_ellipsize (GTK_LABEL (d->folder_path_label), PANGO_ELLIPSIZE_START);
	gtk_widget_set_halign (d->folder_path_label, GTK_ALIGN_START);
	gtk_widget_set_hexpand (d->folder_path_label, TRUE);
	open = gtk_button_new_with_mnemonic (_("_Open in Files"));
	g_signal_connect (open, "clicked", G_CALLBACK (on_open_folder_clicked), self);
	gtk_box_pack_start (GTK_BOX (nav), d->folder_up_button, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (nav), d->folder_path_label, TRUE, TRUE, 0);
	gtk_box_pack_end (GTK_BOX (nav), open, FALSE, FALSE, 0);

	tree = make_tree (GTK_TREE_MODEL (d->folder_store), _("Name"), _("Size"), _("Files"), FALSE);
	g_signal_connect (tree, "row-activated", G_CALLBACK (on_folder_activated), self);
	attach_tree_actions (self, tree, FALSE, TRUE);

	gtk_box_pack_end (GTK_BOX (actions), button_with_tree (_("Move to _Trash"), G_CALLBACK (on_trash_selected), self, tree), FALSE, FALSE, 0);
	{
		GtkWidget *show = button_with_tree (_("_Show in Files"), G_CALLBACK (on_show_selected), self, tree);

		gtk_box_pack_end (GTK_BOX (actions), show, FALSE, FALSE, 0);
	}
	gtk_box_pack_start (GTK_BOX (box), nav, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box), scrolled (tree), TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (box), actions, FALSE, FALSE, 0);
	return box;
}

static GtkWidget *
build_large (NemoStorageView *self)
{
	NemoStorageViewDetails *d = self->details;
	GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
	GtkWidget *actions = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	GtkWidget *tree = make_tree (GTK_TREE_MODEL (d->large_store), _("File"), _("Size"), _("Type · modified"), TRUE);
	GtkWidget *show;

	attach_tree_actions (self, tree, TRUE, TRUE);

	gtk_box_pack_end (GTK_BOX (actions), button_with_tree (_("Move to _Trash"), G_CALLBACK (on_trash_selected), self, tree), FALSE, FALSE, 0);
	show = button_with_tree (_("_Show in Folder"), G_CALLBACK (on_show_selected), self, tree);
	g_object_set_data (G_OBJECT (show), "select-parent", GINT_TO_POINTER (1));
	gtk_box_pack_end (GTK_BOX (actions), show, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box), section_label (_("Largest files (50 MB and up)")), FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box), scrolled (tree), TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (box), actions, FALSE, FALSE, 0);
	return box;
}

static GtkWidget *
build_cleanup (NemoStorageView *self)
{
	NemoStorageViewDetails *d = self->details;
	GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
	GtkWidget *actions = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	GtkWidget *tree = make_tree (GTK_TREE_MODEL (d->cleanup_store), _("Item"), _("Size"), _("Kind"), TRUE);
	GtkWidget *all, *run;
	GtkCellRenderer *r = gtk_cell_renderer_toggle_new ();
	GtkTreeViewColumn *c = gtk_tree_view_column_new_with_attributes ("", r, "active", COL_CHECK, NULL);

	g_signal_connect (r, "toggled", G_CALLBACK (on_cleanup_toggled), self);
	gtk_tree_view_column_set_cell_data_func (c, r, toggle_cell_data, NULL, NULL);
	gtk_tree_view_insert_column (GTK_TREE_VIEW (tree), c, 0);
	gtk_tree_view_set_expander_column (GTK_TREE_VIEW (tree), gtk_tree_view_get_column (GTK_TREE_VIEW (tree), 1));
	g_signal_connect (tree, "row-expanded", G_CALLBACK (on_cleanup_row_expanded), self);
	g_signal_connect (tree, "row-activated", G_CALLBACK (on_cleanup_row_activated), self);
	attach_tree_actions (self, tree, FALSE, TRUE);

	d->cleanup_total = gtk_label_new ("");
	all = gtk_button_new_with_mnemonic (_("Select _caches and build files"));
	g_signal_connect (all, "clicked", G_CALLBACK (on_cleanup_select_all), self);
	run = gtk_button_new_with_mnemonic (_("_Free up space"));
	gtk_style_context_add_class (gtk_widget_get_style_context (run), "suggested-action");
	g_signal_connect (run, "clicked", G_CALLBACK (on_cleanup_run), self);

	{
		GtkWidget *props = button_with_tree (_("_Properties"), G_CALLBACK (on_properties_selected), self, tree);

		gtk_box_pack_start (GTK_BOX (actions), all, FALSE, FALSE, 0);
		gtk_box_pack_start (GTK_BOX (actions), props, FALSE, FALSE, 0);
	}
	gtk_box_pack_start (GTK_BOX (actions), d->cleanup_total, FALSE, FALSE, 12);
	gtk_box_pack_end (GTK_BOX (actions), run, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box), section_label (_("Safe-to-remove space (expand folders to see what is inside; double-click a file for its properties)")), FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box), scrolled (tree), TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (box), actions, FALSE, FALSE, 0);
	return box;
}

static GtkWidget *
build_growth (NemoStorageView *self)
{
	NemoStorageViewDetails *d = self->details;
	GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
	GtkWidget *bar = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	GtkWidget *b24 = gtk_radio_button_new_with_label (NULL, _("Last 24 hours"));
	GtkWidget *b7 = gtk_radio_button_new_with_label_from_widget (GTK_RADIO_BUTTON (b24), _("Last 7 days"));
	GtkWidget *tree = make_tree (GTK_TREE_MODEL (d->growth_store), _("Folder"), _("Change"), _("Size now"), TRUE);

	g_object_set_data (G_OBJECT (b24), "hours", GINT_TO_POINTER (24));
	g_object_set_data (G_OBJECT (b7), "hours", GINT_TO_POINTER (24 * 7));
	g_signal_connect (b24, "toggled", G_CALLBACK (on_growth_toggled), self);
	g_signal_connect (b7, "toggled", G_CALLBACK (on_growth_toggled), self);
	gtk_box_pack_start (GTK_BOX (bar), section_label (_("What grew")), FALSE, FALSE, 0);
	gtk_box_pack_end (GTK_BOX (bar), b7, FALSE, FALSE, 0);
	gtk_box_pack_end (GTK_BOX (bar), b24, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box), bar, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box), scrolled (tree), TRUE, TRUE, 0);
	return box;
}

static void
proxy_signal_cb (GDBusProxy *proxy, const char *sender, const char *signal,
		 GVariant *params, gpointer data)
{
	NemoStorageView *self = data;

	if (g_strcmp0 (signal, "Changed") == 0 && self->details->refresh_id == 0)
		self->details->refresh_id = g_timeout_add (500, refresh_timeout, self);
}

static void
proxy_ready (GObject *src, GAsyncResult *res, gpointer data)
{
	GError *error = NULL;
	GDBusProxy *proxy = g_dbus_proxy_new_for_bus_finish (res, &error);
	NemoStorageView *self;

	if (proxy == NULL) {
		if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
			self = data;
			gtk_label_set_text (GTK_LABEL (self->details->subtitle),
					    _("The Storage Sense service is not available."));
		}
		g_clear_error (&error);
		return;
	}
	self = data;
	self->details->proxy = proxy;
	g_signal_connect (proxy, "g-signal", G_CALLBACK (proxy_signal_cb), self);
	nemo_storage_view_refresh_all (self);
	call_json (self, "GetStatus", NULL, on_status, NULL);
	self->details->poll_id = g_timeout_add_seconds (2, poll_status, self);
}

static void
nemo_storage_view_build (NemoStorageView *self)
{
	NemoStorageViewDetails *d = self->details;
	GtkWidget *box, *head, *titles, *title, *switcher;

	d->cat_store = make_store ();
	d->top_store = make_store ();
	d->folder_store = make_store ();
	d->large_store = make_store ();
	d->cleanup_store = gtk_tree_store_new (N_COLS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT,
					       G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
					       G_TYPE_BOOLEAN, G_TYPE_INT64);
	d->growth_store = make_store ();
	d->timeline_pts = g_array_new (FALSE, FALSE, sizeof (gint64));
	d->growth_hours = 24;

	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);
	gtk_container_set_border_width (GTK_CONTAINER (box), 14);
	d->root_box = box;

	head = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
	titles = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
	title = gtk_label_new (NULL);
	gtk_label_set_markup (GTK_LABEL (title), "<span size='x-large' weight='bold'>Storage</span>");
	gtk_widget_set_halign (title, GTK_ALIGN_START);
	d->subtitle = gtk_label_new (_("Connecting to the Storage Sense service…"));
	gtk_widget_set_halign (d->subtitle, GTK_ALIGN_START);
	gtk_style_context_add_class (gtk_widget_get_style_context (d->subtitle), "dim-label");
	gtk_box_pack_start (GTK_BOX (titles), title, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (titles), d->subtitle, FALSE, FALSE, 0);
	d->spinner = gtk_spinner_new ();
	d->rescan_button = gtk_button_new_with_mnemonic (_("_Rescan now"));
	g_signal_connect (d->rescan_button, "clicked", G_CALLBACK (on_rescan_clicked), self);
	gtk_box_pack_start (GTK_BOX (head), titles, TRUE, TRUE, 0);
	gtk_box_pack_end (GTK_BOX (head), d->rescan_button, FALSE, FALSE, 0);
	gtk_box_pack_end (GTK_BOX (head), d->spinner, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box), head, FALSE, FALSE, 0);

	d->usage_bar = gtk_progress_bar_new ();
	gtk_progress_bar_set_show_text (GTK_PROGRESS_BAR (d->usage_bar), TRUE);
	gtk_box_pack_start (GTK_BOX (box), d->usage_bar, FALSE, FALSE, 0);

	d->stack = gtk_stack_new ();
	gtk_stack_set_transition_type (GTK_STACK (d->stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
	gtk_stack_add_titled (GTK_STACK (d->stack), build_overview (self), "overview", _("Overview"));
	gtk_stack_add_titled (GTK_STACK (d->stack), build_map (self), "map", _("Map"));
	gtk_stack_add_titled (GTK_STACK (d->stack), build_folders (self), "folders", _("Folders"));
	gtk_stack_add_titled (GTK_STACK (d->stack), build_large (self), "large", _("Large files"));
	gtk_stack_add_titled (GTK_STACK (d->stack), build_cleanup (self), "cleanup", _("Clean up"));
	gtk_stack_add_titled (GTK_STACK (d->stack), build_growth (self), "growth", _("Growth"));
	switcher = gtk_stack_switcher_new ();
	gtk_stack_switcher_set_stack (GTK_STACK_SWITCHER (switcher), GTK_STACK (d->stack));
	gtk_widget_set_halign (switcher, GTK_ALIGN_START);
	gtk_box_pack_start (GTK_BOX (box), switcher, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box), d->stack, TRUE, TRUE, 0);

	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (self), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_container_add (GTK_CONTAINER (self), box);
	gtk_widget_show_all (GTK_WIDGET (self));

	d->cancellable = g_cancellable_new ();
	g_dbus_proxy_new_for_bus (G_BUS_TYPE_SESSION, G_DBUS_PROXY_FLAGS_NONE, NULL,
				  SS_BUS_NAME, SS_OBJ_PATH, SS_IFACE,
				  d->cancellable, proxy_ready, self);
}

/* ------------------------------------------------------------------ */
/* NemoView boilerplate: this view never lists files                  */
/* ------------------------------------------------------------------ */

static void   sv_add_file (NemoView *v, NemoFile *f, NemoDirectory *d) { }
static void   sv_nop (NemoView *v) { }
static void   sv_file_changed (NemoView *v, NemoFile *f, NemoDirectory *d) { }
static GList *sv_get_selection (NemoView *v) { return NULL; }
static guint  sv_item_count (NemoView *v) { return 0; }
static gboolean sv_is_empty (NemoView *v) { return TRUE; }
static void   sv_remove_file (NemoView *v, NemoFile *f, NemoDirectory *d) { }
static void   sv_set_selection (NemoView *v, GList *s) { nemo_view_notify_selection_changed (v); }
static void   sv_merge_menus (NemoView *v) { NEMO_VIEW_CLASS (nemo_storage_view_parent_class)->merge_menus (v); }
static void   sv_update_menus (NemoView *v) { NEMO_VIEW_CLASS (nemo_storage_view_parent_class)->update_menus (v); }
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
/* x-fileharbor-storage:/// is virtual: the directory load always fails, which is expected */
static void   sv_load_error (NemoView *v, GError *e) { }
static const char *sv_get_id (NemoView *v) { return NEMO_STORAGE_VIEW_ID; }

static void
nemo_storage_view_dispose (GObject *object)
{
	NemoStorageView *self = NEMO_STORAGE_VIEW (object);
	NemoStorageViewDetails *d = self->details;

	if (d->cancellable != NULL) {
		g_cancellable_cancel (d->cancellable);
		g_clear_object (&d->cancellable);
	}
	if (d->refresh_id != 0) {
		g_source_remove (d->refresh_id);
		d->refresh_id = 0;
	}
	if (d->poll_id != 0) {
		g_source_remove (d->poll_id);
		d->poll_id = 0;
	}
	g_clear_object (&d->proxy);
	G_OBJECT_CLASS (nemo_storage_view_parent_class)->dispose (object);
}

static void
nemo_storage_view_finalize (GObject *object)
{
	NemoStorageViewDetails *d = NEMO_STORAGE_VIEW (object)->details;

	g_clear_object (&d->cat_store);
	g_clear_object (&d->top_store);
	g_clear_object (&d->folder_store);
	g_clear_object (&d->large_store);
	g_clear_object (&d->cleanup_store);
	g_clear_object (&d->growth_store);
	if (d->timeline_pts != NULL)
		g_array_free (d->timeline_pts, TRUE);
	g_free (d->folder_path);
	g_free (d->folder_parent);
	g_free (d->root_path);
	tree_free (d->map_root);
	g_free (d->map_path);
	g_free (d->map_parent);
	G_OBJECT_CLASS (nemo_storage_view_parent_class)->finalize (object);
}

static void
nemo_storage_view_class_init (NemoStorageViewClass *klass)
{
	GObjectClass *oc = G_OBJECT_CLASS (klass);
	NemoViewClass *vc = NEMO_VIEW_CLASS (klass);

	g_type_class_add_private (klass, sizeof (NemoStorageViewDetails));
	oc->dispose = nemo_storage_view_dispose;
	oc->finalize = nemo_storage_view_finalize;

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
	vc->load_error = sv_load_error;
	vc->get_first_visible_file = sv_first_visible;
	vc->scroll_to_file = sv_scroll_to;
}

static void
nemo_storage_view_init (NemoStorageView *self)
{
	self->details = G_TYPE_INSTANCE_GET_PRIVATE (self, NEMO_TYPE_STORAGE_VIEW,
						     NemoStorageViewDetails);
	nemo_storage_view_build (self);
}

static NemoView *
nemo_storage_view_create (NemoWindowSlot *slot)
{
	return NEMO_VIEW (g_object_new (NEMO_TYPE_STORAGE_VIEW, "window-slot", slot, NULL));
}

gboolean
nemo_uri_is_virtual_page (const char *uri)
{
	return nemo_storage_uri_is_storage (uri) ||
	       (uri != NULL && (g_str_has_prefix (uri, "x-fileharbor-page:") || g_str_has_prefix (uri, "x-fileharbor-tool:")));
}

gboolean
nemo_storage_uri_is_storage (const char *uri)
{
	return uri != NULL && g_str_has_prefix (uri, "x-fileharbor-storage:");
}

static gboolean
nemo_storage_view_supports_uri (const char *uri, GFileType file_type, const char *mime_type)
{
	return nemo_storage_uri_is_storage (uri);
}

static NemoViewInfo nemo_storage_view = {
	NEMO_STORAGE_VIEW_ID,
	"Storage",
	"Storage",
	"_Storage",
	"The storage page encountered an error.",
	"Display this location with the storage page.",
	nemo_storage_view_create,
	nemo_storage_view_supports_uri
};

void
nemo_storage_view_register (void)
{
	nemo_view_factory_register (&nemo_storage_view);
}

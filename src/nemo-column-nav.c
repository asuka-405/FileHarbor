/* nemo-column-nav.c - Miller-column folder navigator. New file, not in upstream Nemo.
 *
 * Shows the ancestors of the current folder, each as a column of its folders,
 * so you can see and jump to siblings at every level. The current folder's
 * own contents are the main view and are deliberately not repeated. */

#include <config.h>

#include "nemo-column-nav.h"

#include "nemo-window.h"
#include "nemo-window-slot.h"
#include "nemo-storage-view.h"

#include <glib/gi18n.h>
#include <string.h>

#define COLUMN_WIDTH 200

enum { C_NAME, C_PATH, C_ICON, C_N };

typedef struct {
	char *path;               /* the folder whose sub-folders this column lists */
	GtkWidget *frame;
	GtkWidget *tree;
	GtkListStore *store;
} Column;

struct _NemoColumnNav {
	GtkBox parent_instance;

	NemoWindow *window;
	GtkWidget *places;        /* quick-access icon row */
	GtkWidget *scroller;
	GtkWidget *cols;          /* horizontal box of columns */
	GPtrArray *columns;       /* Column* */
	char *current;
	gulong loading_id;
	GVolumeMonitor *monitor;
	guint scroll_idle;
};

G_DEFINE_TYPE (NemoColumnNav, nemo_column_nav, GTK_TYPE_BOX)

/* ------------------------------------------------------------------ */
/* navigation                                                         */
/* ------------------------------------------------------------------ */

static void
go_to_uri (NemoColumnNav *self, const char *uri, gboolean new_tab)
{
	NemoWindowSlot *slot = nemo_window_get_active_slot (self->window);
	GFile *f = g_file_new_for_uri (uri);

	if (slot != NULL)
		nemo_window_slot_open_location (slot, f, new_tab ? NEMO_WINDOW_OPEN_FLAG_NEW_TAB : 0);
	g_object_unref (f);
}

static void
go_to_path (NemoColumnNav *self, const char *path, gboolean new_tab)
{
	char *uri = g_filename_to_uri (path, NULL, NULL);

	if (uri != NULL)
		go_to_uri (self, uri, new_tab);
	g_free (uri);
}

/* ------------------------------------------------------------------ */
/* quick-access row                                                   */
/* ------------------------------------------------------------------ */

static void
place_clicked (GtkButton *b, gpointer data)
{
	go_to_uri (data, g_object_get_data (G_OBJECT (b), "uri"), FALSE);
}

static gboolean
place_pressed (GtkWidget *b, GdkEventButton *ev, gpointer data)
{
	if (ev->button == 2) {      /* middle click: new tab */
		go_to_uri (data, g_object_get_data (G_OBJECT (b), "uri"), TRUE);
		return TRUE;
	}
	return FALSE;
}

static void
add_place (NemoColumnNav *self, GtkWidget *flow, const char *icon, const char *tip, const char *uri)
{
	GtkWidget *b = gtk_button_new_from_icon_name (icon, GTK_ICON_SIZE_LARGE_TOOLBAR);

	gtk_button_set_relief (GTK_BUTTON (b), GTK_RELIEF_NONE);
	gtk_widget_set_tooltip_text (b, tip);
	gtk_widget_set_focus_on_click (b, FALSE);
	gtk_style_context_add_class (gtk_widget_get_style_context (b), "fileharbor-place");
	g_object_set_data_full (G_OBJECT (b), "uri", g_strdup (uri), g_free);
	g_signal_connect (b, "clicked", G_CALLBACK (place_clicked), self);
	g_signal_connect (b, "button-press-event", G_CALLBACK (place_pressed), self);
	gtk_container_add (GTK_CONTAINER (flow), b);
	gtk_widget_show (b);
}

static void
add_user_dir (NemoColumnNav *self, GtkWidget *flow, GUserDirectory dir, const char *icon, const char *tip)
{
	const char *p = g_get_user_special_dir (dir);
	char *uri;

	if (p == NULL || g_strcmp0 (p, g_get_home_dir ()) == 0)
		return;
	uri = g_filename_to_uri (p, NULL, NULL);
	add_place (self, flow, icon, tip, uri);
	g_free (uri);
}

static void
rebuild_places (NemoColumnNav *self)
{
	GList *kids, *l, *mounts;
	char *home_uri = g_filename_to_uri (g_get_home_dir (), NULL, NULL);

	kids = gtk_container_get_children (GTK_CONTAINER (self->places));
	for (l = kids; l != NULL; l = l->next)
		gtk_widget_destroy (l->data);
	g_list_free (kids);

	add_place (self, self->places, "user-home-symbolic", _("Home"), home_uri);
	add_user_dir (self, self->places, G_USER_DIRECTORY_DOCUMENTS, "folder-documents-symbolic", _("Documents"));
	add_user_dir (self, self->places, G_USER_DIRECTORY_DOWNLOAD, "folder-download-symbolic", _("Downloads"));
	add_user_dir (self, self->places, G_USER_DIRECTORY_PICTURES, "folder-pictures-symbolic", _("Pictures"));
	add_user_dir (self, self->places, G_USER_DIRECTORY_MUSIC, "folder-music-symbolic", _("Music"));
	add_user_dir (self, self->places, G_USER_DIRECTORY_VIDEOS, "folder-videos-symbolic", _("Videos"));
	add_place (self, self->places, "drive-harddisk-symbolic", _("File System"), "file:///");

	mounts = g_volume_monitor_get_mounts (self->monitor);
	for (l = mounts; l != NULL; l = l->next) {
		GMount *m = l->data;
		GFile *root;
		char *uri, *name;
		GIcon *icon;

		if (g_mount_is_shadowed (m))
			continue;
		root = g_mount_get_default_location (m);
		uri = g_file_get_uri (root);
		name = g_mount_get_name (m);
		icon = g_mount_get_symbolic_icon (m);
		{
			GtkWidget *b = gtk_button_new ();
			GtkWidget *img = gtk_image_new_from_gicon (icon, GTK_ICON_SIZE_LARGE_TOOLBAR);

			gtk_container_add (GTK_CONTAINER (b), img);
			gtk_button_set_relief (GTK_BUTTON (b), GTK_RELIEF_NONE);
			gtk_widget_set_tooltip_text (b, name);
			gtk_widget_set_focus_on_click (b, FALSE);
			gtk_style_context_add_class (gtk_widget_get_style_context (b), "fileharbor-place");
			g_object_set_data_full (G_OBJECT (b), "uri", g_strdup (uri), g_free);
			g_signal_connect (b, "clicked", G_CALLBACK (place_clicked), self);
			gtk_container_add (GTK_CONTAINER (self->places), b);
			gtk_widget_show_all (b);
		}
		g_free (uri); g_free (name);
		g_object_unref (icon); g_object_unref (root);
	}
	g_list_free_full (mounts, g_object_unref);

	add_place (self, self->places, "user-trash-symbolic", _("Trash"), "trash:///");
	add_place (self, self->places, "drive-multidisk-symbolic", _("Storage: what is using space"), NEMO_STORAGE_URI);
	g_free (home_uri);
}

static void
monitor_changed (GVolumeMonitor *vm, gpointer obj, gpointer data)
{
	rebuild_places (data);
}

/* ------------------------------------------------------------------ */
/* columns                                                            */
/* ------------------------------------------------------------------ */

static void
column_free (Column *c)
{
	if (c->frame != NULL)
		gtk_widget_destroy (c->frame);
	g_free (c->path);
	g_free (c);
}

static void
fill_column (Column *c)
{
	GFile *dir = g_file_new_for_path (c->path);
	GFileEnumerator *en = g_file_enumerate_children (dir,
		"standard::name,standard::type,standard::is-hidden,standard::symbolic-icon",
		G_FILE_QUERY_INFO_NONE, NULL, NULL);
	GPtrArray *names = g_ptr_array_new_with_free_func (g_free);
	GHashTable *icons = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
	GFileInfo *info;
	guint i;

	while (en != NULL && names->len < 3000 && (info = g_file_enumerator_next_file (en, NULL, NULL)) != NULL) {
		if (g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY && !g_file_info_get_is_hidden (info)) {
			const char *n = g_file_info_get_name (info);
			GIcon *ic = g_file_info_get_symbolic_icon (info);

			g_ptr_array_add (names, g_strdup (n));
			if (ic != NULL)
				g_hash_table_insert (icons, g_strdup (n), g_object_ref (ic));
		}
		g_object_unref (info);
	}
	g_clear_object (&en);
	g_object_unref (dir);

	{
		/* case-insensitive, locale-aware order */
		GPtrArray *keyed = g_ptr_array_new_with_free_func (g_free);
		GHashTable *key_to_name = g_hash_table_new (g_str_hash, g_str_equal);
		GList *keys = NULL, *l;

		for (i = 0; i < names->len; i++) {
			char *key = g_utf8_collate_key_for_filename (g_ptr_array_index (names, i), -1);

			g_ptr_array_add (keyed, key);
			g_hash_table_insert (key_to_name, key, g_ptr_array_index (names, i));
			keys = g_list_prepend (keys, key);
		}
		keys = g_list_sort (keys, (GCompareFunc) strcmp);
		for (l = keys; l != NULL; l = l->next) {
			const char *name = g_hash_table_lookup (key_to_name, l->data);
			char *full = g_build_filename (c->path, name, NULL);
			GtkTreeIter it;

			gtk_list_store_append (c->store, &it);
			gtk_list_store_set (c->store, &it, C_NAME, name, C_PATH, full,
					    C_ICON, g_hash_table_lookup (icons, name), -1);
			g_free (full);
		}
		g_list_free (keys);
		g_hash_table_destroy (key_to_name);
		g_ptr_array_free (keyed, TRUE);
	}
	g_hash_table_destroy (icons);
	g_ptr_array_free (names, TRUE);
}

static gboolean
column_release (GtkWidget *w, GdkEventButton *ev, gpointer data)
{
	NemoColumnNav *self = data;
	GtkTreePath *tp = NULL;
	GtkTreeModel *m = gtk_tree_view_get_model (GTK_TREE_VIEW (w));
	GtkTreeIter it;

	if (ev->window != gtk_tree_view_get_bin_window (GTK_TREE_VIEW (w)) || (ev->button != 1 && ev->button != 2))
		return FALSE;
	if (gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (w), ev->x, ev->y, &tp, NULL, NULL, NULL)) {
		if (gtk_tree_model_get_iter (m, &it, tp)) {
			char *path = NULL;

			gtk_tree_model_get (m, &it, C_PATH, &path, -1);
			if (path != NULL)
				go_to_path (self, path, ev->button == 2);
			g_free (path);
		}
		gtk_tree_path_free (tp);
	}
	return FALSE;
}

static void
column_row_activated (GtkTreeView *tv, GtkTreePath *tp, GtkTreeViewColumn *c, gpointer data)
{
	GtkTreeModel *m = gtk_tree_view_get_model (tv);
	GtkTreeIter it;
	char *path = NULL;

	if (gtk_tree_model_get_iter (m, &it, tp)) {
		gtk_tree_model_get (m, &it, C_PATH, &path, -1);
		if (path != NULL)
			go_to_path (data, path, FALSE);
		g_free (path);
	}
}

static Column *
column_new (NemoColumnNav *self, const char *path)
{
	Column *c = g_new0 (Column, 1);
	GtkCellRenderer *r;
	GtkTreeViewColumn *tc;
	GtkWidget *sw;
	char *title;

	c->path = g_strdup (path);
	c->store = gtk_list_store_new (C_N, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_ICON);
	fill_column (c);

	c->tree = gtk_tree_view_new_with_model (GTK_TREE_MODEL (c->store));
	g_object_unref (c->store);
	gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (c->tree), TRUE);
	gtk_tree_view_set_enable_search (GTK_TREE_VIEW (c->tree), TRUE);
	gtk_tree_view_set_search_column (GTK_TREE_VIEW (c->tree), C_NAME);
	gtk_style_context_add_class (gtk_widget_get_style_context (c->tree), "fileharbor-column");

	title = g_strcmp0 (path, "/") == 0 ? g_strdup ("/") : g_path_get_basename (path);
	tc = gtk_tree_view_column_new ();
	gtk_tree_view_column_set_title (tc, title);
	g_free (title);
	r = gtk_cell_renderer_pixbuf_new ();
	g_object_set (r, "stock-size", GTK_ICON_SIZE_MENU, NULL);
	gtk_tree_view_column_pack_start (tc, r, FALSE);
	gtk_tree_view_column_add_attribute (tc, r, "gicon", C_ICON);
	r = gtk_cell_renderer_text_new ();
	g_object_set (r, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
	gtk_tree_view_column_pack_start (tc, r, TRUE);
	gtk_tree_view_column_add_attribute (tc, r, "text", C_NAME);
	gtk_tree_view_column_set_expand (tc, TRUE);
	gtk_tree_view_append_column (GTK_TREE_VIEW (c->tree), tc);

	g_signal_connect (c->tree, "button-release-event", G_CALLBACK (column_release), self);
	g_signal_connect (c->tree, "row-activated", G_CALLBACK (column_row_activated), self);

	sw = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_size_request (sw, COLUMN_WIDTH, -1);
	gtk_container_add (GTK_CONTAINER (sw), c->tree);
	c->frame = sw;
	gtk_box_pack_start (GTK_BOX (self->cols), sw, FALSE, FALSE, 0);
	gtk_widget_show_all (sw);
	return c;
}

static void
select_in_column (Column *c, const char *child_path)
{
	GtkTreeModel *m = GTK_TREE_MODEL (c->store);
	GtkTreeSelection *sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (c->tree));
	GtkTreeIter it;

	gtk_tree_selection_unselect_all (sel);
	if (child_path == NULL || !gtk_tree_model_get_iter_first (m, &it))
		return;
	do {
		char *p = NULL;

		gtk_tree_model_get (m, &it, C_PATH, &p, -1);
		if (g_strcmp0 (p, child_path) == 0) {
			GtkTreePath *tp = gtk_tree_model_get_path (m, &it);

			gtk_tree_selection_select_iter (sel, &it);
			gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (c->tree), tp, NULL, TRUE, 0.4, 0);
			gtk_tree_path_free (tp);
			g_free (p);
			return;
		}
		g_free (p);
	} while (gtk_tree_model_iter_next (m, &it));
}

static gboolean
scroll_to_end (gpointer data)
{
	NemoColumnNav *self = data;
	GtkAdjustment *adj = gtk_scrolled_window_get_hadjustment (GTK_SCROLLED_WINDOW (self->scroller));

	self->scroll_idle = 0;
	gtk_adjustment_set_value (adj, gtk_adjustment_get_upper (adj) - gtk_adjustment_get_page_size (adj));
	return G_SOURCE_REMOVE;
}

static void
update_columns (NemoColumnNav *self, const char *path)
{
	GPtrArray *chain = g_ptr_array_new_with_free_func (g_free);
	char *p = g_strdup (path);
	guint i, keep = 0, ncols;

	/* "/", "/home", "/home/me", ... , path */
	while (TRUE) {
		char *parent;

		g_ptr_array_insert (chain, 0, g_strdup (p));
		if (g_strcmp0 (p, "/") == 0)
			break;
		parent = g_path_get_dirname (p);
		g_free (p);
		p = parent;
	}
	g_free (p);

	/* One column per ancestor, each showing that folder's sub-folders with the
	 * next step of the path highlighted. The current folder's own contents are
	 * not repeated here: they are already the main view. At "/" there is no
	 * ancestor, so its single column is shown. */
	ncols = chain->len > 1 ? chain->len - 1 : 1;

	/* reuse leading columns that are unchanged */
	while (keep < self->columns->len && keep < ncols &&
	       g_strcmp0 (((Column *) g_ptr_array_index (self->columns, keep))->path, g_ptr_array_index (chain, keep)) == 0)
		keep++;
	while (self->columns->len > keep)
		g_ptr_array_remove_index (self->columns, self->columns->len - 1);
	for (i = keep; i < ncols; i++)
		g_ptr_array_add (self->columns, column_new (self, g_ptr_array_index (chain, i)));

	for (i = 0; i < self->columns->len; i++)
		select_in_column (g_ptr_array_index (self->columns, i),
				  i + 1 < chain->len ? g_ptr_array_index (chain, i + 1) : NULL);

	if (keep != ncols && self->scroll_idle == 0)
		self->scroll_idle = g_idle_add (scroll_to_end, self);
	g_ptr_array_free (chain, TRUE);
}

static void
on_loading_uri (NemoWindow *window, const char *uri, gpointer data)
{
	NemoColumnNav *self = data;
	GFile *f;
	char *path;

	if (uri == NULL || nemo_uri_is_virtual_page (uri))
		return;
	f = g_file_new_for_uri (uri);
	path = g_file_get_path (f);
	g_object_unref (f);
	if (path == NULL)
		return;             /* trash:///, search, remote: keep what is shown */
	g_free (self->current);
	self->current = path;
	update_columns (self, path);
}

/* ------------------------------------------------------------------ */

static void
nemo_column_nav_dispose (GObject *object)
{
	NemoColumnNav *self = NEMO_COLUMN_NAV (object);

	if (self->scroll_idle != 0) {
		g_source_remove (self->scroll_idle);
		self->scroll_idle = 0;
	}
	if (self->window != NULL && self->loading_id != 0) {
		g_signal_handler_disconnect (self->window, self->loading_id);
		self->loading_id = 0;
	}
	if (self->monitor != NULL) {
		g_signal_handlers_disconnect_by_data (self->monitor, self);
		g_clear_object (&self->monitor);
	}
	if (self->columns != NULL) {
		g_ptr_array_free (self->columns, TRUE);
		self->columns = NULL;
	}
	G_OBJECT_CLASS (nemo_column_nav_parent_class)->dispose (object);
}

static void
nemo_column_nav_finalize (GObject *object)
{
	g_free (NEMO_COLUMN_NAV (object)->current);
	G_OBJECT_CLASS (nemo_column_nav_parent_class)->finalize (object);
}

static void
nemo_column_nav_class_init (NemoColumnNavClass *klass)
{
	G_OBJECT_CLASS (klass)->dispose = nemo_column_nav_dispose;
	G_OBJECT_CLASS (klass)->finalize = nemo_column_nav_finalize;
}

static void
nemo_column_nav_init (NemoColumnNav *self)
{
	gtk_orientable_set_orientation (GTK_ORIENTABLE (self), GTK_ORIENTATION_VERTICAL);
	gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (self)), "fileharbor-columnnav");
	self->columns = g_ptr_array_new_with_free_func ((GDestroyNotify) column_free);

	self->places = gtk_flow_box_new ();
	gtk_flow_box_set_selection_mode (GTK_FLOW_BOX (self->places), GTK_SELECTION_NONE);
	gtk_flow_box_set_homogeneous (GTK_FLOW_BOX (self->places), FALSE);
	gtk_flow_box_set_max_children_per_line (GTK_FLOW_BOX (self->places), 20);
	gtk_box_pack_start (GTK_BOX (self), self->places, FALSE, FALSE, 0);

	self->scroller = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (self->scroller), GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
	self->cols = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_container_add (GTK_CONTAINER (self->scroller), self->cols);
	gtk_widget_set_vexpand (self->scroller, TRUE);
	gtk_box_pack_start (GTK_BOX (self), self->scroller, TRUE, TRUE, 0);
	gtk_widget_set_size_request (GTK_WIDGET (self), COLUMN_WIDTH * 2 + 20, -1);

	self->monitor = g_volume_monitor_get ();
	g_signal_connect (self->monitor, "mount-added", G_CALLBACK (monitor_changed), self);
	g_signal_connect (self->monitor, "mount-removed", G_CALLBACK (monitor_changed), self);
	g_signal_connect (self->monitor, "mount-changed", G_CALLBACK (monitor_changed), self);
	gtk_widget_show_all (self->places);
	gtk_widget_show_all (self->scroller);
}

GtkWidget *
nemo_column_nav_new (NemoWindow *window)
{
	NemoColumnNav *self = g_object_new (NEMO_TYPE_COLUMN_NAV, NULL);
	NemoWindowSlot *slot;
	GFile *loc;

	self->window = window;
	rebuild_places (self);
	self->loading_id = g_signal_connect (window, "loading_uri", G_CALLBACK (on_loading_uri), self);

	slot = nemo_window_get_active_slot (window);
	loc = slot != NULL ? nemo_window_slot_get_location (slot) : NULL;
	if (loc != NULL) {
		char *uri = g_file_get_uri (loc);

		on_loading_uri (window, uri, self);
		g_free (uri);
	}
	return GTK_WIDGET (self);
}

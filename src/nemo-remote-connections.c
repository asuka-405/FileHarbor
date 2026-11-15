/* nemo-remote-connections.c - saved remote locations and a simple dialog to
 * make new ones. New file, not in upstream Nemo. */

#include <config.h>

#include "nemo-remote-connections.h"
#include "nemo-window.h"
#include "nemo-window-slot.h"

#include <glib/gi18n.h>
#include <string.h>

typedef struct {
	const char *id;        /* combo id */
	const char *label;
	const char *scheme;
	int default_port;
} Protocol;

static const Protocol protocols[] = {
	{ "sftp",   N_("SFTP (SSH file transfer)"), "sftp", 22 },
	{ "ftp",    N_("FTP"),                      "ftp",  21 },
	{ "ftps",   N_("FTPS (FTP over TLS)"),      "ftps", 21 },
	{ "dav",    N_("WebDAV"),                   "dav",  80 },
	{ "davs",   N_("WebDAV (secure, HTTPS)"),   "davs", 443 },
	{ "smb",    N_("Windows share (SMB)"),      "smb",  0 },
	{ "nfs",    N_("NFS"),                      "nfs",  0 },
	{ "custom", N_("Other (type a full address)"), "",  0 },
};

/* ------------------------------------------------------------------ */
/* storage                                                            */
/* ------------------------------------------------------------------ */

char *
nemo_remote_connections_get_path (void)
{
	return g_build_filename (g_get_user_config_dir (), "fileharbor", "connections.ini", NULL);
}

GList *
nemo_remote_connections_load (void)
{
	GKeyFile *kf = g_key_file_new ();
	char *path = nemo_remote_connections_get_path ();
	GList *out = NULL;

	if (g_key_file_load_from_file (kf, path, G_KEY_FILE_NONE, NULL)) {
		gsize n, i;
		char **groups = g_key_file_get_groups (kf, &n);

		for (i = 0; i < n; i++) {
			NemoRemoteConnection *c = g_new0 (NemoRemoteConnection, 1);

			c->name = g_key_file_get_string (kf, groups[i], "Name", NULL);
			c->uri = g_key_file_get_string (kf, groups[i], "Uri", NULL);
			if (c->name == NULL || c->uri == NULL) {
				g_free (c->name); g_free (c->uri); g_free (c);
				continue;
			}
			out = g_list_append (out, c);
		}
		g_strfreev (groups);
	}
	g_free (path);
	g_key_file_free (kf);
	return out;
}

void
nemo_remote_connections_free (GList *list)
{
	GList *l;

	for (l = list; l != NULL; l = l->next) {
		NemoRemoteConnection *c = l->data;

		g_free (c->name);
		g_free (c->uri);
		g_free (c);
	}
	g_list_free (list);
}

static void
save_list (GList *list)
{
	GKeyFile *kf = g_key_file_new ();
	char *path = nemo_remote_connections_get_path ();
	char *dir = g_path_get_dirname (path);
	GList *l;
	int i = 0;

	for (l = list; l != NULL; l = l->next, i++) {
		NemoRemoteConnection *c = l->data;
		char *group = g_strdup_printf ("connection-%d", i);

		g_key_file_set_string (kf, group, "Name", c->name);
		g_key_file_set_string (kf, group, "Uri", c->uri);
		g_free (group);
	}
	g_mkdir_with_parents (dir, 0700);
	g_key_file_save_to_file (kf, path, NULL);
	g_free (dir); g_free (path);
	g_key_file_free (kf);
}

/* ------------------------------------------------------------------ */
/* dialog                                                             */
/* ------------------------------------------------------------------ */

typedef struct {
	GtkWidget *window;
	GtkWindow *parent;
	GtkWidget *proto, *server, *port, *folder, *user, *name, *list;
	GtkWidget *server_label, *user_label, *port_label;
	GtkListStore *store;
} Dlg;

static Dlg *dlg = NULL;

static const Protocol *
current_protocol (void)
{
	const char *id = gtk_combo_box_get_active_id (GTK_COMBO_BOX (dlg->proto));
	guint i;

	for (i = 0; i < G_N_ELEMENTS (protocols); i++)
		if (g_strcmp0 (protocols[i].id, id) == 0)
			return &protocols[i];
	return &protocols[0];
}

static char *
build_uri (void)
{
	const Protocol *p = current_protocol ();
	const char *server = gtk_entry_get_text (GTK_ENTRY (dlg->server));
	const char *port = gtk_entry_get_text (GTK_ENTRY (dlg->port));
	const char *user = gtk_entry_get_text (GTK_ENTRY (dlg->user));
	const char *folder = gtk_entry_get_text (GTK_ENTRY (dlg->folder));
	char *s_server = g_strstrip (g_strdup (server));
	char *uri, *eu, *ef;

	if (s_server[0] == '\0') {
		g_free (s_server);
		return NULL;
	}
	if (p->scheme[0] == '\0') {              /* custom: the user typed a whole address */
		uri = strstr (s_server, "://") != NULL ? g_strdup (s_server) : g_strdup_printf ("%s", s_server);
		g_free (s_server);
		return uri;
	}
	eu = g_uri_escape_string (user, NULL, FALSE);
	{
		char *fixed = folder[0] == '/' ? g_strdup (folder) : g_strdup_printf ("/%s", folder);

		ef = g_uri_escape_string (fixed, G_URI_RESERVED_CHARS_ALLOWED_IN_PATH, FALSE);
		g_free (fixed);
	}
	uri = g_strdup_printf ("%s://%s%s%s%s%s%s", p->scheme,
			       eu[0] ? eu : "", eu[0] ? "@" : "", s_server,
			       port[0] ? ":" : "", port[0] ? port : "",
			       strcmp (ef, "/") == 0 && folder[0] == '\0' ? "/" : ef);
	g_free (eu); g_free (ef); g_free (s_server);
	return uri;
}

static void
refresh_list (void)
{
	GList *list = nemo_remote_connections_load (), *l;

	gtk_list_store_clear (dlg->store);
	for (l = list; l != NULL; l = l->next) {
		NemoRemoteConnection *c = l->data;
		GtkTreeIter it;

		gtk_list_store_append (dlg->store, &it);
		gtk_list_store_set (dlg->store, &it, 0, c->name, 1, c->uri, -1);
	}
	nemo_remote_connections_free (list);
}

static void
open_uri (const char *uri)
{
	NemoWindowSlot *slot = nemo_window_get_active_slot (NEMO_WINDOW (dlg->parent));
	GFile *f = g_file_new_for_uri (uri);

	if (slot != NULL)
		nemo_window_slot_open_location (slot, f, 0);
	g_object_unref (f);
}

static void
on_connect (GtkButton *b, gpointer data)
{
	char *uri = build_uri ();

	if (uri != NULL)
		open_uri (uri);          /* nemo mounts it and asks for the password itself */
	g_free (uri);
}

static void
on_save (GtkButton *b, gpointer data)
{
	char *uri = build_uri ();
	const char *name = gtk_entry_get_text (GTK_ENTRY (dlg->name));
	GList *list;
	NemoRemoteConnection *c;

	if (uri == NULL)
		return;
	list = nemo_remote_connections_load ();
	c = g_new0 (NemoRemoteConnection, 1);
	c->uri = uri;
	if (name[0] != '\0') {
		c->name = g_strdup (name);
	} else {
		GFile *f = g_file_new_for_uri (uri);
		char *parse = g_file_get_parse_name (f);

		c->name = parse;
		g_object_unref (f);
	}
	list = g_list_append (list, c);
	save_list (list);
	nemo_remote_connections_free (list);
	gtk_entry_set_text (GTK_ENTRY (dlg->name), "");
	refresh_list ();
}

static void
on_remove (GtkButton *b, gpointer data)
{
	GtkTreeSelection *sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (dlg->list));
	GtkTreeModel *m;
	GtkTreeIter it;
	char *uri = NULL;

	if (!gtk_tree_selection_get_selected (sel, &m, &it))
		return;
	gtk_tree_model_get (m, &it, 1, &uri, -1);
	{
		GList *list = nemo_remote_connections_load (), *l, *keep = NULL;

		for (l = list; l != NULL; l = l->next) {
			NemoRemoteConnection *c = l->data;

			if (g_strcmp0 (c->uri, uri) == 0)
				continue;
			keep = g_list_append (keep, c);
		}
		save_list (keep);
		g_list_free (keep);
		nemo_remote_connections_free (list);
	}
	g_free (uri);
	refresh_list ();
}

static void
on_row_activated (GtkTreeView *tv, GtkTreePath *p, GtkTreeViewColumn *c, gpointer data)
{
	GtkTreeIter it;
	char *uri = NULL;

	if (gtk_tree_model_get_iter (GTK_TREE_MODEL (dlg->store), &it, p)) {
		gtk_tree_model_get (GTK_TREE_MODEL (dlg->store), &it, 1, &uri, -1);
		if (uri != NULL)
			open_uri (uri);
		g_free (uri);
	}
}

static void
on_protocol_changed (GtkComboBox *c, gpointer data)
{
	const Protocol *p = current_protocol ();
	gboolean custom = p->scheme[0] == '\0';
	char *placeholder = p->default_port > 0 ? g_strdup_printf ("%d", p->default_port) : g_strdup (_("default"));

	gtk_label_set_text (GTK_LABEL (dlg->server_label), custom ? _("Address") : _("Server"));
	gtk_entry_set_placeholder_text (GTK_ENTRY (dlg->server),
					custom ? "sftp://user@host/folder" : (strcmp (p->scheme, "smb") == 0 ? "server-name" : "example.com"));
	gtk_entry_set_placeholder_text (GTK_ENTRY (dlg->port), placeholder);
	g_free (placeholder);
	gtk_widget_set_visible (dlg->port_label, !custom);
	gtk_widget_set_visible (dlg->port, !custom);
	gtk_widget_set_visible (dlg->user_label, !custom);
	gtk_widget_set_visible (dlg->user, !custom);
	gtk_widget_set_sensitive (dlg->folder, !custom);
}

static void
on_dialog_destroy (GtkWidget *w, gpointer data)
{
	g_free (dlg);
	dlg = NULL;
}

void
nemo_remote_connections_present (GtkWindow *parent)
{
	GtkWidget *grid, *box, *buttons, *connect, *save, *remove, *sw, *tv, *l;
	GtkCellRenderer *r;
	guint i;
	int row = 0;

	if (dlg != NULL) {
		gtk_window_present (GTK_WINDOW (dlg->window));
		return;
	}
	dlg = g_new0 (Dlg, 1);
	dlg->parent = parent;
	dlg->window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title (GTK_WINDOW (dlg->window), _("Connect to a server"));
	gtk_window_set_default_size (GTK_WINDOW (dlg->window), 460, 560);
	if (parent != NULL)
		gtk_window_set_transient_for (GTK_WINDOW (dlg->window), parent);
	g_signal_connect (dlg->window, "destroy", G_CALLBACK (on_dialog_destroy), NULL);

	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);
	gtk_container_set_border_width (GTK_CONTAINER (box), 14);
	gtk_container_add (GTK_CONTAINER (dlg->window), box);

	grid = gtk_grid_new ();
	gtk_grid_set_row_spacing (GTK_GRID (grid), 8);
	gtk_grid_set_column_spacing (GTK_GRID (grid), 12);

	l = gtk_label_new (_("Type"));
	gtk_widget_set_halign (l, GTK_ALIGN_START);
	dlg->proto = gtk_combo_box_text_new ();
	for (i = 0; i < G_N_ELEMENTS (protocols); i++)
		gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (dlg->proto), protocols[i].id, _(protocols[i].label));
	gtk_widget_set_hexpand (dlg->proto, TRUE);
	gtk_grid_attach (GTK_GRID (grid), l, 0, row, 1, 1);
	gtk_grid_attach (GTK_GRID (grid), dlg->proto, 1, row++, 1, 1);

	dlg->server_label = gtk_label_new (_("Server"));
	gtk_widget_set_halign (dlg->server_label, GTK_ALIGN_START);
	dlg->server = gtk_entry_new ();
	gtk_entry_set_activates_default (GTK_ENTRY (dlg->server), FALSE);
	gtk_grid_attach (GTK_GRID (grid), dlg->server_label, 0, row, 1, 1);
	gtk_grid_attach (GTK_GRID (grid), dlg->server, 1, row++, 1, 1);

	dlg->port_label = gtk_label_new (_("Port"));
	gtk_widget_set_halign (dlg->port_label, GTK_ALIGN_START);
	dlg->port = gtk_entry_new ();
	gtk_entry_set_input_purpose (GTK_ENTRY (dlg->port), GTK_INPUT_PURPOSE_DIGITS);
	gtk_grid_attach (GTK_GRID (grid), dlg->port_label, 0, row, 1, 1);
	gtk_grid_attach (GTK_GRID (grid), dlg->port, 1, row++, 1, 1);

	dlg->user_label = gtk_label_new (_("User name"));
	gtk_widget_set_halign (dlg->user_label, GTK_ALIGN_START);
	dlg->user = gtk_entry_new ();
	gtk_entry_set_placeholder_text (GTK_ENTRY (dlg->user), _("optional: asked when connecting"));
	gtk_grid_attach (GTK_GRID (grid), dlg->user_label, 0, row, 1, 1);
	gtk_grid_attach (GTK_GRID (grid), dlg->user, 1, row++, 1, 1);

	l = gtk_label_new (_("Folder"));
	gtk_widget_set_halign (l, GTK_ALIGN_START);
	dlg->folder = gtk_entry_new ();
	gtk_entry_set_placeholder_text (GTK_ENTRY (dlg->folder), "/");
	gtk_grid_attach (GTK_GRID (grid), l, 0, row, 1, 1);
	gtk_grid_attach (GTK_GRID (grid), dlg->folder, 1, row++, 1, 1);

	l = gtk_label_new (_("Name in sidebar"));
	gtk_widget_set_halign (l, GTK_ALIGN_START);
	dlg->name = gtk_entry_new ();
	gtk_entry_set_placeholder_text (GTK_ENTRY (dlg->name), _("optional"));
	gtk_grid_attach (GTK_GRID (grid), l, 0, row, 1, 1);
	gtk_grid_attach (GTK_GRID (grid), dlg->name, 1, row++, 1, 1);
	gtk_box_pack_start (GTK_BOX (box), grid, FALSE, FALSE, 0);

	buttons = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
	connect = gtk_button_new_with_mnemonic (_("_Connect"));
	gtk_style_context_add_class (gtk_widget_get_style_context (connect), "suggested-action");
	save = gtk_button_new_with_mnemonic (_("Save to _sidebar"));
	g_signal_connect (connect, "clicked", G_CALLBACK (on_connect), NULL);
	g_signal_connect (save, "clicked", G_CALLBACK (on_save), NULL);
	gtk_box_pack_end (GTK_BOX (buttons), connect, FALSE, FALSE, 0);
	gtk_box_pack_end (GTK_BOX (buttons), save, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box), buttons, FALSE, FALSE, 0);

	l = gtk_label_new (NULL);
	gtk_label_set_markup (GTK_LABEL (l), _("<b>Saved connections</b>  (double-click to connect)"));
	gtk_widget_set_halign (l, GTK_ALIGN_START);
	gtk_box_pack_start (GTK_BOX (box), l, FALSE, FALSE, 0);

	dlg->store = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_STRING);
	tv = gtk_tree_view_new_with_model (GTK_TREE_MODEL (dlg->store));
	dlg->list = tv;
	g_object_unref (dlg->store);
	r = gtk_cell_renderer_text_new ();
	gtk_tree_view_append_column (GTK_TREE_VIEW (tv), gtk_tree_view_column_new_with_attributes (_("Name"), r, "text", 0, NULL));
	r = gtk_cell_renderer_text_new ();
	g_object_set (r, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
	{
		GtkTreeViewColumn *c = gtk_tree_view_column_new_with_attributes (_("Address"), r, "text", 1, NULL);

		gtk_tree_view_column_set_expand (c, TRUE);
		gtk_tree_view_append_column (GTK_TREE_VIEW (tv), c);
	}
	g_signal_connect (tv, "row-activated", G_CALLBACK (on_row_activated), NULL);
	sw = gtk_scrolled_window_new (NULL, NULL);
	gtk_container_add (GTK_CONTAINER (sw), tv);
	gtk_widget_set_vexpand (sw, TRUE);
	gtk_box_pack_start (GTK_BOX (box), sw, TRUE, TRUE, 0);

	remove = gtk_button_new_with_mnemonic (_("_Remove selected"));
	g_signal_connect (remove, "clicked", G_CALLBACK (on_remove), NULL);
	gtk_widget_set_halign (remove, GTK_ALIGN_START);
	gtk_box_pack_start (GTK_BOX (box), remove, FALSE, FALSE, 0);

	g_signal_connect (dlg->proto, "changed", G_CALLBACK (on_protocol_changed), NULL);
	gtk_combo_box_set_active_id (GTK_COMBO_BOX (dlg->proto), "sftp");
	refresh_list ();
	gtk_widget_show_all (dlg->window);
	on_protocol_changed (GTK_COMBO_BOX (dlg->proto), NULL);
}

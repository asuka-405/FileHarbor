/* nemo-extensions-window.c - manage FileHarbor extensions, and launch the built-in tools.
 * New file, not in upstream Nemo. */

#include <config.h>

#include "nemo-extensions-window.h"

#include <glib/gi18n.h>
#include <string.h>
#include <libnemo-private/fileharbor-extensions.h>

static GtkWidget *win = NULL;

void
nemo_launch_tool (const char *tool, GList *files)
{
	GPtrArray *argv = g_ptr_array_new_with_free_func (g_free);
	GError *error = NULL;
	GList *l;

	g_ptr_array_add (argv, g_strdup ("fileharbor-tools"));
	g_ptr_array_add (argv, g_strdup (tool));
	for (l = files; l != NULL; l = l->next)
		g_ptr_array_add (argv, g_strdup (l->data));
	g_ptr_array_add (argv, NULL);
	if (!g_spawn_async (NULL, (char **) argv->pdata, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error)) {
		g_warning ("Could not start the %s tool: %s", tool, error->message);
		g_clear_error (&error);
	}
	g_ptr_array_free (argv, TRUE);
}

static void
on_toggled (GtkSwitch *sw, GParamSpec *p, gpointer data)
{
	fh_extensions_set_enabled (data, gtk_switch_get_active (sw));
}

static void
on_open_folder (GtkButton *b, gpointer data)
{
	char *dir = fh_extensions_user_dir ();
	char *uri;

	g_mkdir_with_parents (dir, 0755);
	uri = g_filename_to_uri (dir, NULL, NULL);
	gtk_show_uri_on_window (GTK_WINDOW (win), uri, GDK_CURRENT_TIME, NULL);
	g_free (uri);
	g_free (dir);
}

static void
on_open_guide (GtkButton *b, gpointer data)
{
	char *guide = g_build_filename (NEMO_DATADIR, "docs", "EXTENSIONS.md", NULL);
	GList *files = g_list_prepend (NULL, guide);

	nemo_launch_tool ("markdown", files);
	g_list_free_full (files, g_free);
}

static void
on_destroy (GtkWidget *w, gpointer data)
{
	win = NULL;
}

void
nemo_extensions_window_present (GtkWindow *parent)
{
	GtkWidget *box, *head, *sw, *list, *foot, *b, *l;
	GList *el;
	int count = 0;

	if (win != NULL) {
		gtk_window_present (GTK_WINDOW (win));
		return;
	}
	win = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title (GTK_WINDOW (win), _("Extensions"));
	gtk_window_set_default_size (GTK_WINDOW (win), 560, 520);
	if (parent != NULL)
		gtk_window_set_transient_for (GTK_WINDOW (win), parent);
	g_signal_connect (win, "destroy", G_CALLBACK (on_destroy), NULL);

	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);
	gtk_container_set_border_width (GTK_CONTAINER (box), 14);
	gtk_container_add (GTK_CONTAINER (win), box);
	head = gtk_label_new (NULL);
	gtk_label_set_markup (GTK_LABEL (head), _("<b>Extensions</b> add menu items, sidebar entries, pages, tools and themes."));
	gtk_label_set_xalign (GTK_LABEL (head), 0.0);
	gtk_label_set_line_wrap (GTK_LABEL (head), TRUE);
	gtk_box_pack_start (GTK_BOX (box), head, FALSE, FALSE, 0);
	l = gtk_label_new (_("Extensions run with your permissions. Only install ones you trust. "
			     "Changes apply the next time FileHarbor starts."));
	gtk_label_set_xalign (GTK_LABEL (l), 0.0);
	gtk_label_set_line_wrap (GTK_LABEL (l), TRUE);
	gtk_style_context_add_class (gtk_widget_get_style_context (l), "dim-label");
	gtk_box_pack_start (GTK_BOX (box), l, FALSE, FALSE, 0);

	list = gtk_list_box_new ();
	gtk_list_box_set_selection_mode (GTK_LIST_BOX (list), GTK_SELECTION_NONE);
	for (el = fh_extensions_get_all (); el != NULL; el = el->next) {
		FhExtension *e = el->data;
		GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12), *v, *t, *d, *sw2;
		GString *what = g_string_new (NULL);
		char *m;

		gtk_container_set_border_width (GTK_CONTAINER (row), 10);
		v = gtk_box_new (GTK_ORIENTATION_VERTICAL, 3);
		m = g_markup_printf_escaped ("<b>%s</b>  <span alpha='60%%'>%s</span>", e->name, e->version ? e->version : "");
		t = gtk_label_new (NULL);
		gtk_label_set_markup (GTK_LABEL (t), m);
		gtk_label_set_xalign (GTK_LABEL (t), 0.0);
		g_free (m);
		gtk_box_pack_start (GTK_BOX (v), t, FALSE, FALSE, 0);
		if (e->error != NULL) {
			m = g_strdup_printf (_("Cannot be used: %s"), e->error);
		} else {
			if (g_list_length (e->sidebar)) g_string_append_printf (what, _("%u sidebar · "), g_list_length (e->sidebar));
			if (g_list_length (e->pages)) g_string_append_printf (what, _("%u pages · "), g_list_length (e->pages));
			if (g_list_length (e->tools)) g_string_append_printf (what, _("%u tools · "), g_list_length (e->tools));
			m = g_strdup (what->len ? what->str : "");
			if (what->len > 3)
				m[strlen (m) - 3] = '\0';       /* trailing " · " */
		}
		d = gtk_label_new ((e->description != NULL && e->error == NULL) ? e->description : m);
		gtk_label_set_xalign (GTK_LABEL (d), 0.0);
		gtk_label_set_line_wrap (GTK_LABEL (d), TRUE);
		gtk_style_context_add_class (gtk_widget_get_style_context (d), "dim-label");
		gtk_box_pack_start (GTK_BOX (v), d, FALSE, FALSE, 0);
		if (e->description != NULL && e->error == NULL && *m) {
			GtkWidget *w = gtk_label_new (m);

			gtk_label_set_xalign (GTK_LABEL (w), 0.0);
			gtk_style_context_add_class (gtk_widget_get_style_context (w), "dim-label");
			gtk_box_pack_start (GTK_BOX (v), w, FALSE, FALSE, 0);
		}
		g_free (m);
		g_string_free (what, TRUE);
		gtk_box_pack_start (GTK_BOX (row), v, TRUE, TRUE, 0);
		sw2 = gtk_switch_new ();
		gtk_widget_set_valign (sw2, GTK_ALIGN_CENTER);
		gtk_switch_set_active (GTK_SWITCH (sw2), e->enabled);
		gtk_widget_set_sensitive (sw2, e->error == NULL);
		g_signal_connect_data (sw2, "notify::active", G_CALLBACK (on_toggled), g_strdup (e->id), (GClosureNotify) g_free, 0);
		gtk_box_pack_end (GTK_BOX (row), sw2, FALSE, FALSE, 0);
		gtk_container_add (GTK_CONTAINER (list), row);
		count++;
	}
	if (count == 0) {
		GtkWidget *e = gtk_label_new (_("No extensions installed yet.\n\n"
						"Put an extension folder in the extensions folder (button below) and restart. "
						"The guide shows how to make one."));
		gtk_label_set_line_wrap (GTK_LABEL (e), TRUE);
		gtk_widget_set_margin_top (e, 24);
		gtk_container_add (GTK_CONTAINER (list), e);
	}
	sw = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sw), GTK_SHADOW_IN);
	gtk_widget_set_vexpand (sw, TRUE);
	gtk_container_add (GTK_CONTAINER (sw), list);
	gtk_box_pack_start (GTK_BOX (box), sw, TRUE, TRUE, 0);

	foot = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
	b = gtk_button_new_with_mnemonic (_("Open extensions _folder"));
	g_signal_connect (b, "clicked", G_CALLBACK (on_open_folder), NULL);
	gtk_box_pack_start (GTK_BOX (foot), b, FALSE, FALSE, 0);
	b = gtk_button_new_with_mnemonic (_("How to make an extension"));
	g_signal_connect (b, "clicked", G_CALLBACK (on_open_guide), NULL);
	gtk_box_pack_end (GTK_BOX (foot), b, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box), foot, FALSE, FALSE, 0);
	gtk_widget_show_all (win);
}

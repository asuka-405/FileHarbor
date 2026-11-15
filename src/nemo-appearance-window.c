/* nemo-appearance-window.c - theme picker/editor and docs. New file, not in upstream.
 *
 * "Themes" tab: choose a theme, tweak every colour and metric with live
 * preview, save as a new theme. "Documentation" tab: fileharbor/docs/THEMING.md. */

#include <config.h>

#include "nemo-appearance-window.h"
#include "nemo-theme-manager.h"
#include "nemo-desktop-window.h"
#include "nemo-window.h"
#include <libnemo-private/nemo-global-preferences.h>

#include <glib/gi18n.h>
#include <string.h>

typedef struct {
	GtkWidget *window;
	GtkWidget *combo;
	GtkWidget *save_btn;      /* overwrite a user theme */
	GtkWidget *saveas_btn;
	GtkWidget *delete_btn;
	GtkWidget *note;
	GtkWidget *color_btn[16];
	GtkWidget *radius, *gap, *border;
	GtkWidget *dark_switch;
	GKeyFile *editing;        /* resolved copy being edited */
	char *selected_id;
	gboolean selected_builtin;
	gboolean loading;
	gboolean dirty;
} Ui;

static Ui *ui = NULL;

static void
rgba_to_hex (const GdkRGBA *c, char *out)
{
	g_snprintf (out, 8, "#%02x%02x%02x", (int) (c->red * 255 + 0.5), (int) (c->green * 255 + 0.5),
		    (int) (c->blue * 255 + 0.5));
}

static void
update_buttons (void)
{
	gtk_widget_set_sensitive (ui->save_btn, !ui->selected_builtin && ui->dirty);
	gtk_widget_set_sensitive (ui->delete_btn, !ui->selected_builtin);
	gtk_label_set_text (GTK_LABEL (ui->note), ui->dirty
		? _("Previewing unsaved changes. Save them, or close this window to discard.")
		: (ui->selected_builtin ? _("Built-in theme: change something, then use “Save as new theme”.") : ""));
}

static void
populate_widgets (void)
{
	int i;

	ui->loading = TRUE;
	for (i = 0; nemo_theme_color_keys[i] != NULL; i++) {
		char *hex = g_key_file_get_string (ui->editing, "Colors", nemo_theme_color_keys[i], NULL);
		GdkRGBA c;

		if (gdk_rgba_parse (&c, hex))
			gtk_color_chooser_set_rgba (GTK_COLOR_CHOOSER (ui->color_btn[i]), &c);
		g_free (hex);
	}
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (ui->radius), g_key_file_get_integer (ui->editing, "Metrics", "radius", NULL));
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (ui->gap), g_key_file_get_integer (ui->editing, "Metrics", "gap", NULL));
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (ui->border), g_key_file_get_integer (ui->editing, "Metrics", "border", NULL));
	gtk_switch_set_active (GTK_SWITCH (ui->dark_switch), g_key_file_get_boolean (ui->editing, "Theme", "Dark", NULL));
	ui->loading = FALSE;
}

static void
edited (void)
{
	if (ui->loading)
		return;
	ui->dirty = TRUE;
	nemo_theme_manager_preview (ui->editing);
	update_buttons ();
}

static void
on_color_set (GtkColorButton *b, gpointer data)
{
	GdkRGBA c;
	char hex[8];
	int i = GPOINTER_TO_INT (data);

	if (ui->loading) return;
	gtk_color_chooser_get_rgba (GTK_COLOR_CHOOSER (b), &c);
	rgba_to_hex (&c, hex);
	g_key_file_set_string (ui->editing, "Colors", nemo_theme_color_keys[i], hex);
	edited ();
}

static void
on_metric_changed (GtkSpinButton *s, gpointer data)
{
	if (ui->loading) return;
	g_key_file_set_integer (ui->editing, "Metrics", (const char *) data, gtk_spin_button_get_value_as_int (s));
	edited ();
}

static void
on_dark_toggled (GObject *sw, GParamSpec *p, gpointer data)
{
	if (ui->loading) return;
	g_key_file_set_boolean (ui->editing, "Theme", "Dark", gtk_switch_get_active (GTK_SWITCH (sw)));
	edited ();
}

static void
select_theme (const char *id, gboolean builtin)
{
	GKeyFile *raw = nemo_theme_manager_load (id);

	g_free (ui->selected_id);
	ui->selected_id = g_strdup (id);
	ui->selected_builtin = builtin;
	if (ui->editing != NULL)
		g_key_file_free (ui->editing);
	ui->editing = nemo_theme_manager_resolve (raw);
	g_key_file_free (raw);
	ui->dirty = FALSE;
	populate_widgets ();
	update_buttons ();
}

static void
on_mode_changed (GtkComboBox *c, gpointer data)
{
	const char *id = gtk_combo_box_get_active_id (c);

	if (ui->loading || id == NULL) return;
	nemo_theme_manager_set_mode (id);                 /* saved and applied at once */
	if (ui->selected_id != NULL)
		select_theme (ui->selected_id, ui->selected_builtin);   /* show the colours of the variant now in use */
}

static void
on_desktop_accent_toggled (GObject *sw, GParamSpec *p, gpointer data)
{
	if (ui->loading) return;
	nemo_theme_manager_set_desktop_accent (gtk_switch_get_active (GTK_SWITCH (sw)));
	if (ui->selected_id != NULL)
		select_theme (ui->selected_id, ui->selected_builtin);
}

static void
fill_combo (const char *select_id)
{
	GList *themes = nemo_theme_manager_list (), *l;
	int idx = 0, active = 0;

	ui->loading = TRUE;
	gtk_list_store_clear (GTK_LIST_STORE (gtk_combo_box_get_model (GTK_COMBO_BOX (ui->combo))));
	for (l = themes; l != NULL; l = l->next) {
		NemoThemeInfo *t = l->data;
		char *label = g_strdup_printf ("%s%s", t->name, t->builtin ? "" : _("  (yours)"));

		gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (ui->combo), t->id, label);
		if (g_strcmp0 (t->id, select_id) == 0)
			active = idx;
		g_free (label);
		idx++;
	}
	gtk_combo_box_set_active (GTK_COMBO_BOX (ui->combo), active);
	ui->loading = FALSE;
	g_list_free_full (themes, (GDestroyNotify) nemo_theme_info_free);
}

static void
on_combo_changed (GtkComboBox *c, gpointer data)
{
	const char *id = gtk_combo_box_get_active_id (c);
	gboolean builtin = TRUE;
	GList *themes, *l;

	if (ui->loading || id == NULL) return;
	themes = nemo_theme_manager_list ();
	for (l = themes; l != NULL; l = l->next)
		if (g_strcmp0 (((NemoThemeInfo *) l->data)->id, id) == 0)
			builtin = ((NemoThemeInfo *) l->data)->builtin;
	g_list_free_full (themes, (GDestroyNotify) nemo_theme_info_free);

	nemo_theme_manager_set_current (id);   /* saved and applied immediately */
	select_theme (id, builtin);
	gtk_widget_set_sensitive (ui->saveas_btn, g_strcmp0 (id, NEMO_THEME_SYSTEM_ID) != 0);
}

static void
write_theme (const char *id, const char *name)
{
	GKeyFile *out = g_key_file_new ();
	GError *error = NULL;
	int i;
	char *dirpath;

	g_key_file_set_string (out, "Theme", "Name", name);
	g_key_file_set_boolean (out, "Theme", "Dark", g_key_file_get_boolean (ui->editing, "Theme", "Dark", NULL));
	for (i = 0; nemo_theme_color_keys[i] != NULL; i++) {
		char *v = g_key_file_get_string (ui->editing, "Colors", nemo_theme_color_keys[i], NULL);

		g_key_file_set_string (out, "Colors", nemo_theme_color_keys[i], v);
		g_free (v);
	}
	g_key_file_set_integer (out, "Metrics", "radius", g_key_file_get_integer (ui->editing, "Metrics", "radius", NULL));
	g_key_file_set_integer (out, "Metrics", "gap", g_key_file_get_integer (ui->editing, "Metrics", "gap", NULL));
	g_key_file_set_integer (out, "Metrics", "border", g_key_file_get_integer (ui->editing, "Metrics", "border", NULL));
	if (!nemo_theme_manager_save_user (id, out, &error)) {
		GtkWidget *d = gtk_message_dialog_new (GTK_WINDOW (ui->window), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR,
						       GTK_BUTTONS_OK, _("Could not save the theme"));

		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (d), "%s", error->message);
		gtk_dialog_run (GTK_DIALOG (d));
		gtk_widget_destroy (d);
		g_clear_error (&error);
	} else {
		dirpath = nemo_theme_manager_user_dir ();
		g_free (dirpath);
		nemo_theme_manager_set_current (id);
		fill_combo (id);
		select_theme (id, FALSE);
	}
	g_key_file_free (out);
}

static void
on_save_clicked (GtkButton *b, gpointer data)
{
	char *name = g_key_file_get_string (ui->editing, "Theme", "Name", NULL);

	write_theme (ui->selected_id, name != NULL ? name : ui->selected_id);
	g_free (name);
}

static void
on_saveas_clicked (GtkButton *b, gpointer data)
{
	GtkWidget *d = gtk_dialog_new_with_buttons (_("Save as new theme"), GTK_WINDOW (ui->window),
						    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
						    _("_Cancel"), GTK_RESPONSE_CANCEL, _("_Save"), GTK_RESPONSE_ACCEPT, NULL);
	GtkWidget *box = gtk_dialog_get_content_area (GTK_DIALOG (d));
	GtkWidget *label = gtk_label_new (_("Theme name:"));
	GtkWidget *entry = gtk_entry_new ();

	gtk_container_set_border_width (GTK_CONTAINER (box), 12);
	gtk_box_set_spacing (GTK_BOX (box), 6);
	gtk_entry_set_activates_default (GTK_ENTRY (entry), TRUE);
	gtk_dialog_set_default_response (GTK_DIALOG (d), GTK_RESPONSE_ACCEPT);
	gtk_widget_set_halign (label, GTK_ALIGN_START);
	gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box), entry, FALSE, FALSE, 0);
	gtk_widget_show_all (d);
	if (gtk_dialog_run (GTK_DIALOG (d)) == GTK_RESPONSE_ACCEPT) {
		const char *name = gtk_entry_get_text (GTK_ENTRY (entry));

		if (name[0] != '\0') {
			char *id = nemo_theme_manager_make_id (name);

			write_theme (id, name);
			g_free (id);
		}
	}
	gtk_widget_destroy (d);
}

static void
on_delete_clicked (GtkButton *b, gpointer data)
{
	GtkWidget *d = gtk_message_dialog_new (GTK_WINDOW (ui->window), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
					       GTK_BUTTONS_NONE, _("Delete this theme?"));

	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (d), _("The theme file will be removed."));
	gtk_dialog_add_button (GTK_DIALOG (d), _("_Cancel"), GTK_RESPONSE_CANCEL);
	gtk_dialog_add_button (GTK_DIALOG (d), _("_Delete"), GTK_RESPONSE_ACCEPT);
	if (gtk_dialog_run (GTK_DIALOG (d)) == GTK_RESPONSE_ACCEPT) {
		nemo_theme_manager_delete_user (ui->selected_id);
		nemo_theme_manager_set_current ("system");
		fill_combo ("system");
		select_theme ("system", TRUE);
	}
	gtk_widget_destroy (d);
}

/* navigation style: columns (Miller) or the classic sidebar; applied to every open window */
static void
on_navigator_changed (GtkComboBox *c, gpointer data)
{
	const char *id = gtk_combo_box_get_active_id (c);
	GList *l;

	if (ui->loading || id == NULL) return;
	g_settings_set_string (nemo_preferences, NEMO_PREFERENCES_FILEHARBOR_NAVIGATOR, id);
	for (l = gtk_application_get_windows (GTK_APPLICATION (g_application_get_default ())); l != NULL; l = l->next) {
		if (NEMO_IS_WINDOW (l->data) && !NEMO_IS_DESKTOP_WINDOW (l->data)) {
			nemo_window_hide_sidebar (NEMO_WINDOW (l->data));
			nemo_window_show_sidebar (NEMO_WINDOW (l->data));
		}
	}
}

static void
on_open_folder (GtkButton *b, gpointer data)
{
	char *dir = nemo_theme_manager_user_dir ();
	GFile *f;
	char *uri;

	g_mkdir_with_parents (dir, 0755);
	f = g_file_new_for_path (dir);
	uri = g_file_get_uri (f);
	gtk_show_uri_on_window (GTK_WINDOW (ui->window), uri, GDK_CURRENT_TIME, NULL);
	g_free (uri); g_object_unref (f); g_free (dir);
}

/* ---- docs tab: a tiny markdown renderer for fileharbor/docs/THEMING.md ---- */

static void
append_inline (GtkTextBuffer *buf, GtkTextIter *it, const char *line)
{
	/* `code` spans and **bold** */
	const char *p = line;

	while (*p != '\0') {
		if (*p == '`') {
			const char *end = strchr (p + 1, '`');

			if (end != NULL) {
				gtk_text_buffer_insert_with_tags_by_name (buf, it, p + 1, end - p - 1, "code", NULL);
				p = end + 1;
				continue;
			}
		} else if (p[0] == '*' && p[1] == '*') {
			const char *end = strstr (p + 2, "**");

			if (end != NULL) {
				gtk_text_buffer_insert_with_tags_by_name (buf, it, p + 2, end - p - 2, "bold", NULL);
				p = end + 2;
				continue;
			}
		}
		{
			const char *next = p + 1;

			while (*next != '\0' && *next != '`' && !(next[0] == '*' && next[1] == '*'))
				next++;
			gtk_text_buffer_insert (buf, it, p, next - p);
			p = next;
		}
	}
}

static void
render_markdown (GtkTextBuffer *buf, const char *text)
{
	char **lines = g_strsplit (text, "\n", -1);
	gboolean in_code = FALSE;
	GtkTextIter it;
	int i;

	gtk_text_buffer_create_tag (buf, "h1", "weight", PANGO_WEIGHT_BOLD, "scale", 1.5, "pixels-above-lines", 14, "pixels-below-lines", 4, NULL);
	gtk_text_buffer_create_tag (buf, "bold", "weight", PANGO_WEIGHT_BOLD, NULL);
	gtk_text_buffer_create_tag (buf, "code", "family", "monospace", "background", "#80808033", NULL);
	gtk_text_buffer_create_tag (buf, "block", "family", "monospace", "background", "#80808033", "left-margin", 16,
				    "right-margin", 16, "pixels-above-lines", 0, NULL);
	gtk_text_buffer_create_tag (buf, "bullet", "left-margin", 22, "indent", -12, "pixels-below-lines", 3, NULL);
	gtk_text_buffer_get_end_iter (buf, &it);
	for (i = 0; lines[i] != NULL; i++) {
		const char *l = lines[i];

		if (g_str_has_prefix (l, "```")) {
			in_code = !in_code;
			continue;
		}
		if (in_code) {
			gtk_text_buffer_insert_with_tags_by_name (buf, &it, l, -1, "block", NULL);
			gtk_text_buffer_insert (buf, &it, "\n", 1);
		} else if (g_str_has_prefix (l, "# ")) {
			gtk_text_buffer_insert_with_tags_by_name (buf, &it, l + 2, -1, "h1", NULL);
			gtk_text_buffer_insert (buf, &it, "\n", 1);
		} else if (g_str_has_prefix (l, "- ")) {
			GtkTextIter start;
			int off;

			off = gtk_text_iter_get_offset (&it);
			gtk_text_buffer_insert (buf, &it, "•  ", -1);
			append_inline (buf, &it, l + 2);
			gtk_text_buffer_insert (buf, &it, "\n", 1);
			gtk_text_buffer_get_iter_at_offset (buf, &start, off);
			gtk_text_buffer_apply_tag_by_name (buf, "bullet", &start, &it);
		} else {
			append_inline (buf, &it, l);
			gtk_text_buffer_insert (buf, &it, "\n", 1);
		}
	}
	g_strfreev (lines);
}

static GtkWidget *
build_docs_tab (void)
{
	GtkWidget *sw = gtk_scrolled_window_new (NULL, NULL);
	GtkWidget *tv = gtk_text_view_new ();
	GtkTextBuffer *buf = gtk_text_view_get_buffer (GTK_TEXT_VIEW (tv));
	char *path = g_build_filename (NEMO_DATADIR, "docs", "THEMING.md", NULL);
	char *text = NULL;

	gtk_text_view_set_editable (GTK_TEXT_VIEW (tv), FALSE);
	gtk_text_view_set_cursor_visible (GTK_TEXT_VIEW (tv), FALSE);
	gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (tv), GTK_WRAP_WORD_CHAR);
	gtk_text_view_set_left_margin (GTK_TEXT_VIEW (tv), 14);
	gtk_text_view_set_right_margin (GTK_TEXT_VIEW (tv), 14);
	if (g_file_get_contents (path, &text, NULL, NULL)) {
		render_markdown (buf, text);
		g_free (text);
	} else {
		gtk_text_buffer_set_text (buf, _("The theming guide (fileharbor/docs/THEMING.md) was not found in the installation."), -1);
	}
	g_free (path);
	gtk_container_add (GTK_CONTAINER (sw), tv);
	return sw;
}

static GtkWidget *
build_themes_tab (void)
{
	GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);
	GtkWidget *top = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	GtkWidget *grid = gtk_grid_new ();
	GtkWidget *open_btn, *l;
	int i, row = 0;

	gtk_container_set_border_width (GTK_CONTAINER (box), 14);
	ui->combo = gtk_combo_box_text_new ();
	gtk_widget_set_hexpand (ui->combo, TRUE);
	ui->save_btn = gtk_button_new_with_mnemonic (_("_Save changes"));
	ui->saveas_btn = gtk_button_new_with_mnemonic (_("Save as _new theme…"));
	ui->delete_btn = gtk_button_new_with_mnemonic (_("_Delete"));
	open_btn = gtk_button_new_with_mnemonic (_("Open themes _folder"));
	g_signal_connect (ui->save_btn, "clicked", G_CALLBACK (on_save_clicked), NULL);
	g_signal_connect (ui->saveas_btn, "clicked", G_CALLBACK (on_saveas_clicked), NULL);
	g_signal_connect (ui->delete_btn, "clicked", G_CALLBACK (on_delete_clicked), NULL);
	g_signal_connect (open_btn, "clicked", G_CALLBACK (on_open_folder), NULL);
	gtk_box_pack_start (GTK_BOX (top), ui->combo, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (top), ui->save_btn, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (top), ui->saveas_btn, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (top), ui->delete_btn, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box), top, FALSE, FALSE, 0);

	ui->note = gtk_label_new ("");
	gtk_widget_set_halign (ui->note, GTK_ALIGN_START);
	gtk_style_context_add_class (gtk_widget_get_style_context (ui->note), "dim-label");
	gtk_box_pack_start (GTK_BOX (box), ui->note, FALSE, FALSE, 0);

	gtk_grid_set_row_spacing (GTK_GRID (grid), 8);
	gtk_grid_set_column_spacing (GTK_GRID (grid), 14);
	/* light or dark, and the accent colour source: they apply to every theme */
	{
		GtkWidget *ml = gtk_label_new (_("Appearance"));
		GtkWidget *mc = gtk_combo_box_text_new ();
		GtkWidget *al = gtk_label_new (_("Use the desktop's accent colour"));
		GtkWidget *as = gtk_switch_new ();

		gtk_widget_set_halign (ml, GTK_ALIGN_START);
		gtk_widget_set_halign (al, GTK_ALIGN_START);
		gtk_widget_set_halign (as, GTK_ALIGN_START);
		gtk_widget_set_tooltip_text (as, _("Off: coral. On: the accent colour from your desktop settings (COSMIC, GNOME, KDE). Applies to the System theme."));
		gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (mc), "system", _("Follow the system (light or dark)"));
		gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (mc), "dark", _("Always dark"));
		gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (mc), "light", _("Always light"));
		gtk_combo_box_set_active_id (GTK_COMBO_BOX (mc), nemo_theme_manager_get_mode ());
		gtk_switch_set_active (GTK_SWITCH (as), nemo_theme_manager_get_desktop_accent ());
		g_signal_connect (mc, "changed", G_CALLBACK (on_mode_changed), NULL);
		g_signal_connect (as, "notify::active", G_CALLBACK (on_desktop_accent_toggled), NULL);
		gtk_grid_attach (GTK_GRID (grid), ml, 0, row, 1, 1);
		gtk_grid_attach (GTK_GRID (grid), mc, 1, row, 1, 1);
		row++;
		gtk_grid_attach (GTK_GRID (grid), al, 0, row, 1, 1);
		gtk_grid_attach (GTK_GRID (grid), as, 1, row, 1, 1);
		row++;
	}
	{
		GtkWidget *extra_grid = gtk_grid_new ();
		GtkWidget *expander = gtk_expander_new (_("More colours"));
		int extra_row = 0;

		gtk_grid_set_row_spacing (GTK_GRID (extra_grid), 8);
		gtk_grid_set_column_spacing (GTK_GRID (extra_grid), 14);
		gtk_widget_set_margin_top (extra_grid, 6);
		gtk_container_add (GTK_CONTAINER (expander), extra_grid);
		for (i = 0; nemo_theme_color_keys[i] != NULL; i++) {
			gboolean core = i < 9;

			l = gtk_label_new (_(nemo_theme_color_labels[i]));
			gtk_widget_set_halign (l, GTK_ALIGN_START);
			ui->color_btn[i] = gtk_color_button_new ();
			g_signal_connect (ui->color_btn[i], "color-set", G_CALLBACK (on_color_set), GINT_TO_POINTER (i));
			gtk_grid_attach (GTK_GRID (core ? grid : extra_grid), l, 0, core ? row : extra_row, 1, 1);
			gtk_grid_attach (GTK_GRID (core ? grid : extra_grid), ui->color_btn[i], 1, core ? row : extra_row, 1, 1);
			if (core) row++; else extra_row++;
		}
		gtk_grid_attach (GTK_GRID (grid), expander, 0, row, 2, 1);
		row++;
	}
	{
		struct { const char *label; const char *key; GtkWidget **w; int max; } m[] = {
			{ N_("Corner radius (px, 0 = square)"), "radius", &ui->radius, 32 },
			{ N_("Gap between panels (px)"), "gap", &ui->gap, 32 },
			{ N_("Border width (px)"), "border", &ui->border, 6 },
		};
		int j;

		for (j = 0; j < 3; j++) {
			l = gtk_label_new (_(m[j].label));
			gtk_widget_set_halign (l, GTK_ALIGN_START);
			*m[j].w = gtk_spin_button_new_with_range (0, m[j].max, 1);
			g_signal_connect (*m[j].w, "value-changed", G_CALLBACK (on_metric_changed), (gpointer) m[j].key);
			gtk_grid_attach (GTK_GRID (grid), l, 0, row, 1, 1);
			gtk_grid_attach (GTK_GRID (grid), *m[j].w, 1, row, 1, 1);
			row++;
		}
	}
	l = gtk_label_new (_("Dark widgets"));
	gtk_widget_set_halign (l, GTK_ALIGN_START);
	ui->dark_switch = gtk_switch_new ();
	gtk_widget_set_halign (ui->dark_switch, GTK_ALIGN_START);
	g_signal_connect (ui->dark_switch, "notify::active", G_CALLBACK (on_dark_toggled), NULL);
	gtk_grid_attach (GTK_GRID (grid), l, 0, row, 1, 1);
	gtk_grid_attach (GTK_GRID (grid), ui->dark_switch, 1, row, 1, 1);
	row++;
	row++;
	{
		struct { const char *label; const char *key; } sw[] = {
			{ N_("Link file view and terminal"), NEMO_PREFERENCES_TERMINAL_LINK_VIEW },
			{ N_("Follow ssh sessions (new terminals)"), NEMO_PREFERENCES_TERMINAL_SSH_INTEGRATION },
			{ N_("Remember the preview pane per folder"), NEMO_PREFERENCES_PREVIEW_PANE_PER_FOLDER },
			{ N_("Open Markdown files in the built-in preview"), NEMO_PREFERENCES_MARKDOWN_OPEN_IN_PREVIEW },
			{ N_("Open text and code files in the built-in editor"), NEMO_PREFERENCES_TEXT_OPEN_IN_EDITOR },
			{ N_("Open pictures, video and music in the built-in viewers"), NEMO_PREFERENCES_MEDIA_OPEN_IN_BUILTIN },
			{ N_("Developer mode (debug log, GTK inspector, developer panel)"), NEMO_PREFERENCES_DEVELOPER_MODE },
		};
		guint k;

		for (k = 0; k < G_N_ELEMENTS (sw); k++) {
			GtkWidget *l2 = gtk_label_new (_(sw[k].label));
			GtkWidget *s2 = gtk_switch_new ();

			gtk_widget_set_halign (l2, GTK_ALIGN_START);
			gtk_widget_set_halign (s2, GTK_ALIGN_START);
			g_settings_bind (nemo_preferences, sw[k].key, s2, "active", G_SETTINGS_BIND_DEFAULT);
			gtk_grid_attach (GTK_GRID (grid), l2, 0, row, 1, 1);
			gtk_grid_attach (GTK_GRID (grid), s2, 1, row, 1, 1);
			row++;
		}
	}
	{
		GtkWidget *nl = gtk_label_new (_("Folder navigation"));
		GtkWidget *combo = gtk_combo_box_text_new ();
		char *cur = g_settings_get_string (nemo_preferences, NEMO_PREFERENCES_FILEHARBOR_NAVIGATOR);

		gtk_widget_set_halign (nl, GTK_ALIGN_START);
		gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (combo), "columns", _("Columns (whole path at a glance)"));
		gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (combo), "classic", _("Classic sidebar"));
		gtk_combo_box_set_active_id (GTK_COMBO_BOX (combo), g_strcmp0 (cur, "classic") == 0 ? "classic" : "columns");
		g_free (cur);
		g_signal_connect (combo, "changed", G_CALLBACK (on_navigator_changed), NULL);
		gtk_grid_attach (GTK_GRID (grid), nl, 0, row, 1, 1);
		gtk_grid_attach (GTK_GRID (grid), combo, 1, row, 1, 1);
	}

	{
		GtkWidget *sw = gtk_scrolled_window_new (NULL, NULL);

		gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
		gtk_container_add (GTK_CONTAINER (sw), grid);
		gtk_widget_set_vexpand (sw, TRUE);
		gtk_box_pack_start (GTK_BOX (box), sw, TRUE, TRUE, 0);
	}
	gtk_box_pack_start (GTK_BOX (box), open_btn, FALSE, FALSE, 0);
	gtk_widget_set_halign (open_btn, GTK_ALIGN_START);
	return box;
}

static void
on_window_destroy (GtkWidget *w, gpointer data)
{
	nemo_theme_manager_reapply ();    /* drop any unsaved preview */
	if (ui->editing != NULL)
		g_key_file_free (ui->editing);
	g_free (ui->selected_id);
	g_free (ui);
	ui = NULL;
}

void
nemo_appearance_window_present (GtkWindow *parent)
{
	GtkWidget *nb;

	if (ui != NULL) {
		gtk_window_present (GTK_WINDOW (ui->window));
		return;
	}
	ui = g_new0 (Ui, 1);
	ui->window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title (GTK_WINDOW (ui->window), _("Appearance"));
	gtk_window_set_default_size (GTK_WINDOW (ui->window), 560, 640);
	if (parent != NULL)
		gtk_window_set_transient_for (GTK_WINDOW (ui->window), parent);
	g_signal_connect (ui->window, "destroy", G_CALLBACK (on_window_destroy), NULL);

	nb = gtk_notebook_new ();
	gtk_notebook_append_page (GTK_NOTEBOOK (nb), build_themes_tab (), gtk_label_new (_("Themes")));
	gtk_notebook_append_page (GTK_NOTEBOOK (nb), build_docs_tab (), gtk_label_new (_("Build your own theme")));
	gtk_container_add (GTK_CONTAINER (ui->window), nb);

	g_signal_connect (ui->combo, "changed", G_CALLBACK (on_combo_changed), NULL);
	{
		const char *cur = nemo_theme_manager_get_current ();
		gboolean builtin = TRUE;
		GList *themes = nemo_theme_manager_list (), *l;

		for (l = themes; l != NULL; l = l->next)
			if (g_strcmp0 (((NemoThemeInfo *) l->data)->id, cur) == 0)
				builtin = ((NemoThemeInfo *) l->data)->builtin;
		g_list_free_full (themes, (GDestroyNotify) nemo_theme_info_free);
		fill_combo (cur);
		select_theme (cur, builtin);
	}
	gtk_widget_show_all (ui->window);
}

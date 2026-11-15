/* nemo-terminal-panel.c - embedded terminal for the FileHarbor fork of Nemo.
 * New file, not present in upstream Nemo. */

#include <config.h>

#include "nemo-terminal-panel.h"

#include <glib/gi18n.h>
#include <vte/vte.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <unistd.h>
#include <termios.h>
#include <string.h>

struct _NemoTerminalPanel {
	GtkBox parent_instance;

	VteTerminal *term;
	GPid shell_pid;
	gboolean spawning;
	char *wanted_path;   /* where the view is */
	char *shell_path;    /* where we last told the shell to be */
	gboolean mapped_once;
	gboolean remote;     /* the shell is on another machine (ssh) */
	char *last_uri;      /* last uri the shell reported and we acted on */
	guint cwd_idle;
	char *expect_path;          /* the view is on its way here because the shell moved */
	gint64 expect_deadline;
	GtkWidget *search_bar;
	GtkWidget *search_entry;
	double zoom;
	int url_tag;
};

enum { SHELL_DIRECTORY_CHANGED, N_SIGNALS };
static guint signals[N_SIGNALS];

G_DEFINE_TYPE (NemoTerminalPanel, nemo_terminal_panel, GTK_TYPE_BOX)

static void spawn_shell (NemoTerminalPanel *self);

/* ANSI palettes: One Dark for dark themes, One Light for light ones */
static const char *const palette_dark[16] = {
	"#282c34", "#e06c75", "#98c379", "#e5c07b", "#61afef", "#c678dd", "#56b6c2", "#abb2bf",
	"#7f8794", "#ef7a85", "#a8d389", "#f0cc8b", "#71bfff", "#d688ed", "#66c6d2", "#ffffff"
};
static const char *const palette_light[16] = {
	"#383a42", "#e45649", "#50a14f", "#c18401", "#4078f2", "#a626a4", "#0184bc", "#7a7c84",
	"#4f525e", "#d3463b", "#40913f", "#a17400", "#3068e2", "#962694", "#0074ac", "#c0c1c7"
};

static void
apply_theme_colors (NemoTerminalPanel *self)
{
	GtkStyleContext *ctx = gtk_widget_get_style_context (GTK_WIDGET (self));
	GdkRGBA fg, bg, accent, pal[16];
	const char *const *src;
	int i;

	/* the file manager palette first, so the terminal matches the panels around it */
	if (!gtk_style_context_lookup_color (ctx, "ns_text", &fg) &&
	    !gtk_style_context_lookup_color (ctx, "theme_fg_color", &fg))
		gdk_rgba_parse (&fg, "#c3c7d1");
	if (!gtk_style_context_lookup_color (ctx, "ns_surface", &bg) &&
	    !gtk_style_context_lookup_color (ctx, "theme_base_color", &bg))
		gdk_rgba_parse (&bg, "#101618");
	if (gtk_style_context_lookup_color (ctx, "ns_accent", &accent) ||
	    gtk_style_context_lookup_color (ctx, "theme_selected_bg_color", &accent))
		vte_terminal_set_color_cursor (self->term, &accent);
	src = (0.299 * bg.red + 0.587 * bg.green + 0.114 * bg.blue) < 0.5 ? palette_dark : palette_light;
	for (i = 0; i < 16; i++)
		gdk_rgba_parse (&pal[i], src[i]);
	vte_terminal_set_colors (self->term, &fg, &bg, pal, 16);
}

static void
apply_font (NemoTerminalPanel *self)
{
	char *name = g_settings_get_string (nemo_preferences, NEMO_PREFERENCES_TERMINAL_FONT);
	PangoFontDescription *fd;

	if (name == NULL || name[0] == '\0') {
		GSettingsSchemaSource *src = g_settings_schema_source_get_default ();
		GSettingsSchema *schema = src != NULL ? g_settings_schema_source_lookup (src, "org.gnome.desktop.interface", TRUE) : NULL;

		g_free (name);
		name = NULL;
		if (schema != NULL) {
			if (g_settings_schema_has_key (schema, "monospace-font-name")) {
				GSettings *gs = g_settings_new ("org.gnome.desktop.interface");

				name = g_settings_get_string (gs, "monospace-font-name");
				g_object_unref (gs);
			}
			g_settings_schema_unref (schema);
		}
		if (name == NULL || name[0] == '\0') {
			g_free (name);
			name = g_strdup ("Monospace 11");
		}
	}
	fd = pango_font_description_from_string (name);
	vte_terminal_set_font (self->term, fd);
	pango_font_description_free (fd);
	g_free (name);
}

static void
set_zoom (NemoTerminalPanel *self, double zoom)
{
	self->zoom = CLAMP (zoom, 0.4, 4.0);
	vte_terminal_set_font_scale (self->term, self->zoom);
}

/* ---- search (Ctrl+Shift+F) ---- */

static void
search_changed (GtkSearchEntry *e, gpointer data)
{
	NemoTerminalPanel *self = data;
	const char *text = gtk_entry_get_text (GTK_ENTRY (e));

	if (text[0] == '\0') {
		vte_terminal_search_set_regex (self->term, NULL, 0);
		return;
	}
	{
		char *esc = g_regex_escape_string (text, -1);
		GError *error = NULL;
		/* PCRE2_CASELESS | PCRE2_MULTILINE | PCRE2_UTF */
		VteRegex *re = vte_regex_new_for_search (esc, -1, 0x00000008u | 0x00000400u | 0x00080000u, &error);

		g_free (esc);
		if (re != NULL) {
			vte_terminal_search_set_regex (self->term, re, 0);
			vte_terminal_search_set_wrap_around (self->term, TRUE);
			vte_terminal_search_find_previous (self->term);
			vte_regex_unref (re);
		}
		g_clear_error (&error);
	}
}

static void search_next (GtkWidget *w, gpointer data) { vte_terminal_search_find_next (NEMO_TERMINAL_PANEL (data)->term); }
static void search_prev (GtkWidget *w, gpointer data) { vte_terminal_search_find_previous (NEMO_TERMINAL_PANEL (data)->term); }

static void
search_stop (GtkSearchEntry *e, gpointer data)
{
	NemoTerminalPanel *self = data;

	gtk_search_bar_set_search_mode (GTK_SEARCH_BAR (self->search_bar), FALSE);
	gtk_widget_grab_focus (GTK_WIDGET (self->term));
}

static void
show_search (NemoTerminalPanel *self)
{
	gtk_search_bar_set_search_mode (GTK_SEARCH_BAR (self->search_bar), TRUE);
	gtk_widget_grab_focus (self->search_entry);
}

/* ---- keyboard, mouse, menu ---- */

static void
menu_copy (GtkMenuItem *i, gpointer d) { vte_terminal_copy_clipboard_format (NEMO_TERMINAL_PANEL (d)->term, VTE_FORMAT_TEXT); }
static void
menu_paste (GtkMenuItem *i, gpointer d) { vte_terminal_paste_clipboard (NEMO_TERMINAL_PANEL (d)->term); }
static void
menu_select_all (GtkMenuItem *i, gpointer d) { vte_terminal_select_all (NEMO_TERMINAL_PANEL (d)->term); }
static void
menu_clear (GtkMenuItem *i, gpointer d) { vte_terminal_reset (NEMO_TERMINAL_PANEL (d)->term, TRUE, TRUE); }
static void
menu_search (GtkMenuItem *i, gpointer d) { show_search (d); }

static void
popup_menu_for (NemoTerminalPanel *self, GdkEvent *ev)
{
	GtkWidget *menu = gtk_menu_new ();
	struct { const char *label; GCallback cb; gboolean needs_selection; } items[] = {
		{ N_("Copy"), G_CALLBACK (menu_copy), TRUE },
		{ N_("Paste"), G_CALLBACK (menu_paste), FALSE },
		{ N_("Select All"), G_CALLBACK (menu_select_all), FALSE },
		{ NULL, NULL, FALSE },
		{ N_("Search…"), G_CALLBACK (menu_search), FALSE },
		{ N_("Clear and Reset"), G_CALLBACK (menu_clear), FALSE },
	};
	guint i;

	for (i = 0; i < G_N_ELEMENTS (items); i++) {
		GtkWidget *mi;

		if (items[i].label == NULL) {
			mi = gtk_separator_menu_item_new ();
		} else {
			mi = gtk_menu_item_new_with_label (_(items[i].label));
			g_signal_connect (mi, "activate", items[i].cb, self);
			if (items[i].needs_selection)
				gtk_widget_set_sensitive (mi, vte_terminal_get_has_selection (self->term));
		}
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), mi);
	}
	gtk_widget_show_all (menu);
	g_signal_connect (menu, "selection-done", G_CALLBACK (gtk_widget_destroy), NULL);
	gtk_menu_popup_at_pointer (GTK_MENU (menu), ev);
}

static gboolean
term_button_press (GtkWidget *w, GdkEventButton *ev, gpointer data)
{
	NemoTerminalPanel *self = data;

	if (ev->type != GDK_BUTTON_PRESS)
		return FALSE;
	if (ev->button == 3 && !(ev->state & GDK_SHIFT_MASK)) {
		popup_menu_for (self, (GdkEvent *) ev);
		return TRUE;
	}
	if (ev->button == 1 && (ev->state & GDK_CONTROL_MASK)) {      /* Ctrl+click opens links */
		char *link = vte_terminal_hyperlink_check_event (self->term, (GdkEvent *) ev);

		if (link == NULL) {
			int tag;

			link = vte_terminal_match_check_event (self->term, (GdkEvent *) ev, &tag);
		}
		if (link != NULL) {
			gtk_show_uri_on_window (GTK_WINDOW (gtk_widget_get_toplevel (w)), link, GDK_CURRENT_TIME, NULL);
			g_free (link);
			return TRUE;
		}
	}
	return FALSE;
}

static gboolean
term_key_press (GtkWidget *w, GdkEventKey *ev, gpointer data)
{
	NemoTerminalPanel *self = data;
	guint mods = ev->state & gtk_accelerator_get_default_mod_mask ();
	guint key = gdk_keyval_to_lower (ev->keyval);

	if (mods == (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) {
		if (key == GDK_KEY_c) {
			vte_terminal_copy_clipboard_format (self->term, VTE_FORMAT_TEXT);
			return TRUE;
		}
		if (key == GDK_KEY_v) {
			vte_terminal_paste_clipboard (self->term);
			return TRUE;
		}
		if (key == GDK_KEY_f) {
			show_search (self);
			return TRUE;
		}
	}
	if (mods == GDK_CONTROL_MASK || mods == (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) {
		if (ev->keyval == GDK_KEY_plus || ev->keyval == GDK_KEY_equal || ev->keyval == GDK_KEY_KP_Add) {
			set_zoom (self, self->zoom + 0.1);
			return TRUE;
		}
		if (ev->keyval == GDK_KEY_minus || ev->keyval == GDK_KEY_KP_Subtract) {
			set_zoom (self, self->zoom - 0.1);
			return TRUE;
		}
		if (ev->keyval == GDK_KEY_0 || ev->keyval == GDK_KEY_KP_0) {
			set_zoom (self, 1.0);
			return TRUE;
		}
	}
	return FALSE;
}

static void
font_pref_changed (GSettings *s, const char *key, gpointer data)
{
	apply_font (data);
}

static gboolean
shell_is_idle (NemoTerminalPanel *self)
{
	VtePty *pty = vte_terminal_get_pty (self->term);
	int fd;

	if (pty == NULL || self->shell_pid <= 0)
		return FALSE;
	fd = vte_pty_get_fd (pty);
	return tcgetpgrp (fd) == self->shell_pid;
}

static void
sync_shell_to_view (NemoTerminalPanel *self)
{
	char *quoted, *cmd;

	if (self->wanted_path == NULL || self->shell_pid <= 0)
		return;
	if (self->remote)       /* never type a local path into a remote shell */
		return;
	if (!g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_TERMINAL_LINK_VIEW))
		return;
	if (g_strcmp0 (self->wanted_path, self->shell_path) == 0)
		return;
	if (!shell_is_idle (self))      /* a program is running: do not type into it */
		return;

	g_free (self->shell_path);
	self->shell_path = g_strdup (self->wanted_path);
	quoted = g_shell_quote (self->wanted_path);
	/* ^U clears anything half-typed on the prompt line first */
	cmd = g_strdup_printf ("\025cd -- %s\n", quoted);
	vte_terminal_feed_child (self->term, cmd, -1);
	g_free (quoted);
	g_free (cmd);
}

static void
spawn_cb (VteTerminal *term, GPid pid, GError *error, gpointer data)
{
	NemoTerminalPanel *self = data;

	if (!NEMO_IS_TERMINAL_PANEL (self))
		return;
	self->spawning = FALSE;
	if (error != NULL) {
		g_warning ("Could not start terminal shell: %s", error->message);
		return;
	}
	self->shell_pid = pid;
}

static char *
integration_dir (void)
{
	const char *env = g_getenv ("NEMO_SHELL_INTEGRATION_DIR");

	return env != NULL ? g_strdup (env) : g_build_filename (NEMO_DATADIR, "shell-integration", NULL);
}

static void
spawn_shell (NemoTerminalPanel *self)
{
	const char *shell = g_getenv ("SHELL");
	const char *dir = self->wanted_path != NULL ? self->wanted_path : g_get_home_dir ();
	char *idir = integration_dir ();
	char *base;
	char **envv = g_get_environ ();
	char **argv;

	if (shell == NULL || *shell == '\0')
		shell = "/bin/sh";
	base = g_path_get_basename (shell);

	/* Report the directory (OSC 7) after every prompt, and follow ssh logins. */
	envv = g_environ_setenv (envv, "NEMO_INTEGRATION_DIR", idir, TRUE);
	if (!g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_TERMINAL_SSH_INTEGRATION))
		envv = g_environ_setenv (envv, "NEMO_NO_SSH_INTEGRATION", "1", TRUE);
	if (g_strcmp0 (base, "bash") == 0) {
		char *rc = g_build_filename (idir, "bash-rc", NULL);

		argv = g_new0 (char *, 5);
		argv[0] = g_strdup (shell);
		argv[1] = g_strdup ("--rcfile");
		argv[2] = rc;
		argv[3] = g_strdup ("-i");
	} else if (g_strcmp0 (base, "zsh") == 0) {
		const char *user_zdotdir = g_environ_getenv (envv, "ZDOTDIR");
		char *zdir = g_build_filename (idir, "zsh", NULL);

		envv = g_environ_setenv (envv, "NEMO_USER_ZDOTDIR", user_zdotdir != NULL ? user_zdotdir : g_get_home_dir (), TRUE);
		envv = g_environ_setenv (envv, "ZDOTDIR", zdir, TRUE);
		g_free (zdir);
		argv = g_new0 (char *, 3);
		argv[0] = g_strdup (shell);
		argv[1] = g_strdup ("-i");
	} else {
		argv = g_new0 (char *, 2);
		argv[0] = g_strdup (shell);
	}

	if (self->spawning) {
		g_strfreev (argv); g_strfreev (envv); g_free (idir); g_free (base);
		return;
	}
	self->spawning = TRUE;
	self->shell_pid = 0;
	self->remote = FALSE;
	g_free (self->shell_path);
	self->shell_path = g_strdup (dir);
	vte_terminal_spawn_async (self->term, VTE_PTY_DEFAULT, dir, argv, envv,
				  G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, -1, NULL,
				  spawn_cb, self);
	g_strfreev (argv);
	g_strfreev (envv);
	g_free (idir);
	g_free (base);
}

static void
child_exited_cb (VteTerminal *term, int status, gpointer data)
{
	NemoTerminalPanel *self = data;

	self->shell_pid = 0;
	self->remote = FALSE;
	g_clear_pointer (&self->last_uri, g_free);
	/* "exit" closes the panel, like Dolphin; it respawns when reopened */
	gtk_widget_hide (GTK_WIDGET (self));
}

/* Turn the shell's OSC 7 report into a location for the file view:
 *   file://<this host>/path          -> the local folder
 *   file://<user%40host%3Aport>/path -> sftp://user@host:port/path  (set by our ssh wrapper)
 *   file://<some other host>/path    -> sftp://host/path           (shell integration of the remote) */
static gboolean
is_local_host (const char *host)
{
	const char *mine = g_get_host_name ();
	char *dot;
	gboolean same;

	if (host == NULL || *host == '\0' || g_ascii_strcasecmp (host, "localhost") == 0)
		return TRUE;
	if (g_ascii_strcasecmp (host, mine) == 0)
		return TRUE;
	dot = strchr (host, '.');
	same = dot != NULL && g_ascii_strncasecmp (host, mine, dot - host) == 0 && strlen (mine) == (gsize) (dot - host);
	return same;
}

static gboolean
cwd_idle_cb (gpointer data)
{
	NemoTerminalPanel *self = data;
	const char *uri = vte_terminal_get_current_directory_uri (self->term);
	const char *rest, *slash;
	char *authority, *host, *path_enc, *path;
	GFile *f = NULL;

	self->cwd_idle = 0;
	if (uri == NULL || !g_str_has_prefix (uri, "file://"))
		return G_SOURCE_REMOVE;
	if (g_strcmp0 (uri, self->last_uri) == 0)
		return G_SOURCE_REMOVE;
	g_free (self->last_uri);
	self->last_uri = g_strdup (uri);
	if (!g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_TERMINAL_LINK_VIEW))
		return G_SOURCE_REMOVE;

	rest = uri + 7;
	slash = strchr (rest, '/');
	authority = slash != NULL ? g_strndup (rest, slash - rest) : g_strdup (rest);
	path_enc = g_strdup (slash != NULL ? slash : "/");
	host = g_uri_unescape_string (authority, NULL);

	if (is_local_host (host)) {
		path = g_uri_unescape_string (path_enc, NULL);
		self->remote = FALSE;
		if (path != NULL && g_strcmp0 (path, self->shell_path) != 0) {
			g_free (self->shell_path);
			self->shell_path = g_strdup (path);
			f = g_file_new_for_path (path);
		}
		g_free (path);
	} else {
		char *remote_uri = g_strdup_printf ("sftp://%s%s", host, path_enc);

		self->remote = TRUE;
		f = g_file_new_for_uri (remote_uri);
		g_free (remote_uri);
	}
	if (f != NULL) {
		g_signal_emit (self, signals[SHELL_DIRECTORY_CHANGED], 0, f);
		g_object_unref (f);
	}
	g_free (authority); g_free (host); g_free (path_enc);
	return G_SOURCE_REMOVE;
}

static void
cwd_uri_changed_cb (GObject *obj, GParamSpec *pspec, gpointer data)
{
	NemoTerminalPanel *self = data;

	/* a prompt can report twice in a row (our hook and the shell's own);
	 * act on the last report only */
	if (self->cwd_idle == 0)
		self->cwd_idle = g_timeout_add (60, cwd_idle_cb, self);
}

static void
panel_show (GtkWidget *w)
{
	NemoTerminalPanel *self = NEMO_TERMINAL_PANEL (w);

	GTK_WIDGET_CLASS (nemo_terminal_panel_parent_class)->show (w);
	apply_theme_colors (self);
	if (self->shell_pid <= 0)
		spawn_shell (self);
	else
		sync_shell_to_view (self);
}

static void
panel_style_updated (GtkWidget *w)
{
	GTK_WIDGET_CLASS (nemo_terminal_panel_parent_class)->style_updated (w);
	apply_theme_colors (NEMO_TERMINAL_PANEL (w));
}

static void
panel_finalize (GObject *object)
{
	NemoTerminalPanel *self = NEMO_TERMINAL_PANEL (object);

	if (self->cwd_idle != 0)
		g_source_remove (self->cwd_idle);
	g_free (self->wanted_path);
	g_free (self->shell_path);
	g_free (self->last_uri);
	g_free (self->expect_path);
	G_OBJECT_CLASS (nemo_terminal_panel_parent_class)->finalize (object);
}

static void
nemo_terminal_panel_class_init (NemoTerminalPanelClass *klass)
{
	GObjectClass *oc = G_OBJECT_CLASS (klass);
	GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);

	oc->finalize = panel_finalize;
	wc->show = panel_show;
	wc->style_updated = panel_style_updated;
	signals[SHELL_DIRECTORY_CHANGED] =
		g_signal_new ("shell-directory-changed", G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
			      G_TYPE_NONE, 1, G_TYPE_FILE);
}

static void
nemo_terminal_panel_init (NemoTerminalPanel *self)
{
	gtk_orientable_set_orientation (GTK_ORIENTABLE (self), GTK_ORIENTATION_VERTICAL);
	gtk_widget_set_no_show_all (GTK_WIDGET (self), TRUE);   /* hidden until toggled */
	gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (self)), "fileharbor-terminal");

	self->term = VTE_TERMINAL (vte_terminal_new ());
	self->zoom = 1.0;
	vte_terminal_set_scrollback_lines (self->term, 20000);
	vte_terminal_set_scroll_on_output (self->term, FALSE);
	vte_terminal_set_scroll_on_keystroke (self->term, TRUE);
	vte_terminal_set_allow_hyperlink (self->term, TRUE);
	vte_terminal_set_mouse_autohide (self->term, TRUE);
	vte_terminal_set_audible_bell (self->term, FALSE);
	vte_terminal_set_bold_is_bright (self->term, FALSE);
	vte_terminal_set_cursor_shape (self->term, VTE_CURSOR_SHAPE_BLOCK);
	vte_terminal_set_cursor_blink_mode (self->term, VTE_CURSOR_BLINK_OFF);
	vte_terminal_set_word_char_exceptions (self->term, "-,./?%&#:_=+@~");
	vte_terminal_set_enable_sixel (self->term, TRUE);
	apply_font (self);
	g_signal_connect_object (nemo_preferences, "changed::" NEMO_PREFERENCES_TERMINAL_FONT,
				 G_CALLBACK (font_pref_changed), self, 0);
	gtk_widget_set_size_request (GTK_WIDGET (self->term), -1, 140);
	gtk_widget_set_hexpand (GTK_WIDGET (self->term), TRUE);
	gtk_widget_set_vexpand (GTK_WIDGET (self->term), TRUE);

	/* URLs are clickable with Ctrl+click */
	{
		GError *error = NULL;
		VteRegex *url = vte_regex_new_for_match ("(?:https?|ftp|sftp|ssh|file)://[\\w\\-.~:/?#\\[\\]@!$&'()*+,;=%]+[\\w/]",
							 -1, VTE_REGEX_FLAGS_DEFAULT, &error);

		if (url != NULL) {
			self->url_tag = vte_terminal_match_add_regex (self->term, url, 0);
			vte_terminal_match_set_cursor_name (self->term, self->url_tag, "pointer");
			vte_regex_unref (url);
		}
		g_clear_error (&error);
	}

	gtk_box_pack_start (GTK_BOX (self), GTK_WIDGET (self->term), TRUE, TRUE, 0);
	gtk_widget_show (GTK_WIDGET (self->term));

	/* search bar under the terminal */
	{
		GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
		GtkWidget *prev = gtk_button_new_from_icon_name ("go-up-symbolic", GTK_ICON_SIZE_MENU);
		GtkWidget *next = gtk_button_new_from_icon_name ("go-down-symbolic", GTK_ICON_SIZE_MENU);

		self->search_bar = gtk_search_bar_new ();
		self->search_entry = gtk_search_entry_new ();
		gtk_widget_set_size_request (self->search_entry, 260, -1);
		gtk_widget_set_tooltip_text (prev, _("Previous match"));
		gtk_widget_set_tooltip_text (next, _("Next match"));
		gtk_box_pack_start (GTK_BOX (box), self->search_entry, FALSE, FALSE, 0);
		gtk_box_pack_start (GTK_BOX (box), prev, FALSE, FALSE, 0);
		gtk_box_pack_start (GTK_BOX (box), next, FALSE, FALSE, 0);
		gtk_container_add (GTK_CONTAINER (self->search_bar), box);
		gtk_search_bar_connect_entry (GTK_SEARCH_BAR (self->search_bar), GTK_ENTRY (self->search_entry));
		g_signal_connect (self->search_entry, "search-changed", G_CALLBACK (search_changed), self);
		g_signal_connect (self->search_entry, "next-match", G_CALLBACK (search_next), self);
		g_signal_connect (self->search_entry, "previous-match", G_CALLBACK (search_prev), self);
		g_signal_connect (self->search_entry, "stop-search", G_CALLBACK (search_stop), self);
		g_signal_connect (next, "clicked", G_CALLBACK (search_next), self);
		g_signal_connect (prev, "clicked", G_CALLBACK (search_prev), self);
		gtk_box_pack_end (GTK_BOX (self), self->search_bar, FALSE, FALSE, 0);
		gtk_widget_show_all (self->search_bar);
	}

	g_signal_connect (self->term, "child-exited", G_CALLBACK (child_exited_cb), self);
	g_signal_connect (self->term, "notify::current-directory-uri", G_CALLBACK (cwd_uri_changed_cb), self);
	g_signal_connect (self->term, "key-press-event", G_CALLBACK (term_key_press), self);
	g_signal_connect (self->term, "button-press-event", G_CALLBACK (term_button_press), self);
}

GtkWidget *
nemo_terminal_panel_new (void)
{
	return g_object_new (NEMO_TYPE_TERMINAL_PANEL, NULL);
}

void
nemo_terminal_panel_set_location (NemoTerminalPanel *self, GFile *location)
{
	char *path;

	g_return_if_fail (NEMO_IS_TERMINAL_PANEL (self));
	if (location == NULL)
		return;
	path = g_file_get_path (location);
	if (path == NULL)          /* remote or virtual location: leave the shell alone */
		return;

	/* The shell moved first and the view is still catching up: a location event
	 * for anywhere else is stale and must not drag the shell back. */
	if (self->expect_path != NULL) {
		gboolean arrived = g_strcmp0 (path, self->expect_path) == 0;
		gboolean stale = !arrived && g_get_monotonic_time () < self->expect_deadline;

		if (arrived || !stale)
			g_clear_pointer (&self->expect_path, g_free);
		if (stale) {
			g_free (path);
			return;
		}
	}
	g_free (self->wanted_path);
	self->wanted_path = path;
	if (gtk_widget_get_visible (GTK_WIDGET (self)))
		sync_shell_to_view (self);
}

/* Called by the window when it navigates the view because the shell changed directory */
void
nemo_terminal_panel_expect_location (NemoTerminalPanel *self, GFile *location)
{
	char *path = g_file_get_path (location);

	g_clear_pointer (&self->expect_path, g_free);
	self->expect_path = path;
	self->expect_deadline = g_get_monotonic_time () + 8 * G_USEC_PER_SEC;
}

void
nemo_terminal_panel_focus (NemoTerminalPanel *self)
{
	g_return_if_fail (NEMO_IS_TERMINAL_PANEL (self));
	gtk_widget_grab_focus (GTK_WIDGET (self->term));
}

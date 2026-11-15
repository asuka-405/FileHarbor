/* nemo-theme-manager.c - FileHarbor theme engine. New file, not in upstream. */

#include <config.h>

#include "nemo-theme-manager.h"
#include <libnemo-private/fileharbor-extensions.h>

#include <glib/gstdio.h>
#include <gio/gio.h>
#include <gtk/gtk.h>
#include <string.h>
#include <stdlib.h>
#include <libnemo-private/nemo-global-preferences.h>

const char *const nemo_theme_color_keys[] = {
	"window", "surface", "surface_alt", "text", "text_dim",
	"accent", "accent_text", "border", "hover", NULL
};
const char *const nemo_theme_color_labels[] = {
	"Window background", "Panels (sidebar, file area)", "Toolbar and headers", "Text",
	"Dim text", "Accent (selection, highlights)", "Text on accent", "Borders",
	"Hover highlight", NULL
};

typedef struct {
	const char *window, *surface, *surface_alt, *text, *text_dim, *accent,
		   *accent_text, *border, *hover;
} Palette;

/* COSMIC's default dark / light palettes */
static const Palette cosmic_dark = {
	"#141414", "#242424", "#2e2e2e", "#ececec", "#b4b4b4",
	"#63d0df", "#0b2a30", "#3a3a3a", "#363636"
};
static const Palette cosmic_light = {
	"#d9d9d9", "#f7f7f7", "#ebebeb", "#161616", "#4f4f4f",
	"#00707d", "#ffffff", "#bdbdbd", "#dcdcdc"
};

static GtkCssProvider *provider = NULL;
static GKeyFile *preview_kf = NULL;
static GList *cosmic_monitors = NULL;
static guint reload_id = 0;

/* ------------------------------------------------------------------ */
/* paths                                                              */
/* ------------------------------------------------------------------ */

char *
nemo_theme_manager_user_dir (void)
{
	return g_build_filename (g_get_user_config_dir (), "nemo", "themes", NULL);
}

static char *
system_dir (void)
{
	const char *env = g_getenv ("NEMO_THEME_DIR");

	return env != NULL ? g_strdup (env) : g_build_filename (NEMO_DATADIR, "themes", NULL);
}

static char *
find_theme_path (const char *id, gboolean *builtin)
{
	char *user_dir = nemo_theme_manager_user_dir ();
	char *file = g_strdup_printf ("%s.ini", id);
	char *path = g_build_filename (user_dir, file, NULL);

	g_free (user_dir);
	if (g_file_test (path, G_FILE_TEST_IS_REGULAR)) {
		if (builtin) *builtin = FALSE;
		g_free (file);
		return path;
	}
	g_free (path);
	user_dir = system_dir ();
	path = g_build_filename (user_dir, file, NULL);
	g_free (user_dir);
	if (g_file_test (path, G_FILE_TEST_IS_REGULAR)) {
		g_free (file);
		if (builtin) *builtin = TRUE;
		return path;
	}
	g_free (path);
	{
		GList *ed = fh_extensions_get_theme_dirs (), *e;
		char *found = NULL;

		for (e = ed; e != NULL && found == NULL; e = e->next) {
			char *cand = g_build_filename (e->data, file, NULL);

			if (g_file_test (cand, G_FILE_TEST_IS_REGULAR))
				found = cand;
			else
				g_free (cand);
		}
		g_list_free_full (ed, g_free);
		g_free (file);
		if (found != NULL && builtin) *builtin = TRUE;
		return found;
	}
}

void
nemo_theme_info_free (NemoThemeInfo *info)
{
	if (info == NULL) return;
	g_free (info->id);
	g_free (info->name);
	g_free (info->path);
	g_free (info);
}

static void
collect_dir (const char *dir, gboolean builtin, GHashTable *seen, GList **out)
{
	GDir *d = g_dir_open (dir, 0, NULL);
	const char *name;

	if (d == NULL) return;
	while ((name = g_dir_read_name (d)) != NULL) {
		NemoThemeInfo *info;
		GKeyFile *kf;
		char *id;

		if (!g_str_has_suffix (name, ".ini")) continue;
		id = g_strndup (name, strlen (name) - 4);
		if (g_hash_table_contains (seen, id)) { g_free (id); continue; }   /* user dir wins */
		g_hash_table_add (seen, g_strdup (id));
		info = g_new0 (NemoThemeInfo, 1);
		info->id = id;
		info->path = g_build_filename (dir, name, NULL);
		info->builtin = builtin;
		kf = g_key_file_new ();
		if (g_key_file_load_from_file (kf, info->path, G_KEY_FILE_NONE, NULL))
			info->name = g_key_file_get_string (kf, "Theme", "Name", NULL);
		if (info->name == NULL)
			info->name = g_strdup (id);
		g_key_file_free (kf);
		*out = g_list_append (*out, info);
	}
	g_dir_close (d);
}

GList *
nemo_theme_manager_list (void)
{
	GList *out = NULL;
	GHashTable *seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	char *dir;
	NemoThemeInfo *sys;

	dir = nemo_theme_manager_user_dir ();
	collect_dir (dir, FALSE, seen, &out);
	g_free (dir);
	dir = system_dir ();
	collect_dir (dir, TRUE, seen, &out);
	g_free (dir);
	{
		GList *ed = fh_extensions_get_theme_dirs (), *e;

		for (e = ed; e != NULL; e = e->next)
			collect_dir (e->data, TRUE, seen, &out);      /* shipped by an extension: read-only */
		g_list_free_full (ed, g_free);
	}
	g_hash_table_destroy (seen);

	sys = g_new0 (NemoThemeInfo, 1);
	sys->id = g_strdup (NEMO_THEME_SYSTEM_ID);
	sys->name = g_strdup ("System GTK theme (no overrides)");
	sys->builtin = TRUE;
	return g_list_append (out, sys);
}

char *
nemo_theme_manager_make_id (const char *display_name)
{
	GString *s = g_string_new (NULL);
	const char *p;

	for (p = display_name; *p != '\0'; p++) {
		if (g_ascii_isalnum (*p))
			g_string_append_c (s, g_ascii_tolower (*p));
		else if (s->len > 0 && s->str[s->len - 1] != '-')
			g_string_append_c (s, '-');
	}
	while (s->len > 0 && s->str[s->len - 1] == '-')
		g_string_truncate (s, s->len - 1);
	if (s->len == 0)
		g_string_assign (s, "my-theme");
	return g_string_free (s, FALSE);
}

/* ------------------------------------------------------------------ */
/* COSMIC desktop integration                                         */
/* ------------------------------------------------------------------ */

static char *
cosmic_file (const char *schema, const char *key)
{
	return g_build_filename (g_get_user_config_dir (), "cosmic", schema, "v1", key, NULL);
}

static gboolean
cosmic_is_dark (void)
{
	char *f = cosmic_file ("com.system76.CosmicTheme.Mode", "is_dark");
	char *txt = NULL;
	gboolean dark = TRUE;

	if (g_file_get_contents (f, &txt, NULL, NULL))
		dark = g_strstr_len (txt, -1, "false") == NULL;
	g_free (txt);
	g_free (f);
	return dark;
}

/* The accent chosen in COSMIC Settings is stored as RON: (red: 0.4, green: ..., blue: ...) */
static char *
cosmic_accent (gboolean dark)
{
	char *f = cosmic_file (dark ? "com.system76.CosmicTheme.Dark.Builder" : "com.system76.CosmicTheme.Light.Builder", "accent");
	char *txt = NULL;
	char *result = NULL;
	GRegex *re;
	GMatchInfo *mi = NULL;

	if (g_file_get_contents (f, &txt, NULL, NULL)) {
		re = g_regex_new ("red:\\s*([0-9.]+).*?green:\\s*([0-9.]+).*?blue:\\s*([0-9.]+)", G_REGEX_DOTALL, 0, NULL);
		if (g_regex_match (re, txt, 0, &mi)) {
			char *r = g_match_info_fetch (mi, 1), *g = g_match_info_fetch (mi, 2), *b = g_match_info_fetch (mi, 3);

			result = g_strdup_printf ("#%02x%02x%02x",
						  CLAMP ((int) (g_ascii_strtod (r, NULL) * 255 + 0.5), 0, 255),
						  CLAMP ((int) (g_ascii_strtod (g, NULL) * 255 + 0.5), 0, 255),
						  CLAMP ((int) (g_ascii_strtod (b, NULL) * 255 + 0.5), 0, 255));
			g_free (r); g_free (g); g_free (b);
		}
		g_match_info_free (mi);
		g_regex_unref (re);
	}
	g_free (txt);
	g_free (f);
	return result;
}

static char *
readable_on (const char *hex)
{
	GdkRGBA c;

	if (!gdk_rgba_parse (&c, hex))
		return g_strdup ("#ffffff");
	return g_strdup ((0.299 * c.red + 0.587 * c.green + 0.114 * c.blue) > 0.55 ? "#101010" : "#ffffff");
}

/* ------------------------------------------------------------------ */
/* loading and resolving                                              */
/* ------------------------------------------------------------------ */

GKeyFile *
nemo_theme_manager_load (const char *id)
{
	GKeyFile *kf = g_key_file_new ();
	char *path = find_theme_path (id, NULL);

	if (path == NULL || !g_key_file_load_from_file (kf, path, G_KEY_FILE_KEEP_COMMENTS, NULL)) {
		g_key_file_free (kf);
		kf = g_key_file_new ();
		g_key_file_set_string (kf, "Theme", "Name", id);
	}
	g_free (path);
	return kf;
}

GKeyFile *
nemo_theme_manager_resolve (GKeyFile *raw)
{
	GKeyFile *out = g_key_file_new ();
	gboolean follow = g_key_file_get_boolean (raw, "Theme", "FollowCosmic", NULL);
	gboolean dark;
	const Palette *base;
	const char *defaults[9];
	char *accent_override = NULL;
	int i;
	char *name = g_key_file_get_string (raw, "Theme", "Name", NULL);

	if (follow) {
		dark = cosmic_is_dark ();
		accent_override = cosmic_accent (dark);
	} else if (g_key_file_has_key (raw, "Theme", "Dark", NULL)) {
		dark = g_key_file_get_boolean (raw, "Theme", "Dark", NULL);
	} else {
		dark = TRUE;
	}
	base = dark ? &cosmic_dark : &cosmic_light;
	defaults[0] = base->window;   defaults[1] = base->surface; defaults[2] = base->surface_alt;
	defaults[3] = base->text;     defaults[4] = base->text_dim; defaults[5] = base->accent;
	defaults[6] = base->accent_text; defaults[7] = base->border; defaults[8] = base->hover;

	for (i = 0; nemo_theme_color_keys[i] != NULL; i++) {
		const char *key = nemo_theme_color_keys[i];
		char *val = g_key_file_get_string (raw, "Colors", key, NULL);

		if (val == NULL && i == 5 && accent_override != NULL)
			val = g_strdup (accent_override);
		if (val == NULL && i == 6) {
			char *acc = g_key_file_get_string (raw, "Colors", "accent", NULL);

			/* keep text-on-accent readable when only the accent was changed */
			if (acc != NULL || accent_override != NULL)
				val = readable_on (acc != NULL ? acc : accent_override);
			g_free (acc);
		}
		if (val == NULL)
			val = g_strdup (defaults[i]);
		g_key_file_set_string (out, "Colors", key, val);
		g_free (val);
	}
	g_key_file_set_integer (out, "Metrics", "radius", CLAMP (g_key_file_get_integer (raw, "Metrics", "radius", NULL), 0, 32));
	g_key_file_set_integer (out, "Metrics", "gap",
				g_key_file_has_key (raw, "Metrics", "gap", NULL) ? CLAMP (g_key_file_get_integer (raw, "Metrics", "gap", NULL), 0, 32) : 10);
	g_key_file_set_integer (out, "Metrics", "border",
				g_key_file_has_key (raw, "Metrics", "border", NULL) ? CLAMP (g_key_file_get_integer (raw, "Metrics", "border", NULL), 0, 6) : 0);
	g_key_file_set_boolean (out, "Theme", "Dark", dark);
	g_key_file_set_string (out, "Theme", "Name", name != NULL ? name : "Custom");
	g_free (name);
	g_free (accent_override);
	return out;
}

static char *
replace_all (char *s, const char *from, const char *to)
{
	char **parts = g_strsplit (s, from, -1);
	char *r = g_strjoinv (to, parts);

	g_strfreev (parts);
	g_free (s);
	return r;
}

static char *
build_css (GKeyFile *resolved, const char *extra_css_path)
{
	GBytes *tpl = g_resources_lookup_data ("/org/nemo/fileharbor-base.css", 0, NULL);
	GString *css = g_string_new (NULL);
	char *body, *tmp, *extra = NULL;
	int i;

	if (tpl == NULL)
		return g_string_free (css, FALSE);
	for (i = 0; nemo_theme_color_keys[i] != NULL; i++) {
		char *v = g_key_file_get_string (resolved, "Colors", nemo_theme_color_keys[i], NULL);

		g_string_append_printf (css, "@define-color ns_%s %s;\n", nemo_theme_color_keys[i], v);
		g_free (v);
	}
	/* derived, not themeable: a red that reads on either tone */
	g_string_append_printf (css, "@define-color ns_error %s;\n",
				g_key_file_get_boolean (resolved, "Theme", "Dark", NULL) ? "#ff7b72" : "#b3261e");
	body = g_strndup (g_bytes_get_data (tpl, NULL), g_bytes_get_size (tpl));
	g_bytes_unref (tpl);
	tmp = g_strdup_printf ("%d", g_key_file_get_integer (resolved, "Metrics", "radius", NULL));
	body = replace_all (body, "%RADIUS%", tmp); g_free (tmp);
	tmp = g_strdup_printf ("%d", g_key_file_get_integer (resolved, "Metrics", "gap", NULL));
	body = replace_all (body, "%GAP%", tmp); g_free (tmp);
	tmp = g_strdup_printf ("%d", g_key_file_get_integer (resolved, "Metrics", "border", NULL));
	body = replace_all (body, "%BORDER%", tmp); g_free (tmp);
	g_string_append (css, body);
	g_free (body);
	if (extra_css_path != NULL && g_file_get_contents (extra_css_path, &extra, NULL, NULL)) {
		g_string_append_printf (css, "\n/* theme extra css */\n%s\n", extra);
		g_free (extra);
	}
	return g_string_free (css, FALSE);
}

/* ------------------------------------------------------------------ */
/* applying                                                           */
/* ------------------------------------------------------------------ */

static void
remove_provider (void)
{
	if (provider != NULL) {
		gtk_style_context_remove_provider_for_screen (gdk_screen_get_default (), GTK_STYLE_PROVIDER (provider));
		g_clear_object (&provider);
	}
}

static void
apply_keyfile (GKeyFile *raw, const char *extra_css_path)
{
	GKeyFile *resolved = nemo_theme_manager_resolve (raw);
	char *css = build_css (resolved, extra_css_path);
	GError *error = NULL;
	GtkCssProvider *fresh = gtk_css_provider_new ();

	if (!gtk_css_provider_load_from_data (fresh, css, -1, &error)) {
		g_warning ("Theme CSS error: %s", error->message);
		g_clear_error (&error);
		g_object_unref (fresh);
	} else {
		remove_provider ();
		provider = fresh;
		gtk_style_context_add_provider_for_screen (gdk_screen_get_default (), GTK_STYLE_PROVIDER (provider),
							   GTK_STYLE_PROVIDER_PRIORITY_USER);
		g_object_set (gtk_settings_get_default (), "gtk-application-prefer-dark-theme",
			      g_key_file_get_boolean (resolved, "Theme", "Dark", NULL), NULL);
	}
	g_free (css);
	g_key_file_free (resolved);
}

static void
apply_saved_theme (void)
{
	const char *id = nemo_theme_manager_get_current ();
	GKeyFile *kf;
	char *path, *css_path = NULL;

	if (g_strcmp0 (id, NEMO_THEME_SYSTEM_ID) == 0) {
		remove_provider ();
		return;
	}
	kf = nemo_theme_manager_load (id);
	path = find_theme_path (id, NULL);
	if (path != NULL) {
		char *base = g_strndup (path, strlen (path) - 4);

		css_path = g_strdup_printf ("%s.css", base);
		g_free (base);
		if (!g_file_test (css_path, G_FILE_TEST_IS_REGULAR))
			g_clear_pointer (&css_path, g_free);
	}
	apply_keyfile (kf, css_path);
	g_free (css_path);
	g_free (path);
	g_key_file_free (kf);
}

const char *
nemo_theme_manager_get_current (void)
{
	static char *cached = NULL;

	g_free (cached);
	cached = g_settings_get_string (nemo_preferences, NEMO_PREFERENCES_FILEHARBOR_THEME);
	if (cached == NULL || cached[0] == '\0') {
		g_free (cached);
		cached = g_strdup ("cosmic");
	}
	return cached;
}

void
nemo_theme_manager_set_current (const char *id)
{
	g_settings_set_string (nemo_preferences, NEMO_PREFERENCES_FILEHARBOR_THEME, id);
	g_clear_pointer (&preview_kf, g_key_file_free);
	apply_saved_theme ();
}

void
nemo_theme_manager_preview (GKeyFile *kf)
{
	apply_keyfile (kf, NULL);
}

void
nemo_theme_manager_reapply (void)
{
	apply_saved_theme ();
}

gboolean
nemo_theme_manager_save_user (const char *id, GKeyFile *kf, GError **error)
{
	char *dir = nemo_theme_manager_user_dir ();
	char *file = g_strdup_printf ("%s.ini", id);
	char *path = g_build_filename (dir, file, NULL);
	gboolean ok;

	g_mkdir_with_parents (dir, 0755);
	ok = g_key_file_save_to_file (kf, path, error);
	g_free (dir); g_free (file); g_free (path);
	return ok;
}

gboolean
nemo_theme_manager_delete_user (const char *id)
{
	gboolean builtin = TRUE;
	char *path = find_theme_path (id, &builtin);
	gboolean ok = FALSE;

	if (path != NULL && !builtin)
		ok = g_unlink (path) == 0;
	g_free (path);
	return ok;
}

/* ------------------------------------------------------------------ */
/* follow the COSMIC desktop live                                     */
/* ------------------------------------------------------------------ */

static gboolean
reload_timeout (gpointer data)
{
	GKeyFile *kf = nemo_theme_manager_load (nemo_theme_manager_get_current ());

	reload_id = 0;
	if (g_key_file_get_boolean (kf, "Theme", "FollowCosmic", NULL))
		apply_saved_theme ();
	g_key_file_free (kf);
	return G_SOURCE_REMOVE;
}

static void
cosmic_changed (GFileMonitor *m, GFile *f, GFile *o, GFileMonitorEvent ev, gpointer data)
{
	if (reload_id == 0)
		reload_id = g_timeout_add (300, reload_timeout, NULL);
}

static void
watch_cosmic (void)
{
	static const char *dirs[] = { "com.system76.CosmicTheme.Mode", "com.system76.CosmicTheme.Dark.Builder",
				      "com.system76.CosmicTheme.Light.Builder", NULL };
	int i;

	for (i = 0; dirs[i] != NULL; i++) {
		char *path = g_build_filename (g_get_user_config_dir (), "cosmic", dirs[i], "v1", NULL);
		GFile *f = g_file_new_for_path (path);
		GFileMonitor *m = g_file_monitor_directory (f, G_FILE_MONITOR_NONE, NULL, NULL);

		if (m != NULL) {
			g_signal_connect (m, "changed", G_CALLBACK (cosmic_changed), NULL);
			cosmic_monitors = g_list_prepend (cosmic_monitors, m);
		}
		g_object_unref (f);
		g_free (path);
	}
}

void
nemo_theme_manager_init (void)
{
	if (g_getenv ("NEMO_CLASSIC") != NULL)
		return;
	apply_saved_theme ();
	watch_cosmic ();
}

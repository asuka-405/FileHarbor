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
#include "fileharbor-desktop-theme.h"
#include "fileharbor-devmode.h"

const char *const nemo_theme_color_keys[] = {
	"window", "surface", "surface_alt", "text", "text_dim",
	"accent", "accent_text", "border", "hover",
	"surface_raised", "text_faint", "border_strong", "success", "warning", "error", "info", NULL
};
const char *const nemo_theme_color_labels[] = {
	"Window background", "Panels (sidebar, file area)", "Toolbar and headers", "Text",
	"Dim text", "Accent (selection, highlights)", "Text on accent", "Borders",
	"Hover highlight",
	"Raised surfaces (menus, popovers)", "Faint text (hints, disabled)", "Strong borders",
	"Success", "Warning", "Error", "Information", NULL
};

typedef struct {
	const char *window, *surface, *surface_alt, *text, *text_dim, *accent, *accent_text, *border, *hover,
		   *surface_raised, *text_faint, *border_strong, *success, *warning, *error, *info;
} Palette;

/* Fallbacks when a theme leaves a colour out: the FileHarbor palette (neutral, with the coral accent). The
 * shipped themes list every colour, so these only matter for hand-written or damaged theme files. */
static const Palette fallback_dark = {
	"#161413", "#221f1e", "#2d2928", "#eceae9", "#bcb6b3", "#ff7a68", "#12100f", "#413c3a", "#363230",
	"#393432", "#968d88", "#5c5551", "#5fd18a", "#f2b84b", "#ff7b72", "#6cb6ff"
};
static const Palette fallback_light = {
	"#e5e2e1", "#f8f7f7", "#eeedec", "#1c1816", "#4d4541", "#c9372c", "#ffffff", "#d0cac8", "#e3dfde",
	"#ffffff", "#6e6560", "#aaa29e", "#15803d", "#946200", "#b3261e", "#1a63c4"
};

#define CORAL_DARK  "#ff7a68"
#define CORAL_LIGHT "#c9372c"

static void apply_saved_theme (void);

static GtkCssProvider *provider = NULL;
static GtkCssProvider *dev_provider = NULL;
static GFileMonitor *dev_monitor = NULL;
static GKeyFile *preview_kf = NULL;

/* ------------------------------------------------------------------ */
/* paths                                                              */
/* ------------------------------------------------------------------ */

char *
nemo_theme_manager_user_dir (void)
{
	return g_build_filename (g_get_user_config_dir (), "fileharbor", "themes", NULL);
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

/* System first, then FileHarbor, then the other built-in themes by name, then the user's own themes by name */
static gint
theme_order (gconstpointer pa, gconstpointer pb)
{
	const NemoThemeInfo *a = pa, *b = pb;
	int ra = g_strcmp0 (a->id, "system") == 0 ? 0 : g_strcmp0 (a->id, "fileharbor") == 0 ? 1 : a->builtin ? 2 : 3;
	int rb = g_strcmp0 (b->id, "system") == 0 ? 0 : g_strcmp0 (b->id, "fileharbor") == 0 ? 1 : b->builtin ? 2 : 3;

	if (ra != rb)
		return ra - rb;
	return g_utf8_collate (a->name, b->name);
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
	out = g_list_sort (out, theme_order);

	sys = g_new0 (NemoThemeInfo, 1);
	sys->id = g_strdup (NEMO_THEME_SYSTEM_ID);
	sys->name = g_strdup ("Plain GTK theme (no overrides)");
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
/* colour helpers                                                     */
/* ------------------------------------------------------------------ */

static char *
readable_on (const char *hex)
{
	GdkRGBA c;

	if (!gdk_rgba_parse (&c, hex))
		return g_strdup ("#ffffff");
	return g_strdup ((0.299 * c.red + 0.587 * c.green + 0.114 * c.blue) > 0.55 ? "#12100f" : "#ffffff");
}

/* a + (b - a) * t, as "#rrggbb" */
static char *
mix_hex (const char *a, const char *b, double t)
{
	GdkRGBA ca, cb;

	if (!gdk_rgba_parse (&ca, a) || !gdk_rgba_parse (&cb, b))
		return g_strdup (a);
	return g_strdup_printf ("#%02x%02x%02x",
				CLAMP ((int) ((ca.red + (cb.red - ca.red) * t) * 255 + 0.5), 0, 255),
				CLAMP ((int) ((ca.green + (cb.green - ca.green) * t) * 255 + 0.5), 0, 255),
				CLAMP ((int) ((ca.blue + (cb.blue - ca.blue) * t) * 255 + 0.5), 0, 255));
}

static const char *
palette_value (const Palette *p, int i)
{
	switch (i) {
	case 0: return p->window;         case 1: return p->surface;       case 2: return p->surface_alt;
	case 3: return p->text;           case 4: return p->text_dim;      case 5: return p->accent;
	case 6: return p->accent_text;    case 7: return p->border;        case 8: return p->hover;
	case 9: return p->surface_raised; case 10: return p->text_faint;   case 11: return p->border_strong;
	case 12: return p->success;       case 13: return p->warning;      case 14: return p->error;
	default: return p->info;
	}
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

static gboolean
file_has_variants (GKeyFile *kf)
{
	return g_key_file_has_group (kf, "Dark") || g_key_file_has_group (kf, "Light");
}

/* The colour group to read for this variant: [Dark] or [Light], or the single [Colors] group of an older theme. */
static const char *
colour_group (GKeyFile *kf, gboolean dark)
{
	const char *want = dark ? "Dark" : "Light";
	const char *other = dark ? "Light" : "Dark";

	if (g_key_file_has_group (kf, want))
		return want;
	if (g_key_file_has_group (kf, "Colors"))
		return "Colors";
	if (g_key_file_has_group (kf, other))
		return other;
	return NULL;
}

const char *
nemo_theme_manager_get_mode (void)
{
	static char *cached = NULL;

	g_free (cached);
	cached = g_settings_get_string (nemo_preferences, NEMO_PREFERENCES_FILEHARBOR_THEME_MODE);
	if (g_strcmp0 (cached, "dark") != 0 && g_strcmp0 (cached, "light") != 0) {
		g_free (cached);
		cached = g_strdup ("system");
	}
	return cached;
}

void
nemo_theme_manager_set_mode (const char *mode)
{
	g_settings_set_string (nemo_preferences, NEMO_PREFERENCES_FILEHARBOR_THEME_MODE, mode);
	g_clear_pointer (&preview_kf, g_key_file_free);
	apply_saved_theme ();
}

gboolean
nemo_theme_manager_get_desktop_accent (void)
{
	return g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_FILEHARBOR_THEME_DESKTOP_ACCENT);
}

void
nemo_theme_manager_set_desktop_accent (gboolean use)
{
	g_settings_set_boolean (nemo_preferences, NEMO_PREFERENCES_FILEHARBOR_THEME_DESKTOP_ACCENT, use);
	g_clear_pointer (&preview_kf, g_key_file_free);
	apply_saved_theme ();
}

static const char *
choice_setting (const char *key, const char *const *allowed, const char *fallback)
{
	static GHashTable *cache = NULL;
	char *v = g_settings_get_string (nemo_preferences, key);
	int i;

	if (cache == NULL)
		cache = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_free);
	for (i = 0; allowed[i] != NULL; i++) {
		if (g_strcmp0 (v, allowed[i]) == 0) {
			g_hash_table_insert (cache, (gpointer) key, v);
			return v;
		}
	}
	g_free (v);
	g_hash_table_insert (cache, (gpointer) key, g_strdup (fallback));
	return g_hash_table_lookup (cache, key);
}

static const char *const roundness_values[] = { "theme", "square", "soft", "rounded", "round", NULL };
static const char *const spaciousness_values[] = { "compact", "default", "comfortable", "spacious", NULL };

const char *
nemo_theme_manager_get_roundness (void)
{
	return choice_setting (NEMO_PREFERENCES_FILEHARBOR_ROUNDNESS, roundness_values, "theme");
}

void
nemo_theme_manager_set_roundness (const char *roundness)
{
	g_settings_set_string (nemo_preferences, NEMO_PREFERENCES_FILEHARBOR_ROUNDNESS, roundness);   /* the watcher in init applies it */
}

const char *
nemo_theme_manager_get_spaciousness (void)
{
	return choice_setting (NEMO_PREFERENCES_FILEHARBOR_SPACIOUSNESS, spaciousness_values, "default");
}

void
nemo_theme_manager_set_spaciousness (const char *spaciousness)
{
	g_settings_set_string (nemo_preferences, NEMO_PREFERENCES_FILEHARBOR_SPACIOUSNESS, spaciousness);
}

GKeyFile *
nemo_theme_manager_resolve (GKeyFile *raw)
{
	GKeyFile *out = g_key_file_new ();
	GKeyFile *base = NULL;               /* where the colours come from: the theme itself, or the desktop's theme */
	const char *mode = nemo_theme_manager_get_mode ();
	gboolean adaptive = g_key_file_get_boolean (raw, "Theme", "Adaptive", NULL);
	gboolean legacy_follow = g_key_file_get_boolean (raw, "Theme", "FollowCosmic", NULL);
	gboolean fixed_variant = !file_has_variants (raw) && !adaptive && !legacy_follow;
	gboolean dark;
	const char *group;
	char *accent_override = NULL;
	char *name = g_key_file_get_string (raw, "Theme", "Name", NULL);
	int i;

	/* light or dark */
	if (fixed_variant)
		dark = g_key_file_has_key (raw, "Theme", "Dark", NULL) ? g_key_file_get_boolean (raw, "Theme", "Dark", NULL) : TRUE;
	else if (g_strcmp0 (mode, "dark") == 0)
		dark = TRUE;
	else if (g_strcmp0 (mode, "light") == 0)
		dark = FALSE;
	else
		dark = fh_desktop_prefers_dark ();

	/* the System theme borrows the look of the desktop it runs on, with the coral accent unless the user asked
	 * for the desktop's own accent colour */
	if (adaptive || legacy_follow) {
		const char *de = legacy_follow ? "cosmic" : fh_desktop_id ();
		const char *id = g_strcmp0 (de, "generic") == 0 ? "fileharbor" : de;

		base = nemo_theme_manager_load (id);
		if (!file_has_variants (base) && !g_key_file_has_group (base, "Colors")) {
			g_key_file_free (base);
			base = nemo_theme_manager_load ("fileharbor");
		}
		if (legacy_follow || nemo_theme_manager_get_desktop_accent ())
			accent_override = fh_desktop_accent ();
		if (accent_override == NULL) {
			GKeyFile *fh = nemo_theme_manager_load ("fileharbor");
			const char *g = colour_group (fh, dark);

			accent_override = g != NULL ? g_key_file_get_string (fh, g, "accent", NULL) : NULL;
			g_key_file_free (fh);
			if (accent_override == NULL)
				accent_override = g_strdup (dark ? CORAL_DARK : CORAL_LIGHT);
		}
	} else {
		base = g_key_file_ref (raw);
	}

	group = colour_group (base, dark);
	{
		const Palette *fb = dark ? &fallback_dark : &fallback_light;
		char *vals[16] = { NULL };

		for (i = 0; nemo_theme_color_keys[i] != NULL; i++)
			if (group != NULL)
				vals[i] = g_key_file_get_string (base, group, nemo_theme_color_keys[i], NULL);
		/* the colours of the raw theme win over the desktop's (a user tweak of the System theme) */
		for (i = 0; nemo_theme_color_keys[i] != NULL; i++) {
			if (base != raw && g_key_file_has_key (raw, "Colors", nemo_theme_color_keys[i], NULL)) {
				g_free (vals[i]);
				vals[i] = g_key_file_get_string (raw, "Colors", nemo_theme_color_keys[i], NULL);
			}
		}
		if (accent_override != NULL && (base == raw || !g_key_file_has_key (raw, "Colors", "accent", NULL))) {
			g_free (vals[5]);
			vals[5] = g_strdup (accent_override);
			if (!(base != raw && g_key_file_has_key (raw, "Colors", "accent_text", NULL))) {
				g_free (vals[6]);
				vals[6] = NULL;                    /* recomputed for the new accent below */
			}
		}
		/* anything still missing: derive it from what the theme did give, or fall back to the FileHarbor palette */
		if (vals[6] == NULL)
			vals[6] = readable_on (vals[5] != NULL ? vals[5] : palette_value (fb, 5));
		for (i = 0; nemo_theme_color_keys[i] != NULL; i++) {
			if (vals[i] != NULL)
				continue;
			switch (i) {
			case 9:  vals[i] = vals[2] != NULL && vals[3] != NULL ? mix_hex (vals[2], vals[3], 0.07) : g_strdup (palette_value (fb, i)); break;
			case 10: vals[i] = vals[4] != NULL && vals[1] != NULL ? mix_hex (vals[4], vals[1], 0.35) : g_strdup (palette_value (fb, i)); break;
			case 11: vals[i] = vals[7] != NULL && vals[3] != NULL ? mix_hex (vals[7], vals[3], 0.25) : g_strdup (palette_value (fb, i)); break;
			default: vals[i] = g_strdup (palette_value (fb, i)); break;
			}
		}
		for (i = 0; nemo_theme_color_keys[i] != NULL; i++) {
			g_key_file_set_string (out, "Colors", nemo_theme_color_keys[i], vals[i]);
			g_free (vals[i]);
		}
	}

	/* metrics: the theme's own, else the desktop theme's, else the defaults */
	{
		GKeyFile *m = g_key_file_has_group (raw, "Metrics") ? raw : base;
		int radius = g_key_file_has_key (m, "Metrics", "radius", NULL) ? g_key_file_get_integer (m, "Metrics", "radius", NULL) : 0;
		int gap = g_key_file_has_key (m, "Metrics", "gap", NULL) ? g_key_file_get_integer (m, "Metrics", "gap", NULL) : 8;
		int border = g_key_file_has_key (m, "Metrics", "border", NULL) ? g_key_file_get_integer (m, "Metrics", "border", NULL) : 0;

		g_key_file_set_integer (out, "Metrics", "radius", CLAMP (radius, 0, 32));
		g_key_file_set_integer (out, "Metrics", "gap", CLAMP (gap, 0, 32));
		g_key_file_set_integer (out, "Metrics", "border", CLAMP (border, 0, 6));
	}
	g_key_file_set_boolean (out, "Theme", "Dark", dark);
	g_key_file_set_string (out, "Theme", "Name", name != NULL ? name : "Custom");
	g_free (name);
	g_free (accent_override);
	g_key_file_unref (base);
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
	body = g_strndup (g_bytes_get_data (tpl, NULL), g_bytes_get_size (tpl));
	g_bytes_unref (tpl);
	{
		/* roundness and spaciousness are the user's own choice and apply on top of every theme */
		const char *round = nemo_theme_manager_get_roundness ();
		const char *space = nemo_theme_manager_get_spaciousness ();
		int radius = g_key_file_get_integer (resolved, "Metrics", "radius", NULL);
		int gap = g_key_file_get_integer (resolved, "Metrics", "gap", NULL);
		/* control, row, bar, bar with header, then the 4/8/12 spacing steps and the panel gap change */
		int ctrl = 32, row = 28, bar = 40, bar2 = 44, s1 = 4, s2 = 8, s3 = 12, dgap = 0;

		if (g_strcmp0 (round, "square") == 0) radius = 0;
		else if (g_strcmp0 (round, "soft") == 0) radius = 4;
		else if (g_strcmp0 (round, "rounded") == 0) radius = 8;
		else if (g_strcmp0 (round, "round") == 0) radius = 12;
		if (g_strcmp0 (space, "compact") == 0) {
			ctrl = 28; row = 24; bar = 36; bar2 = 40; s1 = 2; s2 = 6; s3 = 10; dgap = -2;
		} else if (g_strcmp0 (space, "comfortable") == 0) {
			ctrl = 36; row = 32; bar = 44; bar2 = 48; s1 = 6; s2 = 10; s3 = 14; dgap = 2;
		} else if (g_strcmp0 (space, "spacious") == 0) {
			ctrl = 40; row = 36; bar = 48; bar2 = 52; s1 = 8; s2 = 12; s3 = 18; dgap = 4;
		}
		gap = CLAMP (gap + dgap, 0, 32);
		tmp = g_strdup_printf ("%d", radius); body = replace_all (body, "%RADIUS%", tmp); g_free (tmp);
		tmp = g_strdup_printf ("%d", gap); body = replace_all (body, "%GAP%", tmp); g_free (tmp);
		tmp = g_strdup_printf ("%d", ctrl); body = replace_all (body, "%CTRL%", tmp); g_free (tmp);
		tmp = g_strdup_printf ("%d", row); body = replace_all (body, "%ROW%", tmp); g_free (tmp);
		tmp = g_strdup_printf ("%d", bar); body = replace_all (body, "%BAR%", tmp); g_free (tmp);
		tmp = g_strdup_printf ("%d", bar2); body = replace_all (body, "%BAR2%", tmp); g_free (tmp);
		tmp = g_strdup_printf ("%d", s1); body = replace_all (body, "%S1%", tmp); g_free (tmp);
		tmp = g_strdup_printf ("%d", s2); body = replace_all (body, "%S2%", tmp); g_free (tmp);
		tmp = g_strdup_printf ("%d", s3); body = replace_all (body, "%S3%", tmp); g_free (tmp);
	}
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
/* developer mode: dev.css hot reload and the theme snapshot          */
/* ------------------------------------------------------------------ */

static void
dev_css_reload (void)
{
	GdkScreen *screen = gdk_screen_get_default ();

	if (dev_provider != NULL) {
		gtk_style_context_remove_provider_for_screen (screen, GTK_STYLE_PROVIDER (dev_provider));
		g_clear_object (&dev_provider);
	}
	if (fh_devmode_enabled ()) {
		char *path = fh_devmode_css_path ();

		if (g_file_test (path, G_FILE_TEST_IS_REGULAR)) {
			GError *error = NULL;

			dev_provider = gtk_css_provider_new ();
			if (!gtk_css_provider_load_from_path (dev_provider, path, &error)) {
				g_warning ("dev.css: %s", error->message);
				g_clear_error (&error);
			}
			gtk_style_context_add_provider_for_screen (screen, GTK_STYLE_PROVIDER (dev_provider),
								   GTK_STYLE_PROVIDER_PRIORITY_USER + 1);
			g_message ("dev.css loaded from %s", path);
		}
		g_free (path);
	}
}

static void
dev_css_changed (GFileMonitor *m, GFile *f, GFile *o, GFileMonitorEvent ev, gpointer data)
{
	if (ev == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT || ev == G_FILE_MONITOR_EVENT_CREATED || ev == G_FILE_MONITOR_EVENT_DELETED)
		dev_css_reload ();
}

static void
dev_watch_css (void)
{
	if (dev_monitor == NULL) {
		char *path = fh_devmode_css_path ();
		char *dir = g_path_get_dirname (path);
		GFile *file;

		g_mkdir_with_parents (dir, 0755);
		file = g_file_new_for_path (path);
		dev_monitor = g_file_monitor_file (file, G_FILE_MONITOR_NONE, NULL, NULL);
		if (dev_monitor != NULL)
			g_signal_connect (dev_monitor, "changed", G_CALLBACK (dev_css_changed), NULL);
		g_object_unref (file);
		g_free (dir);
		g_free (path);
	}
}

/* What the developer panel shows: the theme as resolved, and what was detected on the desktop. */
static void
dev_write_snapshot (const char *theme_id, GKeyFile *resolved)
{
	GString *j = g_string_new ("{\n");
	char *dir = fh_devmode_debug_dir (), *path, *accent = fh_desktop_accent ();
	int i;

	g_string_append_printf (j, "  \"theme\": \"%s\",\n  \"mode\": \"%s\",\n  \"dark\": %s,\n",
				theme_id != NULL ? theme_id : "", nemo_theme_manager_get_mode (),
				g_key_file_get_boolean (resolved, "Theme", "Dark", NULL) ? "true" : "false");
	g_string_append_printf (j, "  \"roundness\": \"%s\",\n  \"spaciousness\": \"%s\",\n",
				nemo_theme_manager_get_roundness (), nemo_theme_manager_get_spaciousness ());
	g_string_append_printf (j, "  \"desktop\": \"%s\",\n  \"desktop_prefers_dark\": %s,\n  \"desktop_accent\": %s%s%s,\n",
				fh_desktop_id (), fh_desktop_prefers_dark () ? "true" : "false",
				accent != NULL ? "\"" : "", accent != NULL ? accent : "null", accent != NULL ? "\"" : "");
	g_string_append_printf (j, "  \"use_desktop_accent\": %s,\n  \"colors\": {",
				nemo_theme_manager_get_desktop_accent () ? "true" : "false");
	for (i = 0; nemo_theme_color_keys[i] != NULL; i++) {
		char *v = g_key_file_get_string (resolved, "Colors", nemo_theme_color_keys[i], NULL);

		g_string_append_printf (j, "%s\n    \"%s\": \"%s\"", i > 0 ? "," : "", nemo_theme_color_keys[i], v != NULL ? v : "");
		g_free (v);
	}
	g_string_append_printf (j, "\n  },\n  \"metrics\": { \"radius\": %d, \"gap\": %d, \"border\": %d }\n}\n",
				g_key_file_get_integer (resolved, "Metrics", "radius", NULL),
				g_key_file_get_integer (resolved, "Metrics", "gap", NULL),
				g_key_file_get_integer (resolved, "Metrics", "border", NULL));
	g_mkdir_with_parents (dir, 0755);
	path = g_build_filename (dir, "theme.json", NULL);
	g_file_set_contents (path, j->str, -1, NULL);
	g_free (path); g_free (dir); g_free (accent);
	g_string_free (j, TRUE);
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
		if (fh_devmode_enabled ()) {
			dev_write_snapshot (nemo_theme_manager_get_current (), resolved);
			dev_watch_css ();
		}
		dev_css_reload ();
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
		dev_css_reload ();
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
		cached = g_strdup ("system");
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
/* follow the desktop live                                            */
/* ------------------------------------------------------------------ */

/* The System theme and the "follow the desktop" mode depend on the desktop's settings, so redo the theme when
 * they change (light/dark switch, accent colour, GTK theme). */
static void
desktop_changed (void)
{
	GKeyFile *kf = nemo_theme_manager_load (nemo_theme_manager_get_current ());

	if (g_key_file_get_boolean (kf, "Theme", "Adaptive", NULL) ||
	    g_key_file_get_boolean (kf, "Theme", "FollowCosmic", NULL) ||
	    (g_strcmp0 (nemo_theme_manager_get_mode (), "system") == 0 && g_strcmp0 (nemo_theme_manager_get_current (), NEMO_THEME_SYSTEM_ID) != 0))
		apply_saved_theme ();
	g_key_file_free (kf);
}

static void
shape_changed (GSettings *settings, const char *key, gpointer data)
{
	g_clear_pointer (&preview_kf, g_key_file_free);
	apply_saved_theme ();
}

void
nemo_theme_manager_init (void)
{
	if (g_getenv ("NEMO_CLASSIC") != NULL)
		return;
	apply_saved_theme ();
	fh_desktop_watch (G_CALLBACK (desktop_changed));
	/* also when changed from outside (fileharbor-setup, gsettings, the developer panel) */
	g_signal_connect (nemo_preferences, "changed::" NEMO_PREFERENCES_FILEHARBOR_ROUNDNESS, G_CALLBACK (shape_changed), NULL);
	g_signal_connect (nemo_preferences, "changed::" NEMO_PREFERENCES_FILEHARBOR_SPACIOUSNESS, G_CALLBACK (shape_changed), NULL);
}

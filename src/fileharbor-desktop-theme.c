/* fileharbor-desktop-theme.c - see the header. New file, not in upstream. */

#include <config.h>

#include "fileharbor-desktop-theme.h"

#include <gio/gio.h>
#include <string.h>
#include <stdlib.h>

#define PORTAL_NAME  "org.freedesktop.portal.Desktop"
#define PORTAL_PATH  "/org/freedesktop/portal/desktop"
#define PORTAL_IFACE "org.freedesktop.portal.Settings"

static GCallback changed_cb = NULL;
static guint changed_id = 0;

/* ------------------------------------------------------------------ */
/* helpers                                                            */
/* ------------------------------------------------------------------ */

static char *
rgb_hex (double r, double g, double b)
{
	return g_strdup_printf ("#%02x%02x%02x",
				CLAMP ((int) (r * 255 + 0.5), 0, 255),
				CLAMP ((int) (g * 255 + 0.5), 0, 255),
				CLAMP ((int) (b * 255 + 0.5), 0, 255));
}

static gboolean
contains_nocase (const char *haystack, const char *needle)
{
	char *h, *n;
	gboolean found;

	if (haystack == NULL)
		return FALSE;
	h = g_ascii_strdown (haystack, -1);
	n = g_ascii_strdown (needle, -1);
	found = strstr (h, n) != NULL;
	g_free (h);
	g_free (n);
	return found;
}

static GKeyFile *
load_config_file (const char *relative)
{
	GKeyFile *kf = g_key_file_new ();
	char *path = g_build_filename (g_get_user_config_dir (), relative, NULL);

	if (!g_key_file_load_from_file (kf, path, G_KEY_FILE_NONE, NULL))
		g_clear_pointer (&kf, g_key_file_free);
	g_free (path);
	return kf;
}

static GSettings *
settings_if_present (const char *schema_id)
{
	GSettingsSchemaSource *source = g_settings_schema_source_get_default ();
	GSettingsSchema *schema;
	GSettings *s;

	if (source == NULL)
		return NULL;
	schema = g_settings_schema_source_lookup (source, schema_id, TRUE);
	if (schema == NULL)
		return NULL;
	s = g_settings_new (schema_id);
	g_settings_schema_unref (schema);
	return s;
}

/* ------------------------------------------------------------------ */
/* which desktop                                                      */
/* ------------------------------------------------------------------ */

const char *
fh_desktop_id (void)
{
	static const char *cached = NULL;
	const char *vars[] = { "XDG_CURRENT_DESKTOP", "XDG_SESSION_DESKTOP", "DESKTOP_SESSION", NULL };
	int i;

	if (cached != NULL)
		return cached;
	cached = "generic";
	for (i = 0; vars[i] != NULL; i++) {
		const char *v = g_getenv (vars[i]);

		if (v == NULL || v[0] == '\0')
			continue;
		if (contains_nocase (v, "cosmic")) { cached = "cosmic"; break; }
		if (contains_nocase (v, "hyprland")) { cached = "hyprland"; break; }
		if (contains_nocase (v, "kde") || contains_nocase (v, "plasma")) { cached = "kde"; break; }
		if (contains_nocase (v, "cinnamon")) { cached = "cinnamon"; break; }
		if (contains_nocase (v, "gnome")) { cached = "gnome"; break; }
	}
	return cached;
}

/* ------------------------------------------------------------------ */
/* COSMIC                                                             */
/* ------------------------------------------------------------------ */

static char *
cosmic_file (const char *schema, const char *key)
{
	return g_build_filename (g_get_user_config_dir (), "cosmic", schema, "v1", key, NULL);
}

static gboolean
cosmic_read_dark (gboolean *dark)
{
	char *f = cosmic_file ("com.system76.CosmicTheme.Mode", "is_dark");
	char *txt = NULL;
	gboolean ok = g_file_get_contents (f, &txt, NULL, NULL);

	if (ok)
		*dark = g_strstr_len (txt, -1, "false") == NULL;
	g_free (txt);
	g_free (f);
	return ok;
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

			result = rgb_hex (g_ascii_strtod (r, NULL), g_ascii_strtod (g, NULL), g_ascii_strtod (b, NULL));
			g_free (r); g_free (g); g_free (b);
		}
		g_match_info_free (mi);
		g_regex_unref (re);
	}
	g_free (txt);
	g_free (f);
	return result;
}

/* ------------------------------------------------------------------ */
/* XDG settings portal                                                */
/* ------------------------------------------------------------------ */

static GVariant *
portal_read (const char *key)
{
	GDBusConnection *bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);
	GVariant *res, *v;

	if (bus == NULL)
		return NULL;
	res = g_dbus_connection_call_sync (bus, PORTAL_NAME, PORTAL_PATH, PORTAL_IFACE, "Read",
					   g_variant_new ("(ss)", "org.freedesktop.appearance", key),
					   G_VARIANT_TYPE ("(v)"), G_DBUS_CALL_FLAGS_NONE, 600, NULL, NULL);
	if (res == NULL)
		return NULL;
	g_variant_get (res, "(v)", &v);
	g_variant_unref (res);
	while (v != NULL && g_variant_is_of_type (v, G_VARIANT_TYPE_VARIANT)) {
		GVariant *inner = g_variant_get_variant (v);

		g_variant_unref (v);
		v = inner;
	}
	return v;
}

/* 1 dark, 2 light, 0 no preference, -1 unknown */
static int
portal_color_scheme (void)
{
	GVariant *v = portal_read ("color-scheme");
	int result = -1;

	if (v != NULL) {
		if (g_variant_is_of_type (v, G_VARIANT_TYPE_UINT32))
			result = (int) g_variant_get_uint32 (v);
		g_variant_unref (v);
	}
	return result;
}

static char *
portal_accent (void)
{
	GVariant *v = portal_read ("accent-color");
	char *hex = NULL;

	if (v != NULL) {
		if (g_variant_is_of_type (v, G_VARIANT_TYPE ("(ddd)"))) {
			double r, g, b;

			g_variant_get (v, "(ddd)", &r, &g, &b);
			if (r >= 0 && r <= 1 && g >= 0 && g <= 1 && b >= 0 && b <= 1)
				hex = rgb_hex (r, g, b);
		}
		g_variant_unref (v);
	}
	return hex;
}

/* ------------------------------------------------------------------ */
/* GNOME, Cinnamon, KDE, GTK                                          */
/* ------------------------------------------------------------------ */

static const struct { const char *name, *hex; } gnome_accents[] = {
	{ "blue", "#3584e4" }, { "teal", "#2190a4" }, { "green", "#3a944a" }, { "yellow", "#c88800" },
	{ "orange", "#ed5b00" }, { "red", "#e62d42" }, { "pink", "#d56199" }, { "purple", "#9141ac" },
	{ "slate", "#6f8396" }, { NULL, NULL }
};

static int
gnome_color_scheme (void)
{
	GSettings *s = settings_if_present ("org.gnome.desktop.interface");
	char *v;
	int result = -1;

	if (s == NULL)
		return -1;
	v = g_settings_get_string (s, "color-scheme");
	if (g_strcmp0 (v, "prefer-dark") == 0) result = 1;
	else if (g_strcmp0 (v, "prefer-light") == 0) result = 2;
	g_free (v);
	g_object_unref (s);
	return result;
}

static char *
gnome_accent (void)
{
	GSettings *s = settings_if_present ("org.gnome.desktop.interface");
	char *name, *hex = NULL;
	int i;

	if (s == NULL)
		return NULL;
	{
		GSettingsSchema *schema = NULL;
		g_object_get (s, "settings-schema", &schema, NULL);
		if (schema == NULL || !g_settings_schema_has_key (schema, "accent-color")) {
			g_clear_pointer (&schema, g_settings_schema_unref);
			g_object_unref (s);
			return NULL;
		}
		g_settings_schema_unref (schema);
	}
	name = g_settings_get_string (s, "accent-color");
	for (i = 0; gnome_accents[i].name != NULL; i++)
		if (g_strcmp0 (name, gnome_accents[i].name) == 0)
			hex = g_strdup (gnome_accents[i].hex);
	g_free (name);
	g_object_unref (s);
	return hex;
}

static int
cinnamon_dark (void)
{
	GSettings *s = settings_if_present ("org.cinnamon.desktop.interface");
	char *theme;
	int result = -1;

	if (s == NULL)
		return -1;
	theme = g_settings_get_string (s, "gtk-theme");
	if (theme != NULL && theme[0] != '\0')
		result = contains_nocase (theme, "dark") ? 1 : 2;
	g_free (theme);
	g_object_unref (s);
	return result;
}

static gboolean
parse_rgb_triplet (const char *s, double *r, double *g, double *b)
{
	char **p = s != NULL ? g_strsplit (s, ",", 4) : NULL;
	gboolean ok = p != NULL && g_strv_length (p) >= 3;

	if (ok) {
		*r = atoi (p[0]) / 255.0; *g = atoi (p[1]) / 255.0; *b = atoi (p[2]) / 255.0;
	}
	g_strfreev (p);
	return ok;
}

static int
kde_dark (void)
{
	GKeyFile *kf = load_config_file ("kdeglobals");
	char *scheme, *bg;
	int result = -1;

	if (kf == NULL)
		return -1;
	scheme = g_key_file_get_string (kf, "General", "ColorScheme", NULL);
	if (scheme != NULL && scheme[0] != '\0' && contains_nocase (scheme, "dark")) {
		result = 1;
	} else {
		double r, g, b;

		bg = g_key_file_get_string (kf, "Colors:Window", "BackgroundNormal", NULL);
		if (parse_rgb_triplet (bg, &r, &g, &b))
			result = (0.299 * r + 0.587 * g + 0.114 * b) < 0.5 ? 1 : 2;
		else if (scheme != NULL && scheme[0] != '\0')
			result = 2;
		g_free (bg);
	}
	g_free (scheme);
	g_key_file_free (kf);
	return result;
}

static char *
kde_accent (void)
{
	GKeyFile *kf = load_config_file ("kdeglobals");
	char *v, *hex = NULL;
	double r, g, b;

	if (kf == NULL)
		return NULL;
	v = g_key_file_get_string (kf, "General", "AccentColor", NULL);
	if (parse_rgb_triplet (v, &r, &g, &b))
		hex = rgb_hex (r, g, b);
	g_free (v);
	g_key_file_free (kf);
	return hex;
}

static int
gtk_settings_ini_dark (void)
{
	GKeyFile *kf = load_config_file ("gtk-3.0/settings.ini");
	char *name;
	int result = -1;

	if (kf == NULL)
		return -1;
	if (g_key_file_has_key (kf, "Settings", "gtk-application-prefer-dark-theme", NULL)) {
		result = g_key_file_get_boolean (kf, "Settings", "gtk-application-prefer-dark-theme", NULL) ? 1 : 2;
	}
	name = g_key_file_get_string (kf, "Settings", "gtk-theme-name", NULL);
	if (name != NULL && name[0] != '\0' && result != 1)
		result = contains_nocase (name, "dark") ? 1 : 2;
	g_free (name);
	g_key_file_free (kf);
	return result;
}

/* ------------------------------------------------------------------ */
/* the questions                                                      */
/* ------------------------------------------------------------------ */

gboolean
fh_desktop_prefers_dark (void)
{
	const char *de = fh_desktop_id ();
	gboolean dark;
	int v;

	if (g_strcmp0 (de, "cosmic") == 0 && cosmic_read_dark (&dark))
		return dark;
	if (g_strcmp0 (de, "kde") == 0 && (v = kde_dark ()) > 0)
		return v == 1;
	if ((v = portal_color_scheme ()) > 0)
		return v == 1;
	if ((v = gnome_color_scheme ()) > 0)
		return v == 1;
	if (g_strcmp0 (de, "cinnamon") == 0 && (v = cinnamon_dark ()) > 0)
		return v == 1;
	if ((v = kde_dark ()) > 0 && g_strcmp0 (de, "kde") == 0)
		return v == 1;
	if ((v = gtk_settings_ini_dark ()) > 0)
		return v == 1;
	if (contains_nocase (g_getenv ("GTK_THEME"), "dark"))
		return TRUE;
	if (contains_nocase (g_getenv ("GTK_THEME"), "light"))
		return FALSE;
	return TRUE;
}

char *
fh_desktop_accent (void)
{
	const char *de = fh_desktop_id ();
	char *hex = NULL;

	if (g_strcmp0 (de, "cosmic") == 0)
		hex = cosmic_accent (fh_desktop_prefers_dark ());
	if (hex == NULL && g_strcmp0 (de, "kde") == 0)
		hex = kde_accent ();
	if (hex == NULL)
		hex = portal_accent ();
	if (hex == NULL && g_strcmp0 (de, "gnome") == 0)
		hex = gnome_accent ();
	return hex;
}

/* ------------------------------------------------------------------ */
/* watching                                                           */
/* ------------------------------------------------------------------ */

static gboolean
debounce_fire (gpointer data)
{
	changed_id = 0;
	if (changed_cb != NULL)
		((void (*)(void)) changed_cb) ();
	return G_SOURCE_REMOVE;
}

static void
something_changed (void)
{
	if (changed_id == 0)
		changed_id = g_timeout_add (300, debounce_fire, NULL);
}

static void
file_changed_cb (GFileMonitor *m, GFile *f, GFile *o, GFileMonitorEvent ev, gpointer data)
{
	something_changed ();
}

static void
watch_path (const char *path, gboolean directory)
{
	GFile *f = g_file_new_for_path (path);
	GFileMonitor *m = directory ? g_file_monitor_directory (f, G_FILE_MONITOR_NONE, NULL, NULL)
				    : g_file_monitor_file (f, G_FILE_MONITOR_NONE, NULL, NULL);

	if (m != NULL)
		g_signal_connect (m, "changed", G_CALLBACK (file_changed_cb), NULL);
	g_object_unref (f);
}

static void
portal_signal (GDBusConnection *c, const char *sender, const char *path, const char *iface,
	       const char *signal, GVariant *params, gpointer data)
{
	const char *ns = NULL;

	g_variant_get_child (params, 0, "&s", &ns);
	if (g_strcmp0 (ns, "org.freedesktop.appearance") == 0)
		something_changed ();
}

static void
settings_changed_cb (GSettings *s, const char *key, gpointer data)
{
	something_changed ();
}

void
fh_desktop_watch (GCallback func)
{
	static gboolean once = FALSE;
	GDBusConnection *bus;
	GSettings *s;
	char *p;
	static const char *cosmic_dirs[] = { "com.system76.CosmicTheme.Mode", "com.system76.CosmicTheme.Dark.Builder",
					     "com.system76.CosmicTheme.Light.Builder", NULL };
	int i;

	if (once)
		return;
	once = TRUE;
	changed_cb = func;

	for (i = 0; cosmic_dirs[i] != NULL; i++) {
		p = g_build_filename (g_get_user_config_dir (), "cosmic", cosmic_dirs[i], "v1", NULL);
		watch_path (p, TRUE);
		g_free (p);
	}
	p = g_build_filename (g_get_user_config_dir (), "kdeglobals", NULL);
	watch_path (p, FALSE);
	g_free (p);
	p = g_build_filename (g_get_user_config_dir (), "gtk-3.0", "settings.ini", NULL);
	watch_path (p, FALSE);
	g_free (p);

	bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);
	if (bus != NULL)
		g_dbus_connection_signal_subscribe (bus, PORTAL_NAME, PORTAL_IFACE, "SettingChanged", PORTAL_PATH, NULL,
						    G_DBUS_SIGNAL_FLAGS_NONE, portal_signal, NULL, NULL);
	if ((s = settings_if_present ("org.gnome.desktop.interface")) != NULL)
		g_signal_connect (s, "changed", G_CALLBACK (settings_changed_cb), NULL);   /* kept alive for the process */
	if ((s = settings_if_present ("org.cinnamon.desktop.interface")) != NULL)
		g_signal_connect (s, "changed", G_CALLBACK (settings_changed_cb), NULL);
}

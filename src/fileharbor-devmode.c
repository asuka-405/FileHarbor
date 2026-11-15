/* fileharbor-devmode.c - see the header. New file, not in upstream. */

#include <config.h>

#include "fileharbor-devmode.h"
#include "nemo-theme-manager.h"
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-debug.h>

#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>

static volatile gint enabled_flag = 0;
static gboolean writer_installed = FALSE;
static GMutex log_lock;
static FILE *log_file = NULL;

#define LOG_LIMIT (8 * 1024 * 1024)

char *
fh_devmode_debug_dir (void)
{
	return g_build_filename (g_get_user_cache_dir (), "fileharbor", "debug", NULL);
}

char *
fh_devmode_css_path (void)
{
	return g_build_filename (g_get_user_config_dir (), "fileharbor", "dev.css", NULL);
}

gboolean
fh_devmode_enabled (void)
{
	return g_atomic_int_get (&enabled_flag) != 0;
}

static const char *
level_name (GLogLevelFlags l)
{
	if (l & G_LOG_LEVEL_ERROR) return "ERROR";
	if (l & G_LOG_LEVEL_CRITICAL) return "CRITICAL";
	if (l & G_LOG_LEVEL_WARNING) return "WARNING";
	if (l & G_LOG_LEVEL_MESSAGE) return "MESSAGE";
	if (l & G_LOG_LEVEL_INFO) return "INFO";
	return "DEBUG";
}

static void
open_log (void)
{
	char *dir = fh_devmode_debug_dir ();
	char *path;
	GStatBuf st;

	g_mkdir_with_parents (dir, 0755);
	path = g_build_filename (dir, "debug.log", NULL);
	if (g_stat (path, &st) == 0 && st.st_size > LOG_LIMIT)
		g_unlink (path);                        /* a runaway log must not fill the disk */
	log_file = fopen (path, "a");
	g_free (path);
	g_free (dir);
}

static GLogWriterOutput
dev_writer (GLogLevelFlags level, const GLogField *fields, gsize n_fields, gpointer data)
{
	if (fh_devmode_enabled ()) {
		const char *domain = NULL, *message = NULL;
		gsize i;

		for (i = 0; i < n_fields; i++) {
			if (g_strcmp0 (fields[i].key, "GLIB_DOMAIN") == 0)
				domain = fields[i].value;
			else if (g_strcmp0 (fields[i].key, "MESSAGE") == 0)
				message = fields[i].value;
		}
		g_mutex_lock (&log_lock);
		if (log_file == NULL)
			open_log ();
		if (log_file != NULL) {
			GDateTime *now = g_date_time_new_now_local ();
			char *stamp = g_date_time_format (now, "%H:%M:%S.%f");

			/* one record per line: a newline inside a message becomes a visible marker */
			char *flat = message != NULL ? g_strdelimit (g_strdup (message), "\n", '\x1f') : g_strdup ("");

			fprintf (log_file, "%.12s\t%s\t%s\t%s\n", stamp, level_name (level), domain != NULL ? domain : "-", flat);
			fflush (log_file);
			g_free (flat);
			g_free (stamp);
			g_date_time_unref (now);
		}
		g_mutex_unlock (&log_lock);
	}
	return g_log_writer_default (level, fields, n_fields, data);
}

static void
install_writer (void)
{
	if (!writer_installed) {
		writer_installed = TRUE;
		g_log_set_writer_func (dev_writer, NULL, NULL);
	}
}

static void
apply_enabled (gboolean on)
{
	g_atomic_int_set (&enabled_flag, on ? 1 : 0);
	install_writer ();
	{
#if GLIB_CHECK_VERSION(2,80,0)
		static const gchar *const all[] = { "all", NULL };

		g_log_writer_default_set_debug_domains (on ? all : NULL);   /* g_debug() output, like G_MESSAGES_DEBUG=all */
#endif
	}
#ifdef ENABLE_DEBUG
	nemo_debug_set_flags (on ? (DebugFlags) 0x1ffffe : 0);
#endif
	if (gdk_display_get_default () != NULL)
		gtk_window_set_interactive_debugging (on);   /* the GTK inspector: Ctrl+Shift+I or Ctrl+Shift+D */
	if (on)
		g_message ("FileHarbor developer mode is on (log: %s)", "~/.cache/fileharbor/debug/debug.log");
}

static void
developer_mode_changed (GSettings *s, const char *key, gpointer data)
{
	apply_enabled (g_getenv ("FILEHARBOR_DEV") != NULL || g_settings_get_boolean (s, "developer-mode"));
	nemo_theme_manager_reapply ();                      /* theme.json and dev.css follow the mode */
}

static void
reload_requested (GSettings *s, const char *key, gpointer data)
{
	g_message ("Developer panel asked for a reload of the theme and its CSS");
	nemo_theme_manager_reapply ();
}

void
fh_devmode_early_init (void)
{
	if (g_getenv ("FILEHARBOR_DEV") != NULL) {
		g_atomic_int_set (&enabled_flag, 1);
		install_writer ();
	}
}

void
fh_devmode_init (void)
{
	gboolean on = g_getenv ("FILEHARBOR_DEV") != NULL ||
		      g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_DEVELOPER_MODE);

	apply_enabled (on);
	g_signal_connect (nemo_preferences, "changed::developer-mode", G_CALLBACK (developer_mode_changed), NULL);
	g_signal_connect (nemo_preferences, "changed::developer-reload", G_CALLBACK (reload_requested), NULL);
}

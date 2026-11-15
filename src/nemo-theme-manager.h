/* nemo-theme-manager.h - FileHarbor theme engine. New file, not in upstream.
 *
 * A theme is a small .ini file (see fileharbor/docs/THEMING.md): a palette, three
 * metrics and optional extra CSS. The manager turns it into GTK CSS for the
 * file manager window and applies it live. */

#ifndef NEMO_THEME_MANAGER_H
#define NEMO_THEME_MANAGER_H

#include <glib.h>

G_BEGIN_DECLS

typedef struct {
	char    *id;        /* file name without .ini */
	char    *name;      /* display name */
	char    *path;
	gboolean builtin;   /* shipped with the app: read-only */
} NemoThemeInfo;

/* palette keys, in the order the editor shows them */
extern const char *const nemo_theme_color_keys[];   /* NULL terminated */
extern const char *const nemo_theme_color_labels[];

#define NEMO_THEME_SYSTEM_ID "gtk"      /* the plain GTK theme, no overrides; "system" is the adaptive default theme */

void       nemo_theme_manager_init          (void);
const char*nemo_theme_manager_get_mode      (void);                  /* "system", "dark" or "light" */
void       nemo_theme_manager_set_mode      (const char *mode);
gboolean   nemo_theme_manager_get_desktop_accent (void);          /* use the desktop's accent colour (System theme) */
void       nemo_theme_manager_set_desktop_accent (gboolean use);
const char*nemo_theme_manager_get_roundness (void);                /* "theme", "square", "soft", "rounded" or "round" */
void       nemo_theme_manager_set_roundness (const char *roundness);
const char*nemo_theme_manager_get_spaciousness (void);            /* "compact", "default", "comfortable" or "spacious" */
void       nemo_theme_manager_set_spaciousness (const char *spaciousness);
GList     *nemo_theme_manager_list          (void);   /* of NemoThemeInfo* */
void       nemo_theme_info_free             (NemoThemeInfo *info);
const char*nemo_theme_manager_get_current   (void);
void       nemo_theme_manager_set_current   (const char *id);
GKeyFile  *nemo_theme_manager_load          (const char *id);       /* raw file */
GKeyFile  *nemo_theme_manager_resolve       (GKeyFile *raw);         /* every key filled in */
void       nemo_theme_manager_preview       (GKeyFile *kf);          /* apply without saving */
void       nemo_theme_manager_reapply       (void);                  /* back to the saved theme */
char      *nemo_theme_manager_user_dir      (void);
gboolean   nemo_theme_manager_save_user     (const char *id, GKeyFile *kf, GError **error);
gboolean   nemo_theme_manager_delete_user   (const char *id);
char      *nemo_theme_manager_make_id       (const char *display_name);

G_END_DECLS

#endif

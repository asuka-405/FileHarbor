/* fileharbor-extensions.c - find and describe FileHarbor extensions. New file, not in upstream Nemo. */

#include <config.h>

#include "fileharbor-extensions.h"
#include "nemo-global-preferences.h"

#include <gio/gio.h>
#include <json-glib/json-glib.h>
#include <string.h>

static GList *all = NULL;
static gboolean loaded = FALSE;

char *
fh_extensions_user_dir (void)
{
	return g_build_filename (g_get_user_data_dir (), "fileharbor", "extensions", NULL);
}

/* ---- parsing ------------------------------------------------------ */

static char *
str_member (JsonObject *o, const char *k)
{
	if (o != NULL && json_object_has_member (o, k) && JSON_NODE_HOLDS_VALUE (json_object_get_member (o, k)))
		return g_strdup (json_object_get_string_member (o, k));
	return NULL;
}

static char **
argv_member (JsonObject *o, const char *k, const char *ext_dir)
{
	JsonNode *n;
	GPtrArray *a;
	guint i;

	if (o == NULL || !json_object_has_member (o, k))
		return NULL;
	n = json_object_get_member (o, k);
	a = g_ptr_array_new_with_free_func (g_free);
	if (JSON_NODE_HOLDS_ARRAY (n)) {
		JsonArray *arr = json_node_get_array (n);

		for (i = 0; i < json_array_get_length (arr); i++) {
			const char *s = json_array_get_string_element (arr, i);
			char *v;

			if (s == NULL)
				continue;
			/* "$EXT" expands to the extension's own folder, so scripts can be shipped inside it */
			if (g_str_has_prefix (s, "$EXT/"))
				v = g_build_filename (ext_dir, s + 5, NULL);
			else
				v = g_strdup (s);
			g_ptr_array_add (a, v);
		}
	} else if (JSON_NODE_HOLDS_VALUE (n)) {
		/* a single string: split like a shell would */
		char **parts = NULL;
		int argc = 0;

		if (g_shell_parse_argv (json_node_get_string (n), &argc, &parts, NULL)) {
			for (i = 0; i < (guint) argc; i++) {
				if (g_str_has_prefix (parts[i], "$EXT/"))
					g_ptr_array_add (a, g_build_filename (ext_dir, parts[i] + 5, NULL));
				else
					g_ptr_array_add (a, g_strdup (parts[i]));
			}
			g_strfreev (parts);
		}
	}
	if (a->len == 0) {
		g_ptr_array_free (a, TRUE);
		return NULL;
	}
	g_ptr_array_add (a, NULL);
	return (char **) g_ptr_array_free (a, FALSE);
}

static void
free_extension (FhExtension *e)
{
	GList *l;

	for (l = e->sidebar; l; l = l->next) {
		FhSidebarEntry *s = l->data;
		g_free (s->name); g_free (s->icon); g_free (s->uri); g_free (s);
	}
	for (l = e->pages; l; l = l->next) {
		FhPage *p = l->data;
		g_free (p->id); g_free (p->title); g_free (p->icon); g_strfreev (p->argv); g_free (p);
	}
	for (l = e->tools; l; l = l->next) {
		FhTool *t = l->data;
		g_free (t->name); g_free (t->icon); g_strfreev (t->argv); g_free (t);
	}
	g_list_free (e->sidebar); g_list_free (e->pages); g_list_free (e->tools);
	g_free (e->id); g_free (e->name); g_free (e->version); g_free (e->description);
	g_free (e->dir); g_free (e->error); g_free (e);
}

static gboolean
valid_id (const char *id)
{
	const char *p;

	if (id == NULL || id[0] == '\0' || strlen (id) > 64)
		return FALSE;
	for (p = id; *p; p++)
		if (!(g_ascii_isalnum (*p) || *p == '-' || *p == '_' || *p == '.'))
			return FALSE;
	return TRUE;
}

static FhExtension *
parse_extension (const char *dir, gboolean builtin)
{
	FhExtension *e = g_new0 (FhExtension, 1);
	char *path = g_build_filename (dir, "extension.json", NULL);
	JsonParser *parser = json_parser_new ();
	GError *error = NULL;
	char *base = g_path_get_basename (dir);

	e->dir = g_strdup (dir);
	e->builtin = builtin;
	e->id = g_strdup (base);
	e->name = g_strdup (base);
	g_free (base);

	if (!json_parser_load_from_file (parser, path, &error)) {
		e->error = g_strdup (error->message);
		g_clear_error (&error);
	} else if (!JSON_NODE_HOLDS_OBJECT (json_parser_get_root (parser))) {
		e->error = g_strdup ("extension.json must contain a JSON object");
	} else {
		JsonObject *o = json_node_get_object (json_parser_get_root (parser));
		JsonArray *arr;
		guint i;
		char *s;

		if ((s = str_member (o, "id")) != NULL) {
			if (valid_id (s)) {
				g_free (e->id);
				e->id = s;
			} else {
				e->error = g_strdup_printf ("invalid id “%s” (use letters, digits, - _ .)", s);
				g_free (s);
			}
		}
		if ((s = str_member (o, "name")) != NULL) { g_free (e->name); e->name = s; }
		e->version = str_member (o, "version");
		e->description = str_member (o, "description");

		if (json_object_has_member (o, "sidebar") && (arr = json_object_get_array_member (o, "sidebar")) != NULL) {
			for (i = 0; i < json_array_get_length (arr); i++) {
				JsonObject *so = json_array_get_object_element (arr, i);
				FhSidebarEntry *se;
				char *page = str_member (so, "page");

				if (so == NULL)
					continue;
				se = g_new0 (FhSidebarEntry, 1);
				se->name = str_member (so, "name");
				se->icon = str_member (so, "icon");
				se->uri = page != NULL ? g_strdup_printf (FH_PAGE_URI_PREFIX "%s/%s", e->id, page) : str_member (so, "uri");
				g_free (page);
				if (se->name == NULL || se->uri == NULL) {
					g_free (se->name); g_free (se->icon); g_free (se->uri); g_free (se);
					continue;
				}
				e->sidebar = g_list_append (e->sidebar, se);
			}
		}
		if (json_object_has_member (o, "pages") && (arr = json_object_get_array_member (o, "pages")) != NULL) {
			for (i = 0; i < json_array_get_length (arr); i++) {
				JsonObject *po = json_array_get_object_element (arr, i);
				FhPage *p;

				if (po == NULL)
					continue;
				p = g_new0 (FhPage, 1);
				p->id = str_member (po, "id");
				p->title = str_member (po, "title");
				p->icon = str_member (po, "icon");
				p->argv = argv_member (po, "exec", e->dir);
				p->refresh = json_object_has_member (po, "refresh") ? (int) json_object_get_int_member (po, "refresh") : 0;
				if (!valid_id (p->id) || p->argv == NULL) {
					g_free (p->id); g_free (p->title); g_free (p->icon); g_strfreev (p->argv); g_free (p);
					continue;
				}
				if (p->title == NULL)
					p->title = g_strdup (p->id);
				e->pages = g_list_append (e->pages, p);
			}
		}
		if (json_object_has_member (o, "tools") && (arr = json_object_get_array_member (o, "tools")) != NULL) {
			for (i = 0; i < json_array_get_length (arr); i++) {
				JsonObject *to = json_array_get_object_element (arr, i);
				FhTool *t;

				if (to == NULL)
					continue;
				t = g_new0 (FhTool, 1);
				t->name = str_member (to, "name");
				t->icon = str_member (to, "icon");
				t->argv = argv_member (to, "exec", e->dir);
				if (t->name == NULL || t->argv == NULL) {
					g_free (t->name); g_free (t->icon); g_strfreev (t->argv); g_free (t);
					continue;
				}
				e->tools = g_list_append (e->tools, t);
			}
		}
	}
	g_object_unref (parser);
	g_free (path);
	return e;
}

static gboolean
is_disabled (const char *id)
{
	char **dis = g_settings_get_strv (nemo_preferences, NEMO_PREFERENCES_EXTENSIONS_DISABLED);
	gboolean found = g_strv_contains ((const char * const *) dis, id);

	g_strfreev (dis);
	return found;
}

static void
scan_dir (const char *root, gboolean builtin, GHashTable *seen)
{
	GDir *d = g_dir_open (root, 0, NULL);
	const char *name;

	if (d == NULL)
		return;
	while ((name = g_dir_read_name (d)) != NULL) {
		char *dir = g_build_filename (root, name, NULL);
		char *manifest = g_build_filename (dir, "extension.json", NULL);

		if (name[0] != '.' && g_file_test (manifest, G_FILE_TEST_IS_REGULAR)) {
			FhExtension *e = parse_extension (dir, builtin);

			if (g_hash_table_contains (seen, e->id)) {      /* the user's copy wins over a system one */
				free_extension (e);
			} else {
				g_hash_table_add (seen, g_strdup (e->id));
				e->enabled = e->error == NULL && !is_disabled (e->id);
				all = g_list_append (all, e);
			}
		}
		g_free (manifest);
		g_free (dir);
	}
	g_dir_close (d);
}

void
fh_extensions_reload (void)
{
	GHashTable *seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	const char *const *dirs;
	char *user;
	int i;

	g_list_free_full (all, (GDestroyNotify) free_extension);
	all = NULL;
	user = fh_extensions_user_dir ();
	scan_dir (user, FALSE, seen);
	g_free (user);
	dirs = g_get_system_data_dirs ();
	for (i = 0; dirs[i] != NULL; i++) {
		char *root = g_build_filename (dirs[i], "fileharbor", "extensions", NULL);

		scan_dir (root, TRUE, seen);
		g_free (root);
	}
	g_hash_table_destroy (seen);
	loaded = TRUE;
}

GList *
fh_extensions_get_all (void)
{
	if (!loaded)
		fh_extensions_reload ();
	return all;
}

static GList *
sub_dirs (const char *sub)
{
	GList *out = NULL, *l;

	for (l = fh_extensions_get_all (); l != NULL; l = l->next) {
		FhExtension *e = l->data;
		char *p;

		if (!e->enabled)
			continue;
		p = g_build_filename (e->dir, sub, NULL);
		if (g_file_test (p, G_FILE_TEST_IS_DIR))
			out = g_list_append (out, p);
		else
			g_free (p);
	}
	return out;
}

GList *fh_extensions_get_action_dirs (void) { return sub_dirs ("actions"); }
GList *fh_extensions_get_theme_dirs (void)  { return sub_dirs ("themes"); }

FhPage *
fh_extensions_find_page (const char *uri, FhExtension **ext_out)
{
	const char *rest;
	char **parts;
	GList *l, *p;
	FhPage *found = NULL;

	if (uri == NULL || !g_str_has_prefix (uri, FH_PAGE_URI_PREFIX))
		return NULL;
	rest = uri + strlen (FH_PAGE_URI_PREFIX);
	parts = g_strsplit (rest, "/", 2);
	if (g_strv_length (parts) == 2) {
		for (l = fh_extensions_get_all (); l != NULL && found == NULL; l = l->next) {
			FhExtension *e = l->data;

			if (!e->enabled || g_strcmp0 (e->id, parts[0]) != 0)
				continue;
			for (p = e->pages; p != NULL; p = p->next) {
				if (g_strcmp0 (((FhPage *) p->data)->id, parts[1]) == 0) {
					found = p->data;
					if (ext_out)
						*ext_out = e;
					break;
				}
			}
		}
	}
	g_strfreev (parts);
	return found;
}

void
fh_extensions_set_enabled (const char *id, gboolean enabled)
{
	char **dis = g_settings_get_strv (nemo_preferences, NEMO_PREFERENCES_EXTENSIONS_DISABLED);
	GPtrArray *a = g_ptr_array_new ();
	int i;
	GList *l;

	for (i = 0; dis[i] != NULL; i++)
		if (g_strcmp0 (dis[i], id) != 0)
			g_ptr_array_add (a, dis[i]);
	if (!enabled)
		g_ptr_array_add (a, (gpointer) id);
	g_ptr_array_add (a, NULL);
	g_settings_set_strv (nemo_preferences, NEMO_PREFERENCES_EXTENSIONS_DISABLED, (const char * const *) a->pdata);
	g_ptr_array_free (a, TRUE);
	g_strfreev (dis);
	for (l = fh_extensions_get_all (); l != NULL; l = l->next)
		if (g_strcmp0 (((FhExtension *) l->data)->id, id) == 0)
			((FhExtension *) l->data)->enabled = enabled && ((FhExtension *) l->data)->error == NULL;
}


/* ---- built-in tools ----------------------------------------------------------------- */

static const FhToolInfo tool_table[] = {
	{ "devtools", "Developer tools",         "applications-development-symbolic", "Format JSON, hashes, encoders, regex tester and more" },
	{ "image",    "Images",                  "image-x-generic-symbolic",          "View, edit and batch-resize pictures, including camera RAW" },
	{ "media",    "Media player",            "applications-multimedia-symbolic",  "Play music and video; trim, convert and capture" },
	{ "editor",   "Text editor",             "accessories-text-editor-symbolic",  "Code and text with syntax highlighting; Markdown and MDX preview" },
	{ "hub",      "Tools",                   "applications-utilities-symbolic",   "All tools" },
	/* below the hub: not listed in menus, kept so older links and the right-click items still resolve */
	{ "preview",  "Markdown preview",        "text-x-generic-symbolic",           "Rendered Markdown" },
	{ "resize",   "Resize pictures",         "view-fullscreen-symbolic",          "Resize or convert many pictures at once" },
	{ "video",    "Video player",            "video-x-generic-symbolic",          "Watch, trim, convert and capture video" },
	{ "music",    "Music player",            "audio-x-generic-symbolic",          "Play music" },
	{ "markdown", "Markdown editor",         "text-x-generic-symbolic",           "View, edit, or both side by side" },
};

const FhToolInfo *
fh_tools_all (int *count)
{
	if (count)
		*count = G_N_ELEMENTS (tool_table);
	return tool_table;
}

const FhToolInfo *
fh_tool_lookup (const char *id)
{
	guint i;

	for (i = 0; id != NULL && i < G_N_ELEMENTS (tool_table); i++)
		if (g_strcmp0 (tool_table[i].id, id) == 0)
			return &tool_table[i];
	return NULL;
}

char *
fh_tool_uri (const char *tool, GList *paths)
{
	GString *s = g_string_new (FH_TOOL_URI_PREFIX);
	GList *l;
	gboolean first = TRUE;

	g_string_append (s, tool);
	for (l = paths; l != NULL; l = l->next) {
		char *e = g_uri_escape_string (l->data, NULL, FALSE);

		g_string_append_printf (s, "%sf=%s", first ? "?" : "&", e);
		first = FALSE;
		g_free (e);
	}
	return g_string_free (s, FALSE);
}

gboolean
fh_tool_uri_parse (const char *uri, char **tool, char ***files)
{
	const char *rest, *q;
	GPtrArray *arr;
	char **parts;
	int i;

	if (uri == NULL || !g_str_has_prefix (uri, FH_TOOL_URI_PREFIX))
		return FALSE;
	rest = uri + strlen (FH_TOOL_URI_PREFIX);
	q = strchr (rest, '?');
	if (tool)
		*tool = q ? g_strndup (rest, q - rest) : g_strdup (rest);
	arr = g_ptr_array_new_with_free_func (g_free);
	if (q != NULL) {
		parts = g_strsplit (q + 1, "&", -1);
		for (i = 0; parts[i] != NULL; i++) {
			if (g_str_has_prefix (parts[i], "f=")) {
				char *p = g_uri_unescape_string (parts[i] + 2, NULL);

				if (p != NULL)
					g_ptr_array_add (arr, p);
			}
		}
		g_strfreev (parts);
	}
	g_ptr_array_add (arr, NULL);
	if (files)
		*files = (char **) g_ptr_array_free (arr, FALSE);
	else
		g_ptr_array_free (arr, TRUE);
	return TRUE;
}

char *
fh_virtual_page_title (const char *uri)
{
	if (uri == NULL)
		return NULL;
	if (g_str_has_prefix (uri, "x-fileharbor-storage:"))
		return g_strdup ("Storage");
	if (g_str_has_prefix (uri, FH_PAGE_URI_PREFIX)) {
		FhPage *p = fh_extensions_find_page (uri, NULL);

		return g_strdup (p ? p->title : "Page");
	}
	if (g_str_has_prefix (uri, FH_TOOL_URI_PREFIX)) {
		char *tool = NULL, **files = NULL, *title;
		const FhToolInfo *info;

		fh_tool_uri_parse (uri, &tool, &files);
		info = fh_tool_lookup (tool);
		if (files != NULL && files[0] != NULL) {
			char *base = g_path_get_basename (files[0]);

			title = g_strdup_printf ("%s%s", base, files[1] ? " …" : "");
			g_free (base);
		} else {
			title = g_strdup (info ? info->label : "Tool");
		}
		g_free (tool);
		g_strfreev (files);
		return title;
	}
	return NULL;
}

/* ---- which files the built-in text editor takes ------------------------------------- */

static gboolean
fh_name_has_suffix (const char *name, const char *const *list)
{
	int i;

	for (i = 0; list[i] != NULL; i++)
		if (g_str_has_suffix (name, list[i]))
			return TRUE;
	return FALSE;
}

static const char *const text_exts[] = {
	".txt", ".log", ".md", ".markdown", ".mdx", ".rst", ".adoc", ".org", ".tex", ".csv", ".tsv", ".json", ".jsonc", ".json5", ".yaml", ".yml",
	".toml", ".ini", ".cfg", ".conf", ".env", ".xml", ".html", ".htm", ".xhtml", ".css", ".scss", ".sass", ".less", ".js", ".mjs", ".cjs",
	".jsx", ".ts", ".tsx", ".vue", ".svelte", ".py", ".pyi", ".rb", ".php", ".pl", ".lua", ".r", ".jl", ".sh", ".bash", ".zsh", ".fish",
	".ps1", ".bat", ".c", ".h", ".cc", ".cpp", ".cxx", ".hpp", ".hh", ".cs", ".java", ".kt", ".kts", ".scala", ".swift", ".m", ".mm",
	".go", ".rs", ".zig", ".dart", ".hs", ".ml", ".ex", ".exs", ".erl", ".clj", ".lisp", ".el", ".sql", ".graphql", ".gql", ".proto",
	".tf", ".hcl", ".nix", ".cmake", ".mk", ".gradle", ".diff", ".patch", ".desktop", ".service", ".rules", ".lock", ".sln", ".svg",
	".gitignore", ".gitattributes", ".editorconfig", "makefile", "dockerfile", "license", "readme", "pkgbuild", NULL };
static const char *const text_mimes[] = { "application/json", "application/xml", "application/javascript", "application/x-shellscript",
					  "application/x-yaml", "application/toml", "application/sql", "application/x-php", "application/x-ruby",
					  "application/x-perl", "application/x-desktop", "application/x-subrip", "application/x-awk",
					  "application/x-sh", "application/x-csh", "application/x-latex", "application/x-tex", NULL };

gboolean
fh_is_text_file (const char *mime, const char *lower)
{
	int i;

	if (mime != NULL && g_str_has_prefix (mime, "text/"))
		return TRUE;
	for (i = 0; mime != NULL && text_mimes[i] != NULL; i++)
		if (g_str_has_prefix (mime, text_mimes[i]))
			return TRUE;
	return fh_name_has_suffix (lower, text_exts);
}

static const char *const image_exts[] = { ".jpg", ".jpeg", ".jpe", ".png", ".gif", ".bmp", ".webp", ".tif", ".tiff", ".ico", ".ppm",
					  ".pgm", ".pbm", ".tga", ".dds", ".psd", ".avif", ".heic", ".heif", ".jp2", ".qoi", ".svg", NULL };
static const char *const raw_exts[] = { ".cr2", ".cr3", ".crw", ".nef", ".nrw", ".arw", ".srf", ".sr2", ".dng", ".orf", ".rw2",
					".raf", ".pef", ".srw", ".x3f", ".3fr", ".erf", ".kdc", ".mrw", ".mef", ".rwl", ".iiq", NULL };
static const char *const video_exts[] = { ".mp4", ".mkv", ".webm", ".avi", ".mov", ".m4v", ".wmv", ".flv", ".mpg", ".mpeg", ".ts",
					  ".m2ts", ".3gp", ".ogv", ".vob", ".mts", ".divx", NULL };
static const char *const audio_exts[] = { ".mp3", ".flac", ".ogg", ".oga", ".opus", ".wav", ".m4a", ".aac", ".wma", ".aiff", ".ape",
					  ".wv", ".mka", NULL };
static const char *const markdown_exts[] = { ".md", ".markdown", ".mdown", ".mkd", ".mdx", NULL };

/* The file type is taken from the MIME type, and from the extension when the MIME type is not known yet. */
static gboolean
kind_matches (const char *mime, const char *lower, const char *prefix, const char *const *exts)
{
	gboolean mime_known = mime != NULL && *mime != '\0' && g_strcmp0 (mime, "application/octet-stream") != 0;

	if (mime_known && g_str_has_prefix (mime, prefix))
		return TRUE;
	if (mime_known && g_str_has_prefix (mime, "text/"))      /* a .ts file that is TypeScript, not video */
		return FALSE;
	return fh_name_has_suffix (lower, exts);
}

FhFileKind
fh_file_kind (const char *mime, const char *lower)
{
	if (kind_matches (mime, lower, "image/", image_exts) || fh_name_has_suffix (lower, raw_exts))
		return FH_KIND_IMAGE;
	if (kind_matches (mime, lower, "video/", video_exts))
		return FH_KIND_VIDEO;
	if (kind_matches (mime, lower, "audio/", audio_exts))
		return FH_KIND_AUDIO;
	if (fh_name_has_suffix (lower, markdown_exts))
		return FH_KIND_MARKDOWN;
	if (fh_is_text_file (mime, lower))
		return FH_KIND_TEXT;
	return FH_KIND_OTHER;
}

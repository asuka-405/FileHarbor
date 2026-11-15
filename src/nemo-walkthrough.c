/* nemo-walkthrough.c - first-run tour. New file, not in upstream Nemo. */

#include <config.h>

#include "nemo-walkthrough.h"

#include <glib/gi18n.h>
#include <libnemo-private/nemo-global-preferences.h>

typedef struct {
	const char *icon;
	const char *title;
	const char *body;     /* Pango markup */
} Step;

static const Step steps[] = {
	{ "go-home-symbolic",
	  N_("Welcome to FileHarbor"),
	  N_("FileHarbor is a file manager built on Nemo, with a new look and a few extra tools built in:\n\n"
	     "  •  a <b>terminal</b> that follows the folder you are in\n"
	     "  •  a <b>Storage</b> page that shows what is using your disk\n"
	     "  •  <b>themes</b> you can change live\n"
	     "  •  one-click <b>remote connections</b> (SFTP, FTP, WebDAV, SMB)\n\n"
	     "This short tour takes a minute. You can reopen it any time from <b>Help &gt; Welcome tour</b>.") },
	{ "view-dual-symbolic",
	  N_("Finding your way around"),
	  N_("<b>Sidebar</b> (left): places, bookmarks, drives, network and Storage.\n"
	     "<b>Breadcrumbs</b> (top): click any part of the path to jump back. Press <b>Ctrl+L</b> to type a path instead.\n"
	     "<b>Tabs</b> (under the title bar): <b>Ctrl+T</b> opens a tab, the <b>+</b> button does too, and middle-clicking a folder opens it in a new tab.\n"
	     "<b>Menus</b> live in the title bar, on the left.") },
	{ "utilities-terminal-symbolic",
	  N_("The terminal"),
	  N_("The terminal sits above your files. Press <b>F4</b> to hide or show it.\n\n"
	     "It is linked to the file view: when you <b>cd</b> in the terminal the files follow, and when you open a folder the terminal moves there too. "
	     "Start an <b>ssh</b> session and the files follow you onto the remote machine over SFTP.\n\n"
	     "Handy keys: <b>Ctrl+Shift+C / V</b> copy and paste, <b>Ctrl+Shift+F</b> search, <b>Ctrl +/-</b> zoom, <b>Ctrl+click</b> opens links.\n"
	     "The first time the shell moves your files, you will be asked whether to keep the linking on.") },
	{ "drive-multidisk-symbolic",
	  N_("Storage: where did my disk go?"),
	  N_("Choose <b>Storage</b> in the sidebar. A background service keeps an index of your home folder and updates it when free space changes by 200 MB or more.\n\n"
	     "<b>Overview</b> shows usage by type, <b>Map</b> is a picture of every folder sized by what it uses, <b>Folders</b> and <b>Large files</b> let you dig in, "
	     "<b>Clean up</b> lists caches and build folders that are safe to remove, and <b>Growth</b> shows what changed recently.\n\n"
	     "You can delete straight from these lists: click the trash icon, press <b>Delete</b>, or right-click.") },
	{ "network-server-symbolic",
	  N_("Connecting to other machines"),
	  N_("Open <b>Network &gt; Connect to a server…</b> in the sidebar. Pick SFTP, FTP, WebDAV, SMB or NFS, fill in the server, and connect. "
	     "Passwords are asked for by the system.\n\n"
	     "Use <b>Save to sidebar</b> to keep a server one click away.") },
	{ "applications-development-symbolic",
	  N_("Built-in tools"),
	  N_("Open the <b>Tools</b> menu, or right-click a file:\n\n"
	     "  •  <b>Image viewer and editor</b>: JPEG, PNG, WebP, camera RAW and more. Rotate, crop, resize and adjust.\n"
	     "  •  <b>Resize pictures</b>: many at once, to a size or a preset.\n"
	     "  •  <b>Video player and editor</b>: trim, convert, extract audio, make a GIF.\n"
	     "  •  <b>Music player</b>, <b>Markdown editor</b> (view, edit, or both), and\n"
	     "  •  <b>Developer tools</b>: JSON, Base64, hashes, UUIDs, regex, JWT, timestamps and more.") },
	{ "application-x-addon-symbolic",
	  N_("Extensions"),
	  N_("Extensions add menu items, sidebar entries, whole pages, tools and themes.\n\n"
	     "Open <b>Tools &gt; Extensions…</b> to turn them on or off. A sample, <b>System Glance</b>, is included: look at its sidebar entry, "
	     "then at its folder to see how little it takes. The same window links to the guide for making your own.") },
	{ "preferences-desktop-appearance-symbolic",
	  N_("Make it yours"),
	  N_("Open <b>Edit &gt; Appearance</b>. Pick a theme, or change any colour, gap or corner radius and watch it update. "
	     "Save your changes as your own theme.\n\n"
	     "The same window switches the folder navigation (a classic sidebar or a column navigator), turns the terminal linking on or off, "
	     "and has a guide to <b>building your own theme</b>.") },
};

typedef struct {
	GtkWidget *window, *stack, *back, *next, *dots, *skip;
	int page;
} Tour;

static Tour *tour = NULL;

static void
update_ui (void)
{
	GString *dots = g_string_new (NULL);
	guint i;

	gtk_stack_set_visible_child_name (GTK_STACK (tour->stack), g_strdup_printf ("p%d", tour->page));
	for (i = 0; i < G_N_ELEMENTS (steps); i++)
		g_string_append (dots, (int) i == tour->page ? "●  " : "○  ");
	gtk_label_set_text (GTK_LABEL (tour->dots), dots->str);
	g_string_free (dots, TRUE);
	gtk_widget_set_sensitive (tour->back, tour->page > 0);
	gtk_button_set_label (GTK_BUTTON (tour->next),
			      tour->page == (int) G_N_ELEMENTS (steps) - 1 ? _("Get started") : _("Next"));
	gtk_widget_set_visible (tour->skip, tour->page < (int) G_N_ELEMENTS (steps) - 1);
}

static void
finish (void)
{
	g_settings_set_boolean (nemo_preferences, NEMO_PREFERENCES_WALKTHROUGH_DONE, TRUE);
	gtk_widget_destroy (tour->window);
}

static void
on_next (GtkButton *b, gpointer data)
{
	if (tour->page == (int) G_N_ELEMENTS (steps) - 1) {
		finish ();
		return;
	}
	tour->page++;
	update_ui ();
}

static void on_back (GtkButton *b, gpointer data) { if (tour->page > 0) { tour->page--; update_ui (); } }
static void on_skip (GtkButton *b, gpointer data) { finish (); }

static gboolean
on_key (GtkWidget *w, GdkEventKey *ev, gpointer data)
{
	if (ev->keyval == GDK_KEY_Escape) {
		finish ();
		return TRUE;
	}
	if (ev->keyval == GDK_KEY_Right || ev->keyval == GDK_KEY_Return) {
		on_next (NULL, NULL);
		return TRUE;
	}
	if (ev->keyval == GDK_KEY_Left) {
		on_back (NULL, NULL);
		return TRUE;
	}
	return FALSE;
}

static void
on_destroy (GtkWidget *w, gpointer data)
{
	/* closing the window by any route counts as seen */
	g_settings_set_boolean (nemo_preferences, NEMO_PREFERENCES_WALKTHROUGH_DONE, TRUE);
	g_free (tour);
	tour = NULL;
}

void
nemo_walkthrough_present (GtkWindow *parent)
{
	GtkWidget *box, *bar, *btns;
	guint i;

	if (tour != NULL) {
		gtk_window_present (GTK_WINDOW (tour->window));
		return;
	}
	tour = g_new0 (Tour, 1);
	tour->window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title (GTK_WINDOW (tour->window), _("Welcome to FileHarbor"));
	gtk_window_set_default_size (GTK_WINDOW (tour->window), 560, 440);
	gtk_window_set_position (GTK_WINDOW (tour->window), GTK_WIN_POS_CENTER_ON_PARENT);
	if (parent != NULL) {
		gtk_window_set_transient_for (GTK_WINDOW (tour->window), parent);
		gtk_window_set_modal (GTK_WINDOW (tour->window), TRUE);
	}
	g_signal_connect (tour->window, "destroy", G_CALLBACK (on_destroy), NULL);
	g_signal_connect (tour->window, "key-press-event", G_CALLBACK (on_key), NULL);

	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
	gtk_container_set_border_width (GTK_CONTAINER (box), 24);
	gtk_container_add (GTK_CONTAINER (tour->window), box);

	tour->stack = gtk_stack_new ();
	gtk_stack_set_transition_type (GTK_STACK (tour->stack), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
	gtk_widget_set_vexpand (tour->stack, TRUE);
	for (i = 0; i < G_N_ELEMENTS (steps); i++) {
		GtkWidget *page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 14);
		GtkWidget *icon = gtk_image_new_from_icon_name (steps[i].icon, GTK_ICON_SIZE_DIALOG);
		GtkWidget *title = gtk_label_new (NULL);
		GtkWidget *body = gtk_label_new (NULL);
		char *t = g_markup_printf_escaped ("<span size='x-large' weight='bold'>%s</span>", _(steps[i].title));
		char *name = g_strdup_printf ("p%u", i);

		gtk_image_set_pixel_size (GTK_IMAGE (icon), 64);
		gtk_widget_set_halign (icon, GTK_ALIGN_START);
		gtk_label_set_markup (GTK_LABEL (title), t);
		gtk_widget_set_halign (title, GTK_ALIGN_START);
		gtk_label_set_markup (GTK_LABEL (body), _(steps[i].body));
		gtk_label_set_line_wrap (GTK_LABEL (body), TRUE);
		gtk_label_set_xalign (GTK_LABEL (body), 0.0);
		gtk_label_set_yalign (GTK_LABEL (body), 0.0);
		gtk_widget_set_valign (body, GTK_ALIGN_START);
		gtk_box_pack_start (GTK_BOX (page), icon, FALSE, FALSE, 0);
		gtk_box_pack_start (GTK_BOX (page), title, FALSE, FALSE, 0);
		gtk_box_pack_start (GTK_BOX (page), body, TRUE, TRUE, 0);
		gtk_stack_add_named (GTK_STACK (tour->stack), page, name);
		g_free (t); g_free (name);
	}
	gtk_box_pack_start (GTK_BOX (box), tour->stack, TRUE, TRUE, 0);

	bar = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
	tour->dots = gtk_label_new ("");
	gtk_style_context_add_class (gtk_widget_get_style_context (tour->dots), "dim-label");
	tour->skip = gtk_button_new_with_mnemonic (_("_Skip tour"));
	tour->back = gtk_button_new_with_mnemonic (_("_Back"));
	tour->next = gtk_button_new_with_label (_("Next"));
	gtk_style_context_add_class (gtk_widget_get_style_context (tour->next), "suggested-action");
	g_signal_connect (tour->next, "clicked", G_CALLBACK (on_next), NULL);
	g_signal_connect (tour->back, "clicked", G_CALLBACK (on_back), NULL);
	g_signal_connect (tour->skip, "clicked", G_CALLBACK (on_skip), NULL);
	btns = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_box_pack_start (GTK_BOX (btns), tour->back, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (btns), tour->next, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (bar), tour->skip, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (bar), tour->dots, TRUE, TRUE, 0);
	gtk_box_pack_end (GTK_BOX (bar), btns, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box), bar, FALSE, FALSE, 0);

	gtk_widget_show_all (tour->window);
	update_ui ();
}

void
nemo_walkthrough_maybe_present (GtkWindow *parent)
{
	if (!g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_WALKTHROUGH_DONE))
		nemo_walkthrough_present (parent);
}

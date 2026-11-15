/* nemo-tab-strip.c - window-wide tab bar. New file, not in upstream Nemo. */

#include <config.h>

#include "nemo-tab-strip.h"

#include "nemo-window.h"
#include "nemo-window-slot.h"
#include "nemo-notebook.h"

#include <glib/gi18n.h>
#include <libnemo-private/nemo-global-preferences.h>

struct _NemoTabStrip {
	GtkBox parent_instance;

	NemoWindow *window;
	GtkNotebook *notebook;      /* the pane notebook we mirror (not owned) */
	GtkWidget *scroller;
	GtkWidget *tabs;            /* box holding one widget per tab */
	GtkWidget *plus;
	gulong added_id, removed_id, reordered_id, switched_id;
};

G_DEFINE_TYPE (NemoTabStrip, nemo_tab_strip, GTK_TYPE_BOX)

static void
disconnect_notebook (NemoTabStrip *self)
{
	if (self->notebook == NULL)
		return;
	g_signal_handler_disconnect (self->notebook, self->added_id);
	g_signal_handler_disconnect (self->notebook, self->removed_id);
	g_signal_handler_disconnect (self->notebook, self->reordered_id);
	g_signal_handler_disconnect (self->notebook, self->switched_id);
	g_object_set_data (G_OBJECT (self->notebook), "fileharbor-tab-strip", NULL);
	g_object_remove_weak_pointer (G_OBJECT (self->notebook), (gpointer *) &self->notebook);
	self->notebook = NULL;
}

static gboolean
tab_button_press (GtkWidget *w, GdkEventButton *ev, gpointer data)
{
	NemoTabStrip *self = data;
	GtkWidget *page = g_object_get_data (G_OBJECT (w), "page");
	int n;

	if (self->notebook == NULL || page == NULL)
		return FALSE;
	n = gtk_notebook_page_num (self->notebook, page);
	if (n < 0)
		return FALSE;
	if (ev->button == 1) {
		gtk_notebook_set_current_page (self->notebook, n);
		return TRUE;
	}
	if (ev->button == 2) {       /* middle click closes, like a browser */
		g_signal_emit_by_name (self->notebook, "tab-close-request", page);
		return TRUE;
	}
	return FALSE;
}

static void
close_clicked (GtkButton *b, gpointer data)
{
	NemoTabStrip *self = data;
	GtkWidget *page = g_object_get_data (G_OBJECT (b), "page");

	if (self->notebook != NULL && page != NULL)
		g_signal_emit_by_name (self->notebook, "tab-close-request", page);
}

static void
plus_clicked (GtkButton *b, gpointer data)
{
	nemo_window_new_tab (NEMO_TAB_STRIP (data)->window);
}

void
nemo_tab_strip_refresh (NemoTabStrip *self)
{
	GList *kids, *l;
	int i, n, current;
	gboolean show;

	g_return_if_fail (NEMO_IS_TAB_STRIP (self));

	kids = gtk_container_get_children (GTK_CONTAINER (self->tabs));
	for (l = kids; l != NULL; l = l->next)
		gtk_widget_destroy (l->data);
	g_list_free (kids);

	n = self->notebook != NULL ? gtk_notebook_get_n_pages (self->notebook) : 0;
	current = self->notebook != NULL ? gtk_notebook_get_current_page (self->notebook) : -1;

	for (i = 0; i < n; i++) {
		GtkWidget *page = gtk_notebook_get_nth_page (self->notebook, i);
		NemoWindowSlot *slot = NEMO_WINDOW_SLOT (page);
		GtkWidget *eb = gtk_event_box_new ();
		GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
		GtkWidget *label = gtk_label_new (slot->title != NULL && slot->title[0] != '\0' ? slot->title : _("Loading…"));
		GtkWidget *close = gtk_button_new_from_icon_name ("window-close-symbolic", GTK_ICON_SIZE_MENU);
		GtkStyleContext *ctx = gtk_widget_get_style_context (eb);
		char *tip = NULL;

		gtk_event_box_set_visible_window (GTK_EVENT_BOX (eb), TRUE);
		gtk_style_context_add_class (ctx, "fileharbor-tab");
		if (i == current)
			gtk_style_context_add_class (ctx, "active");
		gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
		gtk_label_set_max_width_chars (GTK_LABEL (label), 24);
		gtk_widget_set_margin_start (box, 10);
		gtk_widget_set_margin_end (box, 4);
		gtk_widget_set_margin_top (box, 4);
		gtk_widget_set_margin_bottom (box, 4);
		gtk_button_set_relief (GTK_BUTTON (close), GTK_RELIEF_NONE);
		gtk_widget_set_focus_on_click (close, FALSE);
		gtk_widget_set_tooltip_text (close, _("Close tab"));
		g_object_set_data (G_OBJECT (close), "page", page);
		g_signal_connect (close, "clicked", G_CALLBACK (close_clicked), self);
		gtk_box_pack_start (GTK_BOX (box), label, TRUE, TRUE, 0);
		gtk_box_pack_end (GTK_BOX (box), close, FALSE, FALSE, 0);
		gtk_container_add (GTK_CONTAINER (eb), box);
		g_object_set_data (G_OBJECT (eb), "page", page);
		g_signal_connect (eb, "button-press-event", G_CALLBACK (tab_button_press), self);
		if (slot->location != NULL)
			tip = g_file_get_parse_name (slot->location);
		gtk_widget_set_tooltip_text (eb, tip);
		g_free (tip);
		gtk_box_pack_start (GTK_BOX (self->tabs), eb, FALSE, FALSE, 0);
		gtk_widget_show_all (eb);
	}

	show = n > 1 || g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_ALWAYS_SHOW_TAB_BAR);
	gtk_widget_set_visible (GTK_WIDGET (self), show);
}

static void
notebook_changed (GtkNotebook *nb, GtkWidget *page, guint num, gpointer data)
{
	nemo_tab_strip_refresh (data);
}

void
nemo_tab_strip_attach (NemoTabStrip *self, GtkNotebook *notebook)
{
	g_return_if_fail (NEMO_IS_TAB_STRIP (self));

	if (self->notebook == notebook)
		return;
	disconnect_notebook (self);
	if (notebook != NULL) {
		self->notebook = notebook;
		g_object_add_weak_pointer (G_OBJECT (notebook), (gpointer *) &self->notebook);
		self->added_id = g_signal_connect_after (notebook, "page-added", G_CALLBACK (notebook_changed), self);
		self->removed_id = g_signal_connect_after (notebook, "page-removed", G_CALLBACK (notebook_changed), self);
		self->reordered_id = g_signal_connect_after (notebook, "page-reordered", G_CALLBACK (notebook_changed), self);
		self->switched_id = g_signal_connect_after (notebook, "switch-page", G_CALLBACK (notebook_changed), self);
		g_object_set_data (G_OBJECT (notebook), "fileharbor-tab-strip", self);
	}
	nemo_tab_strip_refresh (self);
}

static void
nemo_tab_strip_dispose (GObject *object)
{
	disconnect_notebook (NEMO_TAB_STRIP (object));
	G_OBJECT_CLASS (nemo_tab_strip_parent_class)->dispose (object);
}

static void
nemo_tab_strip_class_init (NemoTabStripClass *klass)
{
	G_OBJECT_CLASS (klass)->dispose = nemo_tab_strip_dispose;
}

static gboolean
scroll_tabs (GtkWidget *w, GdkEventScroll *ev, gpointer data)
{
	GtkAdjustment *adj = gtk_scrolled_window_get_hadjustment (GTK_SCROLLED_WINDOW (w));
	double step = gtk_adjustment_get_page_size (adj) / 4;

	if (ev->direction == GDK_SCROLL_UP || ev->direction == GDK_SCROLL_LEFT)
		gtk_adjustment_set_value (adj, gtk_adjustment_get_value (adj) - step);
	else if (ev->direction == GDK_SCROLL_DOWN || ev->direction == GDK_SCROLL_RIGHT)
		gtk_adjustment_set_value (adj, gtk_adjustment_get_value (adj) + step);
	return TRUE;
}

static void
nemo_tab_strip_init (NemoTabStrip *self)
{
	gtk_orientable_set_orientation (GTK_ORIENTABLE (self), GTK_ORIENTATION_HORIZONTAL);
	gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (self)), "fileharbor-tabstrip");
	gtk_widget_set_no_show_all (GTK_WIDGET (self), TRUE);

	self->scroller = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (self->scroller), GTK_POLICY_EXTERNAL, GTK_POLICY_NEVER);
	gtk_scrolled_window_set_overlay_scrolling (GTK_SCROLLED_WINDOW (self->scroller), TRUE);
	g_signal_connect (self->scroller, "scroll-event", G_CALLBACK (scroll_tabs), NULL);
	gtk_widget_set_hexpand (self->scroller, TRUE);
	self->tabs = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_container_add (GTK_CONTAINER (self->scroller), self->tabs);

	self->plus = gtk_button_new_from_icon_name ("list-add-symbolic", GTK_ICON_SIZE_MENU);
	gtk_button_set_relief (GTK_BUTTON (self->plus), GTK_RELIEF_NONE);
	gtk_widget_set_tooltip_text (self->plus, _("New tab (Ctrl+T)"));
	gtk_widget_set_focus_on_click (self->plus, FALSE);
	gtk_widget_set_margin_start (self->plus, 4);
	gtk_widget_set_margin_end (self->plus, 4);
	g_signal_connect (self->plus, "clicked", G_CALLBACK (plus_clicked), self);

	gtk_box_pack_start (GTK_BOX (self), self->scroller, TRUE, TRUE, 0);
	gtk_box_pack_end (GTK_BOX (self), self->plus, FALSE, FALSE, 0);
	gtk_widget_show_all (self->scroller);
	gtk_widget_show (self->plus);
}

GtkWidget *
nemo_tab_strip_new (NemoWindow *window)
{
	NemoTabStrip *self = g_object_new (NEMO_TYPE_TAB_STRIP, NULL);

	self->window = window;
	return GTK_WIDGET (self);
}

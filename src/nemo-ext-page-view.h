/* nemo-ext-page-view.h - pages provided by FileHarbor extensions. New file, not in upstream Nemo.
 *
 * An extension page is a command that prints JSON describing what to show (headings, text, lists,
 * tables, buttons, progress bars, images). The view runs it, draws the result, and can re-run it on
 * a timer. See fileharbor/docs/EXTENSIONS.md. */

#ifndef NEMO_EXT_PAGE_VIEW_H
#define NEMO_EXT_PAGE_VIEW_H

#include "nemo-view.h"

#define NEMO_TYPE_EXT_PAGE_VIEW nemo_ext_page_view_get_type()
#define NEMO_EXT_PAGE_VIEW(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), NEMO_TYPE_EXT_PAGE_VIEW, NemoExtPageView))
#define NEMO_IS_EXT_PAGE_VIEW(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NEMO_TYPE_EXT_PAGE_VIEW))
#define NEMO_EXT_PAGE_VIEW_ID "OAFIID:Nemo_File_Manager_Extension_Page_View"

typedef struct NemoExtPageViewDetails NemoExtPageViewDetails;

typedef struct {
	NemoView parent_instance;
	NemoExtPageViewDetails *details;
} NemoExtPageView;

typedef struct {
	NemoViewClass parent_class;
} NemoExtPageViewClass;

GType    nemo_ext_page_view_get_type (void);
void     nemo_ext_page_view_register (void);
gboolean nemo_uri_is_ext_page        (const char *uri);

#endif

/* nemo-storage-view.h - "Storage Sense" page for the FileHarbor fork of Nemo.
 *
 * New file, not present in upstream Nemo. Renders the disk usage index kept
 * by nemo-storage-daemon (org.nemo.StorageSense on the session bus) as a
 * page inside the normal Nemo window, reachable at x-nemo-storage:///.
 */

#ifndef NEMO_STORAGE_VIEW_H
#define NEMO_STORAGE_VIEW_H

#include "nemo-view.h"

#define NEMO_TYPE_STORAGE_VIEW nemo_storage_view_get_type()
#define NEMO_STORAGE_VIEW(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), NEMO_TYPE_STORAGE_VIEW, NemoStorageView))
#define NEMO_IS_STORAGE_VIEW(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NEMO_TYPE_STORAGE_VIEW))

#define NEMO_STORAGE_VIEW_ID "OAFIID:Nemo_File_Manager_Storage_View"
#define NEMO_STORAGE_URI "x-nemo-storage:///"

typedef struct NemoStorageViewDetails NemoStorageViewDetails;

typedef struct {
	NemoView parent_instance;
	NemoStorageViewDetails *details;
} NemoStorageView;

typedef struct {
	NemoViewClass parent_class;
} NemoStorageViewClass;

GType    nemo_storage_view_get_type (void);
void     nemo_storage_view_register (void);
gboolean nemo_storage_uri_is_storage (const char *uri);
gboolean nemo_uri_is_virtual_page   (const char *uri);   /* storage page or an extension page */

#endif /* NEMO_STORAGE_VIEW_H */

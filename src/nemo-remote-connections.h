/* nemo-remote-connections.h - saved remote locations and a simple dialog to
 * make new ones (SFTP, FTP, WebDAV, SMB, NFS, ...). New file, not in upstream. */

#ifndef NEMO_REMOTE_CONNECTIONS_H
#define NEMO_REMOTE_CONNECTIONS_H

#include <gtk/gtk.h>

#define NEMO_ADD_CONNECTION_URI "x-fileharbor-add-connection:///"

typedef struct {
	char *name;
	char *uri;
} NemoRemoteConnection;

GList *nemo_remote_connections_load     (void);   /* of NemoRemoteConnection* */
void   nemo_remote_connections_free     (GList *list);
char  *nemo_remote_connections_get_path (void);   /* the file the list is stored in */
void   nemo_remote_connections_present  (GtkWindow *parent);   /* parent is a NemoWindow */

#endif

#ifndef _MODULES_CLIENT_H_
#define _MODULES_CLIENT_H_

#if defined(__MOCC__)
#warning "mocc is not allowed for parsing the client API code!"
#undef __MOCC__
#endif // defined(__MOCC__)

#ifdef __cplusplus
extern "C" {
#endif 

#include "mola/runtime.h"

#include "mola/c/mola_client.h"

extern Connection ConnectToCluster(ClusterAddr);
extern void DisconnectFromCluster(Connection);

extern int SendMsgToCluster(Connection, const char*, unsigned, void*);
extern int AsyncSendMsgToCluster(Connection, const char*, unsigned, void*,
                                 RecvCallback, void *);

extern int RecvMsgFromCluster(Connection, const char*, void**);
extern int RecvMsgWithMoFromCluster(Connection, void **, char **);

typedef int (*EventCallback) (Connection, void *);

extern void Subscribe(Connection, const char *, EventCallback, void *);

#define CONNECT_SUCCESS(conn) \
    ((conn).conn_ctx != NULL)

#ifdef __cplusplus
}
#endif

#endif // _MODULES_CLIENT_H_

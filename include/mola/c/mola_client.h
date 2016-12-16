#ifndef _MOLA_CLIENT_H_
#define _MOLA_CLIENT_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  void *conn_ctx;
} Connection;

typedef const char* ClusterAddr;

typedef void (*RecvCallback)(Connection, void *, void *);

extern Connection ConnectToCluster(ClusterAddr addr);
extern void DisconnectFromCluster(Connection conn);
extern int SendMsgToCluster(Connection conn, const char* module,
                            unsigned intf, void * message);
extern int AsyncSendMsgToCluster(Connection conn, const char* module,
                                 unsigned intf, void * message,
                                 RecvCallback callback, void * ctx);
extern int RecvMsgFromCluster(Connection conn, const char* module,
                              void ** message);
extern int RecvMsgWithMoFromCluster(Connection conn, void ** message,
                                    char ** module);

#ifdef __cplusplus
}
#endif

#endif // _MOLA_CLIENT_H_

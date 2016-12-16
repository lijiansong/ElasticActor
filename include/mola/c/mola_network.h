#ifndef _MOLA_C_NETWORK_H_
#define _MOLA_C_NETWORK_H_

#ifdef __cplusplus
extern "C" {
#endif

/// The mola TCP API support asynchronized send/receive operations
/// from the TCP socket. The proto must be a HEAD-BODY sequence, 
/// the HEAD 's size can be determined before transfer, and the size
/// of the BODY must be determined by the content of the HEAD.
typedef struct mola_proto {
  /* get the header package's size */
  size_t (*header_size)(void *ctx);
  /* get the body package's size, by the header's content */
  size_t (*body_size)(void *ctx, const char* header, size_t hdsize);
  /* make the buffer for sending */
  size_t (*make_buffer)(void *ctx, size_t bsz, const char* bbuf, char **sbuf);
  /* release the buffer for sending */
  void (*release_buffer)(void *ctx, char *buffer);
  /* user defined data */
  void*  ctx;
  /* bind the client to an instance */
  unsigned bind;
}* mola_proto_t;

/// The default proto for TCP connections.
extern struct mola_proto _mola_default_proto;

#ifdef __cplusplus
}
#endif

#endif // _MOLA_C_NETWORK_H_

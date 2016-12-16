#ifndef _MOLA_CRT_MODULE_H_
#define _MOLA_CRT_MODULE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdint.h>

/* define the type of module */
enum {
  MOLA_MODULE_TYPE_NORMAL = 0,
  MOLA_MODULE_TYPE_SINGLETON = 1, // single instance
  MOLA_MODULE_TYPE_AUTOSTART = 2, // auto start an instance after loading
  MOLA_MODULE_TYPE_STATELESS = 4, // stateless module
  MOLA_MODULE_TYPE_STRONG = 8,    // should store/resume state on stack
  MOLA_MODULE_TYPE_PRIVATE = 16,  // accessed by local actors only
  // TODO : 4, 8, 16, 32 ...
};

typedef uint32_t mola_module_type_t;

/* define the types of module init/fini callback */
typedef void (*mola_init_cb_t)(void); // TODO : add a void * typed param
typedef void (*mola_fini_cb_t)(void);

/* define the type of interface */
enum {
  MOLA_INTF_TYPE_INVALID = -1,
  MOLA_INTF_TYPE_NORMAL = 0,
  MOLA_INTF_TYPE_SYNC = 1, /* interface with synchronized operation(s) */
  // TODO : 8, 16, 32, 64 ...
};

typedef uint32_t mola_intf_type_t;

// general interface callback type
typedef void* (*mola_intf_cb_t)(void*, void*);

// general gather callback type
typedef void* (*mola_gather_cb_t)(size_t, void*[]);

// interface table items' type
typedef struct {
  mola_intf_type_t type;
  mola_intf_cb_t callback;
} mola_intf_t;


/* define the runtime module specific type */
typedef struct mola_module_spec {
  const char *name; // the module name
  mola_module_type_t type; // the module type
  size_t size; // the data size per-instance
  uint32_t max_instances; // the max running instances
  mola_init_cb_t init; // the init callback
  mola_fini_cb_t fini; // the fini callback
  size_t intf_num; // the total interface number
  mola_intf_t *intf_tbl; // the table of interfaces
  void * userdata; // an optional user defined data
} *mola_module_spec_t;

/* the module handler, just for libdl using */
typedef void* mola_module_handle_t;

#ifdef __cplusplus
}
#endif

#endif // _MOLA_CRT_MODULE_H_

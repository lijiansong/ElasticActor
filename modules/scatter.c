#include <stdio.h>

#include "framework.h"
#include "msg.h"

struct mola_module_spec __module_spec;

static void* scatter_intf(void * message, void * dummy) {
  MessageA *req = ConvertRawToType(message, MessageA);
  MessageA *res = AllocMsg(MessageA, NULL);

  printf("[%s] receive %d \n", __module_spec.name, req->a);

  res->a = req->a * 2;

  FreeMsg(req);
  return GEN_MESSAGE(res);
}

mola_intf_t scatter_intf_tbl[] = {
 {0, scatter_intf},
};

struct mola_module_spec __module_spec = {
  .name = "ScatterTest",
  .max_instances = 16,
  .init = NULL,
  .fini = NULL,
  .intf_num = 1,
  .intf_tbl = scatter_intf_tbl,
};

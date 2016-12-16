#include <assert.h>
#include <stdio.h>

#include "framework.h"
#include "msg.h"

#define MY_TYPE (MOLA_MODULE_TYPE_SINGLETON|\
                 MOLA_MODULE_TYPE_STRONG)

MODULE_ID_T Module_C, Module_A;

static void* module_B_intf(void * message, void *dummy) {
  MessageA *res = NULL,
           *req = ConvertRawToType(message, MessageA);
  
  req->a += 1;

  printf("Module_B : sending %d to Module_C..\n", req->a);

  /* sync send & recv */
  SendRecvMsgAndConvertType(Module_C, 0, ANY_INST, req, &res, MessageA);

  assert(res != NULL);
  
  printf("Module_B : receive %d from Module_C..\n", res->a);
  printf("Module_B : sending %d back to Module_A..\n", res->a);

  SendMsgToIntf(Module_A, 0, ANY_INST, res, ANY_INTF, NULL);

  return NULL;
}

mola_intf_t module_B_intf_tbl[] = {
  {RT_INTF_SYNC, module_B_intf},
};


static void module_B_init() {
  Module_A = GetModuleID("Module_A");
  Module_C = GetModuleID("Module_C");
}

struct mola_module_spec __module_spec = {
  .name = "Module_B",
  .type = MY_TYPE,
  .size = 0,
  .max_instances = 1,
  .init = module_B_init,
  .fini = NULL,
  .intf_num = 1,
  .intf_tbl = module_B_intf_tbl,
  .userdata = NULL,
};

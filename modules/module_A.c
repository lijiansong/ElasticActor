#include <stdio.h>

#include "framework.h"
#include "msg.h"

#define MY_TYPE (MOLA_MODULE_TYPE_SINGLETON|\
                 MOLA_MODULE_TYPE_AUTOSTART)

extern void InstanceExit(int);

static MODULE_ID_T Module_B;

static void module_A_A2B(int n) {

  MessageA *first = AllocMsg(MessageA, NULL);
  int ret;
  
  first->a = n;

  if (first->a > 100000) {
    FreeMsg(first);
    InstanceExit(first->a);
  }

  printf("Module_A : sending %d to Module_B..\n", first->a);
  if ((ret = SendMsgToIntf(Module_B, 0, ANY_INST, first, ANY_INTF, NULL)) != 0) {
    printf("Module A : sending error %d!!\n", ret);
    InstanceExit(ret);
  }
}

static void * module_A_B2A(void * message, void* dummy) {
  MessageA *req = ConvertRawToType(message, MessageA);

  printf("Module_A : receive %d from Module_B..\n", req->a);
  module_A_A2B(req->a + 1);

  FreeMsg(req);
  
  return NULL;
}

static void module_A_init() {
  Module_B = GetModuleID("Module_B"); 

  module_A_A2B(0);
}

mola_intf_t module_A_intf_tbl[] = {
  {0, module_A_B2A},
};

struct mola_module_spec __module_spec = {
  .name = "Module_A",
  .type = MY_TYPE,
  .size = 0,
  .max_instances = 1,
  .init = module_A_init,
  .fini = NULL,
  .intf_num = 1,
  .intf_tbl = module_A_intf_tbl,
  .userdata = NULL,
};

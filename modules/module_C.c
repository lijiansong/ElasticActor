#include <stdio.h>

#include "framework.h"
#include "msg.h"

#define MY_TYPE MOLA_MODULE_TYPE_SINGLETON

extern __thread void *__rt_gen_data;

typedef struct module_C_data {
  int n;
} * module_C_data_t;

static void* module_C_intf(void * message, void * dummy) {
  MessageA *req = ConvertRawToType(message, MessageA);
  MessageA *res = AllocMsg(MessageA, NULL);

  res->a = ((module_C_data_t)(__rt_gen_data))->n + req->a;

  printf("Module_C : sending back %d to Module_B\n", res->a);

  FreeMsg(req);
  return GEN_MESSAGE(res);
}


static void module_C_init() {
  module_C_data_t user_data = (module_C_data_t)__rt_gen_data;
  user_data->n = 0xABCD;
}

mola_intf_t module_C_intf_tbl[] = {
  {0, module_C_intf},
};

struct mola_module_spec __module_spec = {
  .name = "Module_C",
  .type = MY_TYPE,
  .size = sizeof(struct module_C_data),
  .max_instances = 1,
  .init = module_C_init,
  .fini = NULL,
  .intf_num = 1,
  .intf_tbl = module_C_intf_tbl,
  .userdata = NULL,
};

#include <stdio.h>

#include "framework.h"
#include "msg.h"

#define SCATTER_SIZE 16

static MODULE_ID_T ScatterTest;

static void* scatter_tasks_intf(void *message, void *dummy) {
  MessageA *req = ConvertRawToType(message, MessageA);
  MessageA *res[SCATTER_SIZE];
  
  printf("receive %d from the client\n", req->a);

  int i, size = req->a;
  for (i = 0; i < size; ++i) {
    res[i] = AllocMsg(MessageA, NULL);
    res[i]->a = i;
  }

  printf("scatter %d messages to \"ScatterTest\"..\n", size);

  int ret = ScatterToIntf(ScatterTest, /*intf=*/0, size,
                          /*dests=*/NULL, /*mg=*/(void**)res,
                          /*gather_intf=*/1, NULL);

  printf("scatter returns %d\n", ret);
  return NULL; // return later by gather ..
}


static void* gather_all(size_t size, void *mg[]) {
  printf("all scatter messages are back\n");

  MessageA *res = AllocMsg(MessageA, NULL);
  res->a = 0;
  
  int i;
  for (i = 0; i < size; ++i) {
    MessageA *m = ConvertRawToType(mg[i], MessageA);
    res->a += m->a;
  }

  printf("return %d back to client\n", res->a);
  return GEN_MESSAGE(res);
}

static void* gather_one_intf(void *message, void *ctx) {
  MessageA *res = ConvertRawToType(message, MessageA);
  printf("receive a gather message %d\n", res->a);

  if (ctx != NULL) {
    SwitchToGatherCtx(ctx, gather_all);
  }

  return NULL;
}

mola_intf_t gather_intf_tbl[] = {
  { 0, scatter_tasks_intf }, 
  { 0, gather_one_intf },
};

static void GatherTest_init() {
  ScatterTest = GetModuleID("ScatterTest");
}

struct mola_module_spec __module_spec = {
  .name = "GatherTest", 
  .max_instances = 1,
  .init = GatherTest_init,
  .fini = NULL,
  .intf_num = 2,
  .intf_tbl = gather_intf_tbl,
};

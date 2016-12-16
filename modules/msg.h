#ifndef _MODULES_MSG_H_
#define _MODULES_MSG_H_

#include "runtime.h"

typedef struct {
  int a;
} MessageA;

// FIXME : generate only one copy for each program!!
static vtable_t MessageA__vtable = {
    .size = sizeof(MessageA),
};

#endif // _MODULES_MSG_H_

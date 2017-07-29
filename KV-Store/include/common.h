#ifndef _COMMON_H_
#define _COMMON_H_

#include<sys/time.h>
#include <stdio.h>

#include <mola/framework.h>

#define __USING_SINGLE_SECTION__ \
__MOCC_IGNORE_CHECK_BEGIN__ \
static int __g_init_counter = 0; \
__MOCC_IGNORE_CHECK_END__

#define __SINGLE_INIT_BEGIN__ \
  if (__sync_fetch_and_add(&__g_init_counter, 1) == 0) {

#define __SINGLE_INIT_END__ }

#define __SINGLE_FINI_BEGIN__ \
  if (__sync_sub_and_fetch(&__g_init_counter, 1) == 0) {

#define __SINGLE_FINI_END__ }

//#define DEBUG

typedef struct _timer {
    struct timeval start;
    struct timeval end;
} ptimer;

#define start_timer(tv) \
    do { \
        gettimeofday(&(tv.start), NULL); \
    }while(0)
#define end_timer(tv, str) \
    do { \
        gettimeofday(&(tv.end), NULL); \
        printf("%s %lf ms passed.\n", str, (tv.end.tv_sec - tv.start.tv_sec) * 1000 \
                + (tv.end.tv_usec - tv.start.tv_usec) / 1000.0); \
    } while(0) 

#ifdef DEBUG
#define debug(module, ret) { \
	int __i = 0;\
	printf("%s\n", &module);\
	for(__i = 0; __i < ret->buf.len; __i++) {\
		printf("%c", (char) ret->buf.data[__i]);\
	} }\
	printf("\n") 
#else
#define debug(module, ret)
#endif

#define ict_err(...) fprintf(stderr, __VA_ARGS__)
#ifdef DEBUG
# define ict_debug(...) printf(__VA_ARGS__)
#else
# define ict_debug(...)
#endif

#define ict_assert(A) \
do {\
  if (!(A)) {\
    fprintf(stderr, "%s:%d: assertion '%s' failed\n", __FILE__, __LINE__, #A);\
    abort();\
  }\
} while (0)




typedef struct bucket_t {
  char* key;
  char* value;
  size_t ksize;
  size_t vsize;
} Bucket;

typedef struct vector_t {
  Bucket* bk;
  size_t  cap;
  size_t  size;
} Vector;

#define VECTOR_CAP 4

#define N_ACTOR_POWER 16
// FIXME: POWER must be less than 32 now.
#define N_INSTANCE_POWER 16
#define KEY_RANGE_MASK ((0xffffffffu)>>(32-N_INSTANCE_POWER))
#define ACTOR_MASK ((0xffffffffu)>>(32-N_ACTOR_POWER))

#define NewBuffer(size) ({\
  Buffer *buffer = NewMsg(Buffer);\
  buffer->content.len = size;\
  buffer->content.data = (unsigned char*)MolaAlloc(size);\
  buffer; })

#define _DAT(buf) ((buf)->content.data)
#define _LEN(buf) ((buf)->content.len)



#define AddRepeatedField(msg, field, type, num) \
  do {\
    (msg)->n_##field = (num); \
    (msg)->field = (type*) MolaAlloc((num) * sizeof(type)); \
  } while (0)

#define FreeRepeatedField(msg, field) \
  do { \
    (msg)->n_##field = 0; \
    MolaFree((msg)->field); \
    (msg)->field = NULL; \
  } while (0)



#include <pthread.h>
#include "queue.h"

typedef struct lockObj {
	pthread_spinlock_t lock;
	Queue* que;
	int isLocked;
} LockObj;


#endif

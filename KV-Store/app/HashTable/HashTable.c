#include <mola/framework.h>

#include "me-pg.h"
#include "common.h"

#include "HashTable-pg.h"




// -------- constructor / destructor ------------ //

__USING_SINGLE_SECTION__;

void HashTable_init() {

    __SINGLE_INIT_BEGIN__
    {
    }
    __SINGLE_INIT_END__
    //printf("Module hashtable instance id: %lx\n", GetMyInstID());
    uint64_t n_actor_per_inst = ((uint64_t)1) << (N_ACTOR_POWER - N_INSTANCE_POWER);
    vec = (Vector*)malloc(sizeof(Vector) * n_actor_per_inst);
    if (vec == NULL) printf("vec is null\n");
    int i = 0;
    for (; i < n_actor_per_inst; i++) {
        vec[i].bk = NULL;
	vec[i].size = 0;
    }
    //printf("Module hashtable is initialized\n");
}

void HashTable_exit() {

    __SINGLE_FINI_BEGIN__
    {
    }
    __SINGLE_FINI_END__
    free(vec);
}
// ----------------------------------------- //

// ------------ interfaces --------------- //

// It takes about 0.002 msecs.
Ret* OnGetReq(GetReq* greq) {
    //ptimer tv;
    //start_timer(tv);
    Ret* ret = NewMsg(Ret);
    char* key = (char*)greq->key;
    size_t ksize = greq->ksize;
    
    uint32_t actor_id = greq->hash_key & ACTOR_MASK;
    actor_id = actor_id >> N_INSTANCE_POWER;
    Vector* bk = &vec[actor_id];
    int flag = 0;
    size_t i = 0; 
    for (; i < bk->size; i++) {
        if ( (bk->bk[i].ksize == ksize) &&
             (memcmp(bk->bk[i].key, key, ksize) == 0)) {
            ret->key = (uint64_t)bk->bk[i].key;
            ret->value = (uint64_t)bk->bk[i].value;
            ret->ksize = bk->bk[i].ksize;
	    ret->vsize = bk->bk[i].vsize;
	    flag = 1;
            break;
	} 
    }
    if (flag == 0) {
        ret->key = (uint64_t)NULL;
	ret->value = (uint64_t)NULL;
    }
    //end_timer(tv, "HashTable OnGetReq");
    return ret;
}

Ret* OnSetReq(SetReq* sreq) {   
    //ptimer tv;
    //start_timer(tv);
    
    char* key = (char*)sreq->key;
    char* value = (char*)sreq->value;
    size_t ksize = sreq->ksize;
    size_t vsize = sreq->vsize;

    // search to find if the key exists
    uint32_t actor_id = sreq->hash_key & ACTOR_MASK;
    actor_id = actor_id >> N_INSTANCE_POWER;
    Vector* bk = &vec[actor_id];
    int is_exist = 0;

    size_t i = 0;
    for (; i < bk->size; i++) {
        if ( (bk->bk[i].ksize == ksize) &&
         (memcmp(bk->bk[i].key, key, ksize) == 0)) {
            bk->bk[i].vsize = vsize;
	    if(bk->bk[i].value) free(bk->bk[i].value);
	    bk->bk[i].value = value;
	    is_exist = 1;
	    break;
        }
    }
    // for the case that the key doesnot exist, add it to the bucket
    if (is_exist == 0) {
        if (bk->bk == NULL) {
	    bk->bk = (Bucket*)malloc(sizeof(Bucket) * (VECTOR_CAP));
	    bk->size = 0;
	    bk->cap = VECTOR_CAP;
	}
        if (bk->size == bk->cap) {
	  bk->bk = (Bucket*)realloc((void*)bk->bk, sizeof(Bucket)*bk->cap*2);
	  bk->cap = bk->cap << 1;
	}
	bk->bk[bk->size].key = key;
	bk->bk[bk->size].value = value;
	bk->bk[bk->size].ksize = ksize;
	bk->bk[bk->size].vsize = vsize;
	bk->size++;
    }

    Ret* ret = NewMsg(Ret);
    ret->key = 1;
    //printf("send back to hash\n");
    //end_timer(tv, "HashTable OnSetReq");
    return ret;
}

#include <mola/framework.h>

#include "me-pg.h"
#include "common.h"

#include "Hash-pg.h"

__MOCC_IMPORT__("HashTable.pg");

// pre declaration of hash function

// Our experimental platform is little endian
#define ENDIAN_LITTLE 1

#if ENDIAN_LITTLE == 1
#define HASH_LITTLE_ENDIAN 1
#define HASH_BIG_ENDIAN 0
#endif

#define rot(x,k) (((x)<<(k)) ^ ((x)>>(32-(k))))
#define mix(a,b,c) \
{ \
  a -= c;  a ^= rot(c, 4);  c += b; \
  b -= a;  b ^= rot(a, 6);  a += c; \
  c -= b;  c ^= rot(b, 8);  b += a; \
  a -= c;  a ^= rot(c,16);  c += b; \
  b -= a;  b ^= rot(a,19);  a += c; \
  c -= b;  c ^= rot(b, 4);  b += a; \
}
#define final(a,b,c) \
{ \
  c ^= b; c -= rot(b,14); \
  a ^= c; a -= rot(c,11); \
  b ^= a; b -= rot(a,25); \
  c ^= b; c -= rot(b,16); \
  a ^= c; a -= rot(c,4);  \
  b ^= a; b -= rot(a,14); \
  c ^= b; c -= rot(b,24); \
}

int parseAndProcRecvCmd(void *recv_buf);
uint32_t jenkins_hash(const void *key, size_t length); 

//uint32_t (*hash)(const void*, size_t);

// -------- constructor / destructor ------------ //

__USING_SINGLE_SECTION__;

void Hash_init() {

    __SINGLE_INIT_BEGIN__
    {
  //      hash = jenkins_hash;
    }
    __SINGLE_INIT_END__
    printf("Module hash instance id: %llx\n", GetMyInstID());
}

void Hash_exit() {

    __SINGLE_FINI_BEGIN__
    {
    }
    __SINGLE_FINI_END__
}
// ----------------------------------------- //

// ------------ interfaces --------------- //

Ret* OnReq(Req* req) {
    //ptimer tv;
    //start_timer(tv);
    
    parseAndProcRecvCmd(_DAT(req->buf));

    // do some boring things
    /*int i = 0;
    for (; i < 100000; i++) {
        ;
    }*/
    //end_timer(tv, "Hash OnReq");
    return NULL;
}


Ret* OnReqRet(Ret *ret) {
    //printf("hash get ret message\n");
    return CopyMsg(ret); 
}

// ----------- internal functions ---------- //
//
int parseAndProcRecvCmd(void *recv_buf) {
    int retc = 0;
    int op = -1;
    char *pSubStr = NULL;
    
    if (recv_buf == NULL)
        return -1;

    ict_debug("procRecvCmd:begin cmd=%s\n", recv_buf);
    pSubStr = (char *)recv_buf;
    
    if (0 == strncmp(pSubStr, "set", 3)) {
        SetReq *sreq = NewMsg(SetReq);
        pSubStr += 4;
        op = 1;
        ict_debug("cmd op is set\n");
	
	// key field
	size_t ksize = strlen(pSubStr);
        char*  key   = malloc(ksize+1);
        memcpy(key, pSubStr, ksize);
        key[ksize] = 0;

	// value field
        pSubStr += ksize + 5;
        size_t vsize = atoi(pSubStr);
        pSubStr += 3;
        char* value = malloc(vsize+1);
	memcpy(value, pSubStr, vsize);
        value[vsize] = 0;

        // hash the key to ring space
        uint32_t hv = jenkins_hash((void*)key, ksize);   
        uint32_t inst_id = hv & KEY_RANGE_MASK;
        uint32_t node_id = 0;
        uint32_t id = (node_id<<16) | inst_id;
        
	sreq->key = (uint64_t)key;
	sreq->value = (uint64_t)value;
	sreq->ksize = ksize;
	sreq->vsize = vsize;
	sreq->hash_key = hv;

        SendMsgToInst("HashTable", id, sreq);
    }

    else if (0 == strncmp(pSubStr, "get", 3)) {
        pSubStr += 4;
	char* tmp = strchr(pSubStr, '\r');
	if (tmp == NULL) {
	  printf("request format error: no \\r appears!\n");
	  exit(0);
	}
	size_t ksize = tmp - pSubStr;
        op = 0;
        ict_debug("cmd op is get\n");

        GetReq *greq = NewMsg(GetReq);

	// key field
	char* key = malloc(ksize);
        memcpy(key, pSubStr, ksize);
        
	// hash the key to ring space
        uint32_t hv = jenkins_hash((void*)key, ksize);   
        uint32_t inst_id = hv & KEY_RANGE_MASK;
 
        uint32_t node_id = 0;
        uint32_t id = (node_id<<16) | inst_id;
       
        greq->key = (uint64_t)key;
	greq->ksize = ksize;
	greq->hash_key = hv;

        SendMsgToInst("HashTable", id, greq);
    }
    //free(ids);


__end:
    return retc;
}



/*
-------------------------------------------------------------------------------
mix -- mix 3 32-bit values reversibly.

This is reversible, so any information in (a,b,c) before mix() is
still in (a,b,c) after mix().

If four pairs of (a,b,c) inputs are run through mix(), or through
mix() in reverse, there are at least 32 bits of the output that
are sometimes the same for one pair and different for another pair.
This was tested for:
* pairs that differed by one bit, by two bits, in any combination
  of top bits of (a,b,c), or in any combination of bottom bits of
  (a,b,c).
* "differ" is defined as +, -, ^, or ~^.  For + and -, I transformed
  the output delta to a Gray code (a^(a>>1)) so a string of 1's (as
  is commonly produced by subtraction) look like a single 1-bit
  difference.
* the base values were pseudorandom, all zero but one bit set, or
  all zero plus a counter that starts at zero.

Some k values for my "a-=c; a^=rot(c,k); c+=b;" arrangement that
satisfy this are
    4  6  8 16 19  4
    9 15  3 18 27 15
   14  9  3  7 17  3
Well, "9 15 3 18 27 15" didn't quite get 32 bits diffing
for "differ" defined as + with a one-bit base and a two-bit delta.  I
used http://burtleburtle.net/bob/hash/avalanche.html to choose
the operations, constants, and arrangements of the variables.

This does not achieve avalanche.  There are input bits of (a,b,c)
that fail to affect some output bits of (a,b,c), especially of a.  The
most thoroughly mixed value is c, but it doesn't really even achieve
avalanche in c.

This allows some parallelism.  Read-after-writes are good at doubling
the number of bits affected, so the goal of mixing pulls in the opposite
direction as the goal of parallelism.  I did what I could.  Rotates
seem to cost as much as shifts on every machine I could lay my hands
on, and rotates are much kinder to the top and bottom bits, so I used
rotates.
-------------------------------------------------------------------------------
*/

/*
-------------------------------------------------------------------------------
final -- final mixing of 3 32-bit values (a,b,c) into c

Pairs of (a,b,c) values differing in only a few bits will usually
produce values of c that look totally different.  This was tested for
* pairs that differed by one bit, by two bits, in any combination
  of top bits of (a,b,c), or in any combination of bottom bits of
  (a,b,c).
* "differ" is defined as +, -, ^, or ~^.  For + and -, I transformed
  the output delta to a Gray code (a^(a>>1)) so a string of 1's (as
  is commonly produced by subtraction) look like a single 1-bit
  difference.
* the base values were pseudorandom, all zero but one bit set, or
  all zero plus a counter that starts at zero.

These constants passed:
 14 11 25 16 4 14 24
 12 14 25 16 4 14 24
and these came close:
  4  8 15 26 3 22 24
 10  8 15 26 3 22 24
 11  8 15 26 3 22 24
-------------------------------------------------------------------------------
*/

uint32_t jenkins_hash(
  const void *key,       /* the key to hash */
  size_t      length)    /* length of the key */
{
  uint32_t a,b,c;                                          /* internal state */
  union { const void *ptr; size_t i; } u;     /* needed for Mac Powerbook G4 */
  
  /* Set up the internal state */
  a = b = c = 0xdeadbeef + ((uint32_t)length) + 0;

  u.ptr = key;
  if (HASH_LITTLE_ENDIAN && ((u.i & 0x3) == 0)) {
    const uint32_t *k = key;                           /* read 32-bit chunks */
#ifdef VALGRIND
    const uint8_t  *k8;
#endif /* ifdef VALGRIND */

    /*------ all but last block: aligned reads and affect 32 bits of (a,b,c) */
    while (length > 12)
    {
      a += k[0];
      b += k[1];
      c += k[2];
      mix(a,b,c);
      length -= 12;
      k += 3;
    }

    /*----------------------------- handle the last (probably partial) block */
    /*
     * "k[2]&0xffffff" actually reads beyond the end of the string, but
     * then masks off the part it's not allowed to read.  Because the
     * string is aligned, the masked-off tail is in the same word as the
     * rest of the string.  Every machine with memory protection I've seen
     * does it on word boundaries, so is OK with this.  But VALGRIND will
     * still catch it and complain.  The masking trick does make the hash
     * noticeably faster for short strings (like English words).
     */
#ifndef VALGRIND

    switch(length)
    {
    case 12: c+=k[2]; b+=k[1]; a+=k[0]; break;
    case 11: c+=k[2]&0xffffff; b+=k[1]; a+=k[0]; break;
    case 10: c+=k[2]&0xffff; b+=k[1]; a+=k[0]; break;
    case 9 : c+=k[2]&0xff; b+=k[1]; a+=k[0]; break;
    case 8 : b+=k[1]; a+=k[0]; break;
    case 7 : b+=k[1]&0xffffff; a+=k[0]; break;
    case 6 : b+=k[1]&0xffff; a+=k[0]; break;
    case 5 : b+=k[1]&0xff; a+=k[0]; break;
    case 4 : a+=k[0]; break;
    case 3 : a+=k[0]&0xffffff; break;
    case 2 : a+=k[0]&0xffff; break;
    case 1 : a+=k[0]&0xff; break;
    case 0 : return c;  /* zero length strings require no mixing */
    }

#else /* make valgrind happy */

    k8 = (const uint8_t *)k;
    switch(length)
    {
    case 12: c+=k[2]; b+=k[1]; a+=k[0]; break;
    case 11: c+=((uint32_t)k8[10])<<16;  /* fall through */
    case 10: c+=((uint32_t)k8[9])<<8;    /* fall through */
    case 9 : c+=k8[8];                   /* fall through */
    case 8 : b+=k[1]; a+=k[0]; break;
    case 7 : b+=((uint32_t)k8[6])<<16;   /* fall through */
    case 6 : b+=((uint32_t)k8[5])<<8;    /* fall through */
    case 5 : b+=k8[4];                   /* fall through */
    case 4 : a+=k[0]; break;
    case 3 : a+=((uint32_t)k8[2])<<16;   /* fall through */
    case 2 : a+=((uint32_t)k8[1])<<8;    /* fall through */
    case 1 : a+=k8[0]; break;
    case 0 : return c;  /* zero length strings require no mixing */
    }

#endif /* !valgrind */

  } else if (HASH_LITTLE_ENDIAN && ((u.i & 0x1) == 0)) {
    const uint16_t *k = key;                           /* read 16-bit chunks */
    const uint8_t  *k8;

    /*--------------- all but last block: aligned reads and different mixing */
    while (length > 12)
    {
      a += k[0] + (((uint32_t)k[1])<<16);
      b += k[2] + (((uint32_t)k[3])<<16);
      c += k[4] + (((uint32_t)k[5])<<16);
      mix(a,b,c);
      length -= 12;
      k += 6;
    }

    /*----------------------------- handle the last (probably partial) block */
    k8 = (const uint8_t *)k;
    switch(length)
    {
    case 12: c+=k[4]+(((uint32_t)k[5])<<16);
             b+=k[2]+(((uint32_t)k[3])<<16);
             a+=k[0]+(((uint32_t)k[1])<<16);
             break;
    case 11: c+=((uint32_t)k8[10])<<16;     /* @fallthrough */
    case 10: c+=k[4];                       /* @fallthrough@ */
             b+=k[2]+(((uint32_t)k[3])<<16);
             a+=k[0]+(((uint32_t)k[1])<<16);
             break;
    case 9 : c+=k8[8];                      /* @fallthrough */
    case 8 : b+=k[2]+(((uint32_t)k[3])<<16);
             a+=k[0]+(((uint32_t)k[1])<<16);
             break;
    case 7 : b+=((uint32_t)k8[6])<<16;      /* @fallthrough */
    case 6 : b+=k[2];
             a+=k[0]+(((uint32_t)k[1])<<16);
             break;
    case 5 : b+=k8[4];                      /* @fallthrough */
    case 4 : a+=k[0]+(((uint32_t)k[1])<<16);
             break;
    case 3 : a+=((uint32_t)k8[2])<<16;      /* @fallthrough */
    case 2 : a+=k[0];
             break;
    case 1 : a+=k8[0];
             break;
    case 0 : return c;  /* zero length strings require no mixing */
    }

  } else {                        /* need to read the key one byte at a time */
    const uint8_t *k = key;

    /*--------------- all but the last block: affect some 32 bits of (a,b,c) */
    while (length > 12)
    {
      a += k[0];
      a += ((uint32_t)k[1])<<8;
      a += ((uint32_t)k[2])<<16;
      a += ((uint32_t)k[3])<<24;
      b += k[4];
      b += ((uint32_t)k[5])<<8;
      b += ((uint32_t)k[6])<<16;
      b += ((uint32_t)k[7])<<24;
      c += k[8];
      c += ((uint32_t)k[9])<<8;
      c += ((uint32_t)k[10])<<16;
      c += ((uint32_t)k[11])<<24;
      mix(a,b,c);
      length -= 12;
      k += 12;
    }

    /*-------------------------------- last block: affect all 32 bits of (c) */
    switch(length)                   /* all the case statements fall through */
    {
    case 12: c+=((uint32_t)k[11])<<24;
    case 11: c+=((uint32_t)k[10])<<16;
    case 10: c+=((uint32_t)k[9])<<8;
    case 9 : c+=k[8];
    case 8 : b+=((uint32_t)k[7])<<24;
    case 7 : b+=((uint32_t)k[6])<<16;
    case 6 : b+=((uint32_t)k[5])<<8;
    case 5 : b+=k[4];
    case 4 : a+=((uint32_t)k[3])<<24;
    case 3 : a+=((uint32_t)k[2])<<16;
    case 2 : a+=((uint32_t)k[1])<<8;
    case 1 : a+=k[0];
             break;
    case 0 : return c;  /* zero length strings require no mixing */
    }
  }

  final(a,b,c);
  return c;             /* zero length strings require no mixing */
}


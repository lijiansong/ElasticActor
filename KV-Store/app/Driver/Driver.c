#include <mola/framework.h>

#include "me-pg.h"
#include "common.h"

#include "Driver-pg.h"

__MOCC_IMPORT__("Hash.pg");

#define HW_TESTBENCH_HDSZ 6
#define HW_TESTBENCH_MAX_MOUNT 50000

static const unsigned int MAX_PROXY_NUM = 1;
static const unsigned short BASE_PORT = 50000;

static size_t get_header_size(void *ctx) { return HW_TESTBENCH_HDSZ; }

static size_t get_body_size(void *ctx, const char *header, size_t size) {
    assert(size == HW_TESTBENCH_HDSZ);
    return atoi(header);
}

static void release_buf(void *ctx, char *buffer) { free(buffer); }

int parseAndProcRecvCmd(void *recv_buf);

// -------- constructor / destructor ------------ //

__USING_SINGLE_SECTION__;

void Driver_init() {

    __SINGLE_INIT_BEGIN__
    {
        /* setting the customed message protocol */
        proto.header_size = get_header_size;
        proto.body_size = get_body_size;
        proto.release_buffer = release_buf;

        int i;
        for (i = 0; i < MAX_PROXY_NUM; ++i) {
            sockfd[i] = StartServer("*", BASE_PORT + i, &proto);
            assert(sockfd != NULL);
        }
    }
    __SINGLE_INIT_END__
    printf("Module driver instance id: %llx\n", GetMyInstID());
    /*int num = GetNodeCount();
    uint32_t* ids = malloc(sizeof(uint32_t) * num);
    if(num == GetNodeIDs(ids)) {
        printf("%d nodes are:\n", num);
        int i = 0;
        for (; i < num; i++)
            printf("%d ", ids[i]);
        printf("\n");
    }
    free(ids);*/
}

void Driver_exit() {

    __SINGLE_FINI_BEGIN__
    {
        int i;
        for (i = 0; i < MAX_PROXY_NUM; ++i)
            CloseSocket(sockfd[i]);
    }
    __SINGLE_FINI_END__
}
// ----------------------------------------- //

// ------------ interfaces --------------- //
void OnNewPackage(const Package *package) {
    //ptimer tv;
    //start_timer(tv);
    if (package->size == 0) {
        ict_debug("new test bench!\n");
        // BindSocketToInst(package->client);
        return;
    }

    // add a '\0' at the end of string
    package->buffer[package->size] = '\0';

    // set the client socket
    client = package->client;

    // a new test input package ..
    //ptimer tv;
    //start_timer(tv);    
    Req* req = NewMsg(Req);
    req->buf = NewBuffer(package->size + 1);
    memcpy(_DAT(req->buf), package->buffer, package->size + 1);
    _LEN(req->buf) = package->size;
    SendMsgWithCtx("Hash", req, client);
    //end_timer(tv, "Driver OnNewPackage");
}

void OnReqReturn(const Ret *ret) {
    //ptimer tv;
    //start_timer(tv);
    //printf("driver get ret message\n");
    char *sendBuf = NULL;
    sendBuf = (char *)malloc(512);
    if (NULL == sendBuf) {
        ict_debug("[TGT ERROR] cannot malloc a send buffer!\n");
        exit(0);
    }
    size_t length;
    if (ret->key == NULL) {
        length = sprintf(sendBuf, "VALUE miss 4 4\r\nmiss\r\nEND\r\n");
    }
    else if (ret->key == 1) {
        length = sprintf(sendBuf, "VALUE aaa 3 3\r\nbbb\r\nEND\r\n");
    }
    else {
        length = sprintf(sendBuf, "VALUE %s %d %d\r\n%s\r\nEND\r\n", ret->key, ret->ksize, ret->vsize, ret->value);
    }
    ict_debug("%s", sendBuf);

    client = GetSavedCtx();
    ict_assert(client != NULL);

    SendToSocket(client, length, sendBuf);
    //end_timer(tv, "Driver OnReqReturn");
}


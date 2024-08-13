#ifndef COMMON_H
#define COMMON_H

#include <infiniband/verbs.h>

#define SERVER_PORT 20079
#define BUFFER_SIZE 1024
#define KEY_VALUE_SIZE 256

enum msg_type {
    MSG_GET,
    MSG_PUT
};

struct kv_pair {
    char key[KEY_VALUE_SIZE];
    char value[KEY_VALUE_SIZE];
};

struct message {
    enum msg_type type;
    struct kv_pair kv;
    uint64_t addr;  // 서버 메모리 주소
};

struct rdma_context {
    struct ibv_context *ctx;
    struct ibv_pd *pd;
    struct ibv_mr *mr;
    struct ibv_cq *cq;
    struct ibv_qp *qp;
    struct ibv_comp_channel *comp_channel;
    void *buffer;
};

#endif // COMMON_H


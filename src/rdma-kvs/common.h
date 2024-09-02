#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>

#include <endian.h>
#if __BYTE_ORDER == __BIG_ENDIAN
#define htonll(x) (x)
#define ntohll(x) (x)
#else
#define htonll(x) __builtin_bswap64(x)
#define ntohll(x) __builtin_bswap64(x)
#endif //endian.h

// Constants
#define KEY_VALUE_SIZE 256
#define BUFFER_SIZE (KEY_VALUE_SIZE * 3)
#define SERVER_PORT 20079
#define TIMEOUT_IN_MS 500
#define CQ_CAPACITY 16
#define MAX_SGE 1
#define MAX_WR 16

struct pdata { 
    uint64_t buf_va; 
    uint32_t buf_rkey;
};

enum msg_type {
    MSG_PUT,
    MSG_GET
};

struct kv_pair {
    char key[KEY_VALUE_SIZE];
    char value[KEY_VALUE_SIZE];
    struct kv_pair *next;
};

struct message {
    enum msg_type type;
    struct kv_pair kv;
};

struct rdma_context {
    struct ibv_device *device;
    struct ibv_context *verbs;
    struct ibv_pd *pd;
    struct ibv_comp_channel *comp_channel;
    struct ibv_cq *cq;
    struct ibv_cq *evt_cq;
    struct ibv_qp *qp;
    struct ibv_mr *send_mr, *recv_mr;
};

void build_context(struct rdma_context *ctx, struct rdma_cm_id *id);
void build_qp_attr(struct ibv_qp_init_attr *attr, struct rdma_context *ctx);

#endif // COMMON_H

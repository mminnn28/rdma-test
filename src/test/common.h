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

// #if __BYTE_ORDER == __LITTLE_ENDIAN
// static inline uint64_t htonll(uint64_t x) { return bswap_64(x); }
// static inline uint64_t ntohll(uint64_t x) { return bswap_64(x); }
// #elif __BYTE_ORDER == __BIG_ENDIAN
// static inline uint64_t htonll(uint64_t x) { return x; }
// static inline uint64_t ntohll(uint64_t x) { return x; }
// #else
// #error __BYTE_ORDER is neither __LITTLE_ENDIAN nor __BIG_ENDIAN
// #endif

static inline uint64_t bswap_64(uint64_t x) {
    return ((x & 0x00000000000000FFULL) << 56) |
           ((x & 0x000000000000FF00ULL) << 40) |
           ((x & 0x0000000000FF0000ULL) << 24) |
           ((x & 0x00000000FF000000ULL) << 8)  |
           ((x & 0x000000FF00000000ULL) >> 8)  |
           ((x & 0x0000FF0000000000ULL) >> 24) |
           ((x & 0x00FF000000000000ULL) >> 40) |
           ((x & 0xFF00000000000000ULL) >> 56);
}

/* Error Macro*/
#define rdma_error(msg, args...) do {\
	fprintf(stderr, "%s : %d : ERROR : "msg, __FILE__, __LINE__, ## args);\
}while(0);

// Constants
#define KEY_VALUE_SIZE 256
#define BUFFER_SIZE (KEY_VALUE_SIZE * 2)
#define SERVER_PORT 20079
#define TIMEOUT_IN_MS 500
#define CQ_CAPACITY 16
#define MAX_SGE 1
#define MAX_WR 16
#define MAX_KEYS 256

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
};

struct message {
    enum msg_type type;
    struct kv_pair kv;
    uint64_t addr; 
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

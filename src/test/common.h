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

// #include <endian.h>
// #if __BYTE_ORDER == __BIG_ENDIAN
// #define htonll(x) (x)
// #define ntohll(x) (x)
// #else
// #define htonll(x) __builtin_bswap64(x)
// #define ntohll(x) __builtin_bswap64(x)
// #endif //endian.h

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

// Define message types
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


struct __attribute((packed)) rdma_buffer_attr {
  uint64_t address;
  uint32_t length;
  union stag {
	  /* if we send, we call it local stags */
	  uint32_t local_stag;
	  /* if we receive, we call it remote stag */
	  uint32_t remote_stag;
  }stag;
};

// RDMA context structure
struct rdma_context {
    struct ibv_device *device;
    struct ibv_context *verbs;
    struct ibv_pd *pd;
    struct ibv_comp_channel *comp_channel;
    struct ibv_cq *cq;
    struct ibv_qp *qp;
    struct ibv_mr *send_mr, *recv_mr;
};

// Function prototypes
void build_context(struct rdma_context *ctx, struct rdma_cm_id *id);
void build_qp_attr(struct ibv_qp_init_attr *attr, struct rdma_context *ctx);
void cleanup(struct rdma_cm_id *id);

#endif // COMMON_H

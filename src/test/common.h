#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>

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

// RDMA context structure
struct rdma_context {
    struct ibv_device *device;
    struct ibv_context *verbs;
    struct ibv_pd *pd;
    struct ibv_comp_channel *comp_channel;
    struct ibv_cq *cq;
    struct ibv_qp *qp;
    
    struct ibv_mr * send_mr;
	struct ibv_mr * recv_mr;

    char *send_buffer;
	char *recv_buffer;

    pthread_t poll_send_thread;
    pthread_t poll_recv_thread;
};

// Function prototypes
void build_context(struct rdma_context *ctx, struct rdma_cm_id *id);
void build_qp_attr(struct ibv_qp_init_attr *attr, struct rdma_context *ctx);
struct ibv_mr* rdma_buffer(struct rdma_context *ctx);
void recv_msg(struct rdma_context *ctx);
void * poll_send_cq(void * context);
void * poll_recv_cq(void * context);
int on_completion(struct ibv_wc *wc);

#endif // COMMON_H


#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>

// Define constants
#define KEY_VALUE_SIZE 256
#define BUFFER_SIZE (KEY_VALUE_SIZE * 2)
#define SERVER_PORT 20079

/* Capacity of the completion queue (CQ) */
#define CQ_CAPACITY (16)
/* MAX SGE capacity */
#define MAX_SGE (1)
/* MAX work requests */
#define MAX_WR (10)

// Define message types
enum msg_type {
    MSG_PUT,
    MSG_GET
};

// Define message structure
struct message {
    enum msg_type type;
    struct {
        char key[KEY_VALUE_SIZE];
        char value[KEY_VALUE_SIZE];
    } kv;
    uintptr_t addr;
};

// RDMA context structure
struct rdma_context {
    struct ibv_device *device;
    struct ibv_context *verbs;
    struct ibv_pd *pd;
    struct ibv_comp_channel *comp_channel;
    struct ibv_cq *cq;
    struct ibv_qp *qp;
    struct ibv_mr *mr;
    void *buffer;
};

// Function prototypes
void build_context(struct rdma_context *ctx);
void build_qp_attr(struct ibv_qp_init_attr *attr, struct rdma_context *ctx);
struct ibv_mr* rdma_buffer(struct rdma_context *ctx);
void show_rdma_buffer_attr(struct rdma_buffer_attr *attr);
int process_rdma_cm_event(struct rdma_event_channel *echannel, 
		enum rdma_cm_event_type expected_event,
		struct rdma_cm_event **cm_event);

// void on_completion(struct ibv_wc *wc);

#endif // COMMON_H

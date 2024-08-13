#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <rdma/rdma_cma.h>

void build_context(struct ibv_context *verbs, struct rdma_context *ctx) {
    ctx->ctx = verbs;
    ctx->pd = ibv_alloc_pd(ctx->ctx);
    ctx->comp_channel = ibv_create_comp_channel(ctx->ctx);
    ctx->cq = ibv_create_cq(ctx->ctx, 10, NULL, ctx->comp_channel, 0);
    ibv_req_notify_cq(ctx->cq, 0);
}

void build_qp_attr(struct ibv_qp_init_attr *qp_attr, struct rdma_context *ctx) {
    memset(qp_attr, 0, sizeof(*qp_attr));
    qp_attr->send_cq = ctx->cq;
    qp_attr->recv_cq = ctx->cq;
    qp_attr->qp_type = IBV_QPT_RC;
    qp_attr->cap.max_send_wr = 10;
    qp_attr->cap.max_recv_wr = 10;
    qp_attr->cap.max_send_sge = 1;
    qp_attr->cap.max_recv_sge = 1;
}

void register_memory(struct rdma_context *ctx) {
    ctx->buffer = malloc(BUFFER_SIZE);
    ctx->mr = ibv_reg_mr(ctx->pd, ctx->buffer, BUFFER_SIZE, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ);
}

void post_receives(struct rdma_context *ctx) {
    struct ibv_sge sge;
    struct ibv_recv_wr wr, *bad_wr = NULL;

    sge.addr = (uintptr_t)ctx->buffer;
    sge.length = BUFFER_SIZE;
    sge.lkey = ctx->mr->lkey;

    memset(&wr, 0, sizeof(wr));
    wr.wr_id = (uintptr_t)ctx;
    wr.sg_list = &sge;
    wr.num_sge = 1;

    ibv_post_recv(ctx->qp, &wr, &bad_wr);
}

#include "common.h"

void build_context(struct rdma_context *ctx, struct rdma_cm_id *id) {
    // device
    struct ibv_device **device_list = ibv_get_device_list(NULL);
    ctx->device = device_list[0]; 
    ctx->verbs = ibv_open_device(ctx->device); 
    ibv_free_device_list(device_list); 

    // resource
    ctx->pd = ibv_alloc_pd(id->verbs);
    if (!ctx->pd) {
        perror("ibv_alloc_pd");
        exit(EXIT_FAILURE);
	}
    //id->context = (void *)malloc(sizeof(struct rdma_context));

    ctx->comp_channel = ibv_create_comp_channel(id->verbs); 
    if (!ctx->comp_channel) {
        perror("ibv_create_comp_channel");
        exit(EXIT_FAILURE);
	}

    ctx->cq = ibv_create_cq(id->verbs, CQ_CAPACITY, NULL, ctx->comp_channel, 0);
    if (!ctx->cq) {
        perror("ibv_create_cq");
        exit(EXIT_FAILURE);
	}

    if (ibv_req_notify_cq(ctx->cq, 0)) {
        perror("ibv_req_notify_cq");
        exit(EXIT_FAILURE);
    }
}

void build_qp_attr(struct ibv_qp_init_attr *attr, struct rdma_context *ctx) {

    memset(attr, 0, sizeof(*attr));
    attr->qp_type = IBV_QPT_RC;

    attr->cap.max_send_wr = MAX_WR;
    attr->cap.max_recv_wr = MAX_WR;
    attr->cap.max_send_sge = MAX_SGE;
    attr->cap.max_recv_sge = MAX_SGE;

    if (!ctx->cq) {
        fprintf(stderr, "Completion Queue (CQ) is not initialized.\n");
        exit(EXIT_FAILURE);
    }

    attr->send_cq = ctx->cq;
    attr->recv_cq = ctx->cq;

    attr->srq = NULL;
    attr->sq_sig_all = 0;
}


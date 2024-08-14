#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <rdma/rdma_cma.h>

#define KV_STORE_SIZE 1024

struct kv_store {
    char key[KEY_VALUE_SIZE];
    char value[KEY_VALUE_SIZE];
} kv_store[KV_STORE_SIZE];

void on_connect(struct rdma_cm_id *id) {
    printf("Client connected.\n");
    struct rdma_context *ctx = (struct rdma_context *)id->context;
    post_receives(ctx);
}

void on_completion(struct ibv_wc *wc) {
    struct rdma_context *ctx = (struct rdma_context *)(uintptr_t)wc->wr_id;
    struct message *msg = (struct message *)ctx->buffer;

    if (wc->opcode == IBV_WC_RECV) {
        if (msg->type == MSG_PUT) {
            // PUT 요청 처리
            int i;
            for (i = 0; i < KV_STORE_SIZE; i++) {
                if (kv_store[i].key[0] == '\0' || strcmp(kv_store[i].key, msg->kv.key) == 0) {
                    strcpy(kv_store[i].key, msg->kv.key);
                    strcpy(kv_store[i].value, msg->kv.value);
                    msg->addr = (uint64_t)&kv_store[i];
                    printf("Stored key '%s' at memory address: %p\n", kv_store[i].key, &kv_store[i]);
                    break;
                }
            }
        } else if (msg->type == MSG_GET) {
            // GET 요청 처리
            int i;
            for (i = 0; i < KV_STORE_SIZE; i++) {
                if (strcmp(kv_store[i].key, msg->kv.key) == 0) {
                    strcpy(msg->kv.value, kv_store[i].value);
                    msg->addr = (uint64_t)&kv_store[i];
                    printf("Retrieved key '%s' from memory address: %p\n", kv_store[i].key, &kv_store[i]);
                    break;
                }
            }
        }

        struct ibv_sge sge;
        struct ibv_send_wr wr, *bad_wr = NULL;

        sge.addr = (uintptr_t)ctx->buffer;
        sge.length = sizeof(struct message);
        sge.lkey = ctx->mr->lkey;

        memset(&wr, 0, sizeof(wr));
        wr.wr_id = (uintptr_t)ctx;
        wr.opcode = IBV_WR_SEND;
        wr.sg_list = &sge;
        wr.num_sge = 1;

        ibv_post_send(ctx->qp, &wr, &bad_wr);
        post_receives(ctx);
    }
}

int main() {
    struct rdma_cm_id *listener;
    struct rdma_event_channel *ec;
    struct rdma_cm_event *event;
    struct rdma_cm_id *id;
    struct rdma_context *ctx;
    struct ibv_qp_init_attr qp_attr;
    struct sockaddr_in addr;
    
    memset(&kv_store, 0, sizeof(kv_store));

    ec = rdma_create_event_channel();
    rdma_create_id(ec, &listener, NULL, RDMA_PS_TCP);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SERVER_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    rdma_bind_addr(listener, (struct sockaddr *)&addr);
    rdma_listen(listener, 1);

    while (rdma_get_cm_event(ec, &event) == 0) {
        id = event->id;

        if (event->event == RDMA_CM_EVENT_CONNECT_REQUEST) {
            ctx = (struct rdma_context *)malloc(sizeof(struct rdma_context));
            build_context(id->verbs, ctx);
            build_qp_attr(&qp_attr, ctx);
            rdma_create_qp(id, ctx->pd, &qp_attr);
            ctx->qp = id->qp;
            register_memory(ctx);
            id->context = ctx;

            rdma_accept(id, NULL);
            on_connect(id);
        } else if (event->event == RDMA_CM_EVENT_ESTABLISHED) {
            // 이미 on_connect에서 처리되었으므로 여기에선 생략.
        } else if (event->event == RDMA_CM_EVENT_DISCONNECTED) {
            rdma_destroy_qp(id);
            ibv_dereg_mr(ctx->mr);
            free(ctx->buffer);
            ibv_destroy_cq(ctx->cq);
            ibv_destroy_comp_channel(ctx->comp_channel);
            ibv_dealloc_pd(ctx->pd);
            free(ctx);
            rdma_destroy_id(id);
            printf("Client disconnected.\n");
        }

        rdma_ack_cm_event(event);
    }

    rdma_destroy_id(listener);
    rdma_destroy_event_channel(ec);

    return 0;
}

//root@node-1:/users/Jeongeun/rdma-kvs# ./server
//Client connected.

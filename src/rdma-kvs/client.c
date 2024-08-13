#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <rdma/rdma_cma.h>

void on_completion(struct ibv_wc *wc) {
    struct rdma_context *ctx = (struct rdma_context *)(uintptr_t)wc->wr_id;
    struct message *msg = (struct message *)ctx->buffer;

    if (wc->opcode == IBV_WC_RECV) {
        if (msg->type == MSG_PUT) {
            printf("PUT operation completed. Stored at server address: %p\n", (void *)msg->addr);
        } else if (msg->type == MSG_GET) {
            printf("GET operation completed. Value: %s (from server address: %p)\n", msg->kv.value, (void *)msg->addr);
        }
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <server-ip>\n", argv[0]);
        return 1;
    }

    struct rdma_cm_id *conn = NULL;
    struct rdma_event_channel *ec = NULL;
    struct rdma_cm_event *event = NULL;
    struct rdma_context ctx;
    struct ibv_qp_init_attr qp_attr;
    struct sockaddr_in addr;
    struct message *msg;

    ec = rdma_create_event_channel();
    rdma_create_id(ec, &conn, NULL, RDMA_PS_TCP);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SERVER_PORT);
    addr.sin_addr.s_addr = inet_addr(argv[1]);

    rdma_resolve_addr(conn, NULL, (struct sockaddr *)&addr, 2000);
    rdma_get_cm_event(ec, &event);
    rdma_ack_cm_event(event);

    build_context(conn->verbs, &ctx);
    build_qp_attr(&qp_attr, &ctx);
    rdma_create_qp(conn, ctx.pd, &qp_attr);
    ctx.qp = conn->qp;
    register_memory(&ctx);
    conn->context = &ctx;

    rdma_resolve_route(conn, 2000);
    rdma_get_cm_event(ec, &event);
    rdma_ack_cm_event(event);

    rdma_connect(conn, NULL);
    rdma_get_cm_event(ec, &event);
    rdma_ack_cm_event(event);

    while (1) {
        char command[10];
        struct message msg_out;

        printf("Enter command (put/get): ");
        scanf("%s", command);

        if (strcmp(command, "put") == 0) {
            msg_out.type = MSG_PUT;
            printf("Enter key: ");
            scanf("%s", msg_out.kv.key);
            printf("Enter value: ");
            scanf("%s", msg_out.kv.value);
        } else if (strcmp(command, "get") == 0) {
            msg_out.type = MSG_GET;
            printf("Enter key: ");
            scanf("%s", msg_out.kv.key);
        } else {
            continue;
        }

        memcpy(ctx.buffer, &msg_out, sizeof(msg_out));

        struct ibv_sge sge;
        struct ibv_send_wr wr, *bad_wr = NULL;

        sge.addr = (uintptr_t)ctx.buffer;
        sge.length = sizeof(struct message);
        sge.lkey = ctx.mr->lkey;

        memset(&wr, 0, sizeof(wr));
        wr.wr_id = (uintptr_t)&ctx;
        wr.opcode = IBV_WR_SEND;
        wr.sg_list = &sge;
        wr.num_sge = 1;

        ibv_post_send(ctx.qp, &wr, &bad_wr);

        struct ibv_wc wc;
        while (ibv_poll_cq(ctx.cq, 1, &wc)) {
            on_completion(&wc);
        }
    }

    rdma_disconnect(conn);
    rdma_destroy_qp(conn);
    ibv_dereg_mr(ctx.mr);
    free(ctx.buffer);
    ibv_destroy_cq(ctx.cq);
    ibv_destroy_comp_channel(ctx.comp_channel);
    ibv_dealloc_pd(ctx.pd);
    rdma_destroy_id(conn);
    rdma_destroy_event_channel(ec);

    return 0;
}
/*
./client 10.10.1.1
Enter command (put/get): put a b
Enter key: Enter value: Enter command (put/get): get a
Enter key: Enter command (put/get): get a
Enter key: Enter command (put/get): put a b
Enter key: Enter value: Enter command (put/get): ^C
 */

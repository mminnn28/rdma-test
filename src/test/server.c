#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>

// RDMA 연결 및 큐 페어 설정
void setup_connection() {
    struct rdma_cm_id *id = NULL;
    struct rdma_event_channel *ec = NULL;
    struct rdma_cm_event *event = NULL;
    struct rdma_context ctx;
    struct ibv_qp_init_attr qp_attr;
    struct sockaddr_in addr;
    int ret;

    ec = rdma_create_event_channel();
    if (!ec) {
        perror("rdma_create_event_channel");
        exit(EXIT_FAILURE);
    }

    ret = rdma_create_id(ec, &id, NULL, RDMA_PS_TCP);
    if (ret) {
        perror("rdma_create_id");
        exit(EXIT_FAILURE);
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SERVER_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    ret = rdma_bind_addr(id, (struct sockaddr *)&addr);
    if (ret) {
        perror("rdma_bind_addr");
        exit(EXIT_FAILURE);
    }

    ret = rdma_listen(id, 0);
    if (ret) {
        perror("rdma_listen");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d...\n", SERVER_PORT);

    while (1) {
        ret = rdma_get_cm_event(ec, &event);
        if (ret) {
            perror("rdma_get_cm_event");
            exit(EXIT_FAILURE);
        }

        if (event->event == RDMA_CM_EVENT_CONNECT_REQUEST) {
            printf("Client connected.\n");

            struct rdma_cm_id *client_id = event->id;
            build_context(&ctx);
            build_qp_attr(&qp_attr, &ctx);
            ret = rdma_create_qp(client_id, ctx.pd, &qp_attr);
            if (ret) {
                perror("rdma_create_qp");
                exit(EXIT_FAILURE);
            }

            client_id->context = &ctx;
            ret = rdma_post_recv(client_id, NULL, ctx.buffer, BUFFER_SIZE, ctx.mr);
            if (ret) {
                perror("rdma_post_recv");
                exit(EXIT_FAILURE);
            }

            rdma_ack_cm_event(event);
            post_receives(&ctx);
            while (1) {
                struct ibv_wc wc;
                int num_completions = ibv_poll_cq(ctx.cq, 1, &wc);
                if (num_completions > 0) {
                    on_completion(&wc);
                }
            }
        } else {
            rdma_ack_cm_event(event);
        }
    }
}

int main() {
    setup_connection();
    return 0;
}

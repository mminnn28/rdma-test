#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h> 

#define MSG_SIZE 16

int main(int argc, char **argv) {

    struct rdma_cm_event *event = NULL;
    struct rdma_event_channel *ec = NULL;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <IP address>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *ip_address = argv[1];
    
    // 메시지 저장을 위한 메모리 할당
    char *msg = malloc(MSG_SIZE);
    if (!msg) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    // 메시지 입력 받기
    printf("Enter a message: ");
    if (!fgets(msg, MSG_SIZE, stdin)) {
        perror("fgets");
        free(msg);
        exit(EXIT_FAILURE);
    }

    // fgets로 입력된 문자열에는 개행 문자가 포함될 수 있으므로 제거
    msg[strcspn(msg, "\n")] = '\0';

    // 입력받은 메시지 확인
    printf("Message to send: %s\n\n", msg);

    /*event-based*/
    printf("Creating event channel...\n");
    ec = rdma_create_event_channel();
    if (!ec) {
        perror("rdma_create_event_channel");
        free(msg);
        exit(EXIT_FAILURE);
    }

    struct rdma_cm_id *cm_id = NULL;
    printf("Resolving address...\n");
    if (rdma_resolve_addr(cm_id, NULL, (struct sockaddr *)&addr, 2000)) {
        perror("rdma_resolve_addr");
        exit(EXIT_FAILURE);
    }

    while (rdma_get_cm_event(ec, &event) == 0) {
        if (event->event == RDMA_CM_EVENT_ADDR_RESOLVED) {
            printf("Address resolved.\n");
            
            /*Scatter/Gather entry 정의*/
            struct ibv_sge sge = {
                .addr = (uintptr_t)ctx.buffer,
                .length = strlen(ctx.buffer) + 1,
                .lkey = ctx.mr->lkey
            };

            /*send work request*/
            struct ibv_send_wr send_wr = {
                .wr_id = 0,
                .next = NULL,
                .sg_list = &sge,
                .num_sge = 1,
                .opcode = IBV_WR_SEND,
                .send_flags = IBV_SEND_SIGNALED
            };

            /*send work request를 queue에 post*/
            struct ibv_send_wr *bad_wr;
            if (ibv_post_send(ctx.qp, &send_wr, &bad_wr)) {
                die("Failed to post send work request");
            }   

            printf("Resolving route...\n");
            if (rdma_resolve_route(cm_id, 2000)) {
                perror("rdma_resolve_route");
                ibv_dereg_mr(mr);
                ibv_dealloc_pd(pd);
                rdma_ack_cm_event(event);
                rdma_destroy_id(cm_id);
                rdma_destroy_event_channel(ec);
                exit(EXIT_FAILURE);
            }

            rdma_ack_cm_event(event);
        } else if (event->event == RDMA_CM_EVENT_ROUTE_RESOLVED) {
            printf("Route resolved.\n");

            memset(&qp_attr, 0, sizeof(qp_attr));
            qp_attr.cap.max_send_wr = 1;
            qp_attr.cap.max_recv_wr = 1;
            qp_attr.cap.max_send_sge = 1;
            qp_attr.cap.max_recv_sge = 1;
            qp_attr.sq_sig_all = 1;
            qp_attr.qp_type = IBV_QPT_RC;

            printf("Creating QP...\n");
            if (rdma_create_qp(cm_id, pd, &qp_attr)) {
                perror("rdma_create_qp");
                ibv_dereg_mr(mr);
                ibv_dealloc_pd(pd);
                rdma_ack_cm_event(event);
                rdma_destroy_id(cm_id);
                rdma_destroy_event_channel(ec);
                free(msg);
                exit(EXIT_FAILURE);
            }

            conn_param.initiator_depth = 1;
            conn_param.retry_count = 7;

            printf("Connecting...\n");
            if (rdma_connect(cm_id, &conn_param)) {
                perror("rdma_connect");
                ibv_dereg_mr(mr);
                ibv_dealloc_pd(pd);
                rdma_destroy_qp(cm_id);
                rdma_ack_cm_event(event);
                rdma_destroy_id(cm_id);
                rdma_destroy_event_channel(ec);
                free(msg);
                exit(EXIT_FAILURE);
            }
            printf("Connection initiated.\n");

            rdma_ack_cm_event(event);
        } else if (event->event == RDMA_CM_EVENT_ESTABLISHED) {
            printf("Connection established.\n");

            if (ibv_post_send(cm_id->qp, &send_wr, NULL)) {
                perror("ibv_post_send");
                ibv_dereg_mr(mr);
                ibv_dealloc_pd(pd);
                rdma_destroy_qp(cm_id);
                rdma_ack_cm_event(event);
                rdma_destroy_id(cm_id);
                rdma_destroy_event_channel(ec);
                free(msg);
                exit(EXIT_FAILURE);
            }

            printf("Message sent: %s\n", msg);
            rdma_ack_cm_event(event);
        } else if (event->event == RDMA_CM_EVENT_DISCONNECTED) {
            printf("Connection closed.\n");
            rdma_ack_cm_event(event);
            break;
        }
    }

    // Cleanup resources
    if (ec) rdma_destroy_event_channel(ec);

    return 0;
}

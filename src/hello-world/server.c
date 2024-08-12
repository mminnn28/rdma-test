#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>  // For INADDR_ANY

#define MSG_SIZE 16

int main(int argc, char **argv) {
    struct rdma_cm_id *cm_id = NULL;
    struct rdma_cm_event *event = NULL;
    struct rdma_event_channel *ec = NULL;
    struct ibv_pd *pd = NULL;
    struct ibv_mr *mr = NULL;
    struct ibv_sge sge = {};
    struct ibv_recv_wr recv_wr = {};
    struct rdma_conn_param conn_param = {};
    struct ibv_qp_init_attr qp_attr = {};

    char *msg = malloc(MSG_SIZE);
    if (!msg) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    strcpy(msg, "Hello World!");

    ec = rdma_create_event_channel();
    if (!ec) {
        perror("rdma_create_event_channel");
        free(msg);
        exit(EXIT_FAILURE);
    }

    if (rdma_create_id(ec, &cm_id, NULL, RDMA_PS_TCP)) {
        perror("rdma_create_id");
        rdma_destroy_event_channel(ec);
        free(msg);
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(20079);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (rdma_bind_addr(cm_id, (struct sockaddr *)&addr)) {
        perror("rdma_bind_addr");
        rdma_destroy_id(cm_id);
        rdma_destroy_event_channel(ec);
        free(msg);
        exit(EXIT_FAILURE);
    }

    if (rdma_listen(cm_id, 1)) {
        perror("rdma_listen");
        rdma_destroy_id(cm_id);
        rdma_destroy_event_channel(ec);
        free(msg);
        exit(EXIT_FAILURE);
    }

    while (rdma_get_cm_event(ec, &event) == 0) {
        if (event->event == RDMA_CM_EVENT_CONNECT_REQUEST) {
            printf("Connection requested.\n");

            if (!event->id) {
                fprintf(stderr, "Event ID is NULL\n");
                rdma_ack_cm_event(event);
                rdma_destroy_id(cm_id);
                rdma_destroy_event_channel(ec);
                free(msg);
                exit(EXIT_FAILURE);
            }

            // Set up Protection Domain and Memory Region
            pd = ibv_alloc_pd(event->id->verbs);
            if (!pd) {
                perror("ibv_alloc_pd");
                rdma_ack_cm_event(event);
                rdma_destroy_id(cm_id);
                rdma_destroy_event_channel(ec);
                free(msg);
                exit(EXIT_FAILURE);
            }

            mr = ibv_reg_mr(pd, msg, MSG_SIZE, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE);
            if (!mr) {
                perror("ibv_reg_mr");
                ibv_dealloc_pd(pd);
                rdma_ack_cm_event(event);
                rdma_destroy_id(cm_id);
                rdma_destroy_event_channel(ec);
                free(msg);
                exit(EXIT_FAILURE);
            }

            sge.addr = (uintptr_t)msg;
            sge.length = MSG_SIZE;
            sge.lkey = mr->lkey;

            recv_wr.sg_list = &sge;
            recv_wr.num_sge = 1;

            // Create Queue Pair
            memset(&qp_attr, 0, sizeof(qp_attr));
            qp_attr.cap.max_send_wr = 1;
            qp_attr.cap.max_recv_wr = 1;
            qp_attr.cap.max_send_sge = 1;
            qp_attr.cap.max_recv_sge = 1;
            qp_attr.sq_sig_all = 1;
            qp_attr.qp_type = IBV_QPT_RC;

            if (rdma_create_qp(event->id, pd, &qp_attr)) {
                perror("rdma_create_qp");
                ibv_dereg_mr(mr);
                ibv_dealloc_pd(pd);
                rdma_ack_cm_event(event);
                rdma_destroy_id(cm_id);
                rdma_destroy_event_channel(ec);
                free(msg);
                exit(EXIT_FAILURE);
            }
            printf("Queue Pair created: %p\n", (void*)event->id->qp);

            if (ibv_post_recv(event->id->qp, &recv_wr, NULL)) {
                perror("ibv_post_recv");
                rdma_destroy_qp(event->id);
                ibv_dereg_mr(mr);
                ibv_dealloc_pd(pd);
                rdma_ack_cm_event(event);
                rdma_destroy_id(cm_id);
                rdma_destroy_event_channel(ec);
                free(msg);
                exit(EXIT_FAILURE);
            }
            printf("Recv WR posted.\n");

            conn_param.responder_resources = 1;
            conn_param.initiator_depth = 1;
            conn_param.retry_count = 7;

            printf("Accepting connection...\n");
            if (rdma_accept(event->id, &conn_param)) {
                perror("rdma_accept");
                rdma_destroy_qp(event->id);
                ibv_dereg_mr(mr);
                ibv_dealloc_pd(pd);
                rdma_ack_cm_event(event);
                rdma_destroy_id(cm_id);
                rdma_destroy_event_channel(ec);
                free(msg);
                exit(EXIT_FAILURE);
            }
            printf("Connection accepted.\n");

            rdma_ack_cm_event(event);
        } else if (event->event == RDMA_CM_EVENT_ESTABLISHED) {
            printf("Connection established.\n");
            rdma_ack_cm_event(event);
        } else if (event->event == RDMA_CM_EVENT_DISCONNECTED) {
            printf("Connection closed.\n");
            rdma_ack_cm_event(event);
            break;
        }
    }

    // Cleanup resources
    if (msg) free(msg);
    if (pd) ibv_dealloc_pd(pd);
    if (cm_id) rdma_destroy_id(cm_id);
    if (ec) rdma_destroy_event_channel(ec);

    return 0;
}

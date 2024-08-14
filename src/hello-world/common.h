/**
 * Header file for RDMA client/server example
 * 2024.08.12
 * 
 */
#ifndef COMMON_H
#define COMMON_H

#include <stdlib.h>
#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>

#define BUFFER_SIZE 1024

struct rdma_context {
    struct ibv_context *context; // RDMA 장치
    struct ibv_pd *pd; //protection domain -> RDMA 리소스 간의 메모리 접근 권한을 관리
    struct ibv_mr *mr; // 메모리 버퍼를 RDMA 메모리 영역으로 등록
    struct ibv_qp *qp; //queue pair
    struct ibv_cq *cq; // completion queue
    struct ibv_comp_channel *comp_channel; // completeion channel -> RDMA 작업의 완료 상태를 관리하고 통지
    char *buffer; // 메모리 버퍼
    uint32_t rkey;
    uint64_t remote_addr;
};

void die(const char *reason);
void create_rdma_resources(struct rdma_context *ctx);

#endif /*COMMON_H*/

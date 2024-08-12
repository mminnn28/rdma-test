#include "rdma_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*에러 메시지 출력*/
void die(const char *reason) {
    fprintf(stderr, "%s Error code: %d\n", reason, errno);
    exit(EXIT_FAILURE);
}

void create_rdma_resource(struct rdma_context *ctx)
{
    /*사용 가능한 RDMA 장치 목록 가져오기*/
    struct ibv_device **dev_list = ibv_get_device_list(NULL);
    if (!dev_list) {
        die("Failed to get IB devices list");
    }

    /*특정 RDMA 장치 열기 - RDMA resource 생성에 사용된다*/
    ctx->context = ibv_open_device(dev_list[0]);
    if (!ctx->context) {
        die("Failed to open device");
    }

    /*ibv_open_device로 사용하려는 모든 장치 열고 ibv_free_device로 배열을 해제하여 열린 장치만 사용*/
    ibv_free_device_list(dev_list);

    /*protection domain 할당 및 RDMA에서 사용할 수 있도록 메모리 등록 - 리소스 간의 메모리 접근 권한 관리*/
    // context에 연결된 RDMA 장치에서 PD를 할당 (우선 PD가 존재하는지 확인 후 없으면 새로 할당)
    if(ctx->pd)
        return ctx->pd;
    else {
        ctx->pd = ibv_alloc_pd(ctx->context);
        if (!ctx->pd)
            die("ibv_alloc_pd failed");
        return ctx->pd;
    }

    /*사용할 메모리 버퍼 할당 - RDMA 데이터 송수신*/
    // flag -> 메모리 지역이 어떤 방식으로 사용될 지 명시
    // 로컬 쓰기, 원격 읽기, 원격 쓰기
    ctx->buffer = malloc(BUFFER_SIZE);
    ctx->mr = ibv_reg_mr(ctx->pd, ctx->buffer, BUFFER_SIZE, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE);
    if (!ctx->mr) {
        die("Failed to register memory region");
    }

    /*Completion Channel 생성*/
    ctx->comp_channel = ibv_create_comp_channel(ctx->context);
    if (!ctx->comp_channel) {
        die("Failed to create completion channel");
    }

    /*Completion queue 생성*/
    //10은 CQ의 엔트리 수를 지정 (저장할 수 있는 최대 수)
    //네 번째 인자는 비동기 이벤트를 받을 구조체를 지정 - 이벤트를 처리하지 않는 경우 NULL
    //마지막 인자는 comp_vector로 interrupt verctor를 지정한다. 0은 기본 벡터를 의미. (CPU 인터럽트를 처리할 벡터 지정)
    ctx->cq = ibv_create_cq(ctx->context, 10, ctx->comp_channel, NULL, 0);
    if (!ctx->cq) {
        die("Failed to create completion queue");
    }

    /*Completion Queue 초기화*/
    // Work는 작업 단위
    // scatter는 데이터를 여러 개의 메모리 지역으로 나누어 전송 (송신)
    // gather는 여러 개의 메모리 지역에서 데이터를 수집하여 하나의 위치로 수신
    struct ibv_qp_init_attr qp_init_attr = {
        .send_cq = ctx->cq,
        .recv_cq = ctx->cq,
        .cap = {
            .max_send_wr = 1, // 최대 송신 워크 요청 수 (10개로 설정하면 한 번에 10개의 송신 작업을 queue에 제출)
            .max_recv_wr = 1, // 최대 수신 워크 요청 수
            .max_send_sge = 1,// 최대 송신 Scatter/Gather 요소 수
            .max_recv_sge = 1 // 최대 수신 Scatter/Gather 요소 수
        },
        .qp_type = IBV_QPT_RC // Reliable Connection QP 유형
    };
    
    /*QP 생성*/
    ctx->qp = ibv_create_qp(ctx->pd, &qp_init_attr);
    if (!ctx->qp) {
        die("Failed to create queue pair");
    }
};

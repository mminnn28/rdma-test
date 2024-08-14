#include "common.h"

// RDMA 자원 초기화
void build_context(struct rdma_context *ctx) {
    struct ibv_device **device_list = ibv_get_device_list(NULL); // 사용 가능한 RDMA 장치 목록 가져오기
    ctx->device = device_list[0]; // 첫 번째 RDMA 장치 선택
    ctx->verbs = ibv_open_device(ctx->device); // 특정 RDMA 장치 열기
    ibv_free_device_list(device_list); // 장치 목록 해제
    ctx->pd = ibv_alloc_pd(ctx->verbs); // PD(Production domain) 할당 ("process abstraction")
    ctx->comp_channel = ibv_create_comp_channel(ctx->verbs); // Completion Channel 생성
    ctx->cq = ibv_create_cq(ctx->verbs, CQ_CAPACITY, NULL, ctx->comp_channel, 0); //Completion queue 생성
    ibv_req_notify_cq(ctx->cq, 0); // Completion queue에서 알림 요청
}

// 큐 페어 속성 설정
void build_qp_attr(struct ibv_qp_init_attr *attr, struct rdma_context *ctx) {
    memset(attr, 0, sizeof(*attr));
    attr->qp_type = IBV_QPT_RC;
    qp_attr->cap.max_send_wr = MAX_WR;
    qp_attr->cap.max_recv_wr = MAX_WR;
    qp_attr->cap.max_send_sge = MAX_SGE;
    qp_attr->cap.max_recv_sge = MAX_SGE;
    attr->send_cq = ctx->cq;
    attr->recv_cq = ctx->cq;
    attr->srq = NULL;
    attr->sq_sig_all = 0;
}

// 메모리 버퍼 할당 및 등록
struct ibv_mr* rdma_buffer(struct rdma_context *ctx) {
    enum ibv_access_flags permission = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ;

    if (!ctx->pd) {
		rdma_error("Protection domain is NULL \n");
		return NULL;
	}

    ctx->buffer = malloc(BUFFER_SIZE);
    if (!ctx->buffer) {
		rdma_error("failed to allocate buffer, -ENOMEM\n");
		return NULL;
	}

    ctx->mr = ibv_reg_mr(ctx->pd, ctx->buffer, BUFFER_SIZE, permission);
    if(!ctx->mr){
		rdma_error("Failed to register memory region, errno: %d \n", -errno);
        free(ctx->buffer);
        return NULL;
	}

    return ctx->mr;
}

void show_rdma_buffer_attr(struct rdma_buffer_attr *attr){
	if(!attr){
		rdma_error("Passed attr is NULL\n");
		return;
	}
	printf("---------------------------------------------------------\n");
	printf("buffer attr, addr: %p , len: %u , stag : 0x%x \n", 
			(void*) attr->address, 
			(unsigned int) attr->length,
			attr->stag.local_stag);
	printf("---------------------------------------------------------\n");
}


int process_work_completion_events (struct ibv_comp_channel *comp_channel, struct ibv_wc *wc, int max_wc)
{
	struct ibv_cq *cq_ptr = NULL;
	void *context = NULL;

	int ret = -1, i, total_wc = 0;
    
    // CQ 이벤트 대기 및 가져오기
	ret = ibv_get_cq_event(comp_channel, &cq_ptr, &context); 
    if (ret) {
	    rdma_error("Failed to get next CQ event due to %d \n", -errno);
	    return -errno;
    }
       
    //CQ에서 추가 알림 요청
    ret = ibv_req_notify_cq(cq_ptr, 0);
    if (ret){
	    rdma_error("Failed to request further notifications %d \n", -errno);
	    return -errno;
    }
      
    //CQ를 폴링하여 완료된 작업 가져오기. max_wc에 도달할 때까지 계속 폴링
    total_wc = 0;
    do {
	    ret = ibv_poll_cq(cq_ptr, max_wc - total_wc,wc + total_wc);
	    if (ret < 0) {
		    rdma_error("Failed to poll cq for wc due to %d \n", ret);
		    return ret;
	    }
	    total_wc += ret;
    } while (total_wc < max_wc); 
        //완료 상태 확인하기
        for( i = 0 ; i < total_wc ; i++) {
	        if (wc[i].status != IBV_WC_SUCCESS) {
		       rdma_error("Work completion (WC) has error status: %s at index %d",  ibv_wc_status_str(wc[i].status), i);
		       return -(wc[i].status);
	        }
    }
       //CQ 이벤트 확인 후 해당 이벤트 인식
       ibv_ack_cq_events(cq_ptr, 1);
       return total_wc; 
}

int process_rdma_cm_event(struct rdma_event_channel *echannel, 
		enum rdma_cm_event_type expected_event,
		struct rdma_cm_event **cm_event)
{
	int ret = 1;
	ret = rdma_get_cm_event(echannel, cm_event);
	
	/* lets see, if it was a good event */
	if(0 != (*cm_event)->status){
		rdma_error("CM event has non zero status: %d\n", (*cm_event)->status);
		ret = -((*cm_event)->status);
		/* important, we acknowledge the event */
		rdma_ack_cm_event(*cm_event);
		return ret;
	}
	/* if it was a good event, was it of the expected type */
	if ((*cm_event)->event != expected_event) {
		rdma_error("Unexpected event received: %s [ expecting: %s ]", 
				rdma_event_str((*cm_event)->event),
				rdma_event_str(expected_event));
		/* important, we acknowledge the event */
		rdma_ack_cm_event(*cm_event);
		return -1; // unexpected event :(
	}
	
	return ret;
}


// RDMA 자원 초기화
// void build_context(struct rdma_context *ctx) {
//     ctx->device = ibv_get_device_list(NULL)[0]; //사용 가능한 RDMA 장치 목록 가져오기
//     ctx->verbs = ibv_open_device(ctx->device); //특정 RDMA 장치 열기 - RDMA resource 생성에 사용된다
//     ibv_free_device_list(NULL); //ibv_open_device로 사용하려는 모든 장치 열고 ibv_free_device로 배열을 해제하여 열린 장치만 사용
//     ctx->pd = ibv_alloc_pd(ctx->verbs); // PD(Production domain) 할당 - 리소스 간의 메모리 접근 권한 관리
//     ctx->comp_channel = ibv_create_comp_channel(ctx->verbs); // Completion Channel 생성
//     ctx->cq = ibv_create_cq(ctx->verbs, 10, NULL, ctx->comp_channel, 0); //Completion queue 생성
//     ibv_req_notify_cq(ctx->cq, 0);
// }

// RDMA 작업 완료 후 호출되는 함수
// 수정 봐야 함
// void on_completion(struct ibv_wc *wc) {
//     if (wc->status == IBV_WC_SUCCESS) {
//         struct rdma_context *ctx = (struct rdma_context *)(uintptr_t)wc->wr_id;
//         struct message *msg = (struct message *)ctx->buffer;

//         if (msg->type == MSG_GET) {
//             // GET 요청 처리
//             msg->addr = (uintptr_t)ctx->buffer; // 예제에서는 buffer 주소를 사용
//         } else if (msg->type == MSG_PUT) {
//             // PUT 요청 처리
//             printf("PUT request received for key '%s'\n", msg->kv.key);
//             // 저장 위치는 예제에서는 buffer 주소를 사용
//             msg->addr = (uintptr_t)ctx->buffer;
//         }

//         struct ibv_sge sge;
//         struct ibv_send_wr wr, *bad_wr = NULL;

//         sge.addr = (uintptr_t)ctx->buffer;
//         sge.length = sizeof(struct message);
//         sge.lkey = ctx->mr->lkey;

//         memset(&wr, 0, sizeof(wr));
//         wr.wr_id = (uintptr_t)ctx;
//         wr.opcode = IBV_WR_SEND;
//         wr.sg_list = &sge;
//         wr.num_sge = 1;

//         if (ibv_post_send(ctx->qp, &wr, &bad_wr)) {
//             perror("ibv_post_send");
//             exit(EXIT_FAILURE);
//         }

//         post_receives(ctx);
//     } else {
//         fprintf(stderr, "Completion with error: %s\n", ibv_wc_status_str(wc->status));
//     }
// }

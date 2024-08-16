#include "common.h"

// RDMA 자원 초기화
void build_context(struct rdma_context *ctx, struct rdma_cm_id *id) {
    // device
    struct ibv_device **device_list = ibv_get_device_list(NULL);
    ctx->device = device_list[0]; 
    ctx->verbs = ibv_open_device(ctx->device); 
    ibv_free_device_list(device_list); 

    // RDMA context structure
    ctx->pd = ibv_alloc_pd(id->verbs);
    if (!ctx->pd) {
        perror("ibv_alloc_pd");
        exit(EXIT_FAILURE);
	}

    rdma_buffer(ctx);

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

	//pthread_create(&ctx->poll_send_thread,NULL,poll_send_cq,ctx);
	//pthread_create(&ctx->poll_recv_thread,NULL,poll_recv_cq,ctx);
}

// 큐 페어 속성 설정
void build_qp_attr(struct ibv_qp_init_attr *attr, struct rdma_context *ctx) {

    memset(attr, 0, sizeof(*attr));
    attr->qp_type = IBV_QPT_RC;

    attr->cap.max_send_wr = MAX_WR;
    attr->cap.max_recv_wr = MAX_WR;
    attr->cap.max_send_sge = MAX_SGE;
    attr->cap.max_recv_sge = MAX_SGE;

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

    ctx->send_buffer = malloc(BUFFER_SIZE);
	ctx->recv_buffer = malloc(BUFFER_SIZE);
    
	if (!ctx->send_buffer) {
		rdma_error("failed to allocate buffer, -ENOMEM\n");
		return NULL;
	}
	if (!ctx->recv_buffer) {
		rdma_error("failed to allocate buffer, -ENOMEM\n");
		return NULL;
	}

    ctx->send_mr = ibv_reg_mr(ctx->pd, ctx->send_buffer, BUFFER_SIZE, permission);
    if(!ctx->send_mr){
		rdma_error("Failed to register memory region, errno: %d \n", -errno);
        free(ctx->send_buffer);
        return NULL;
	}

	ctx->recv_mr = ibv_reg_mr(ctx->pd, ctx->recv_buffer, BUFFER_SIZE, permission);
    if(!ctx->recv_mr){
		rdma_error("Failed to register memory region, errno: %d \n", -errno);
        free(ctx->recv_buffer);
        return NULL;
	}
}

void recv_msg(struct rdma_context *ctx)
{
	struct ibv_sge sge;
    struct ibv_recv_wr wr, *bad_wr = NULL;

	memset(&wr,0,sizeof(wr));
	wr.wr_id = (uintptr_t)ctx;
    wr.next = NULL;
    wr.sg_list = &sge;
    wr.num_sge = MAX_SGE;

	sge.addr = (uintptr_t)ctx->recv_buffer;
    sge.length = BUFFER_SIZE;
    sge.lkey = ctx->send_mr->lkey;
	
	if (ibv_post_recv(ctx->qp,&wr,&bad_wr)) {
		fprintf(stderr, "ibv_post_send error.\n");
	}
}

void * poll_send_cq(void * context)
{
	printf("pollind send cq...\n");

	struct rdma_context *ctx = (struct rdma_context *)context;
	
	struct ibv_wc wc;
	struct ibv_cq * cq;
	void * tar;

	while(1) {
		ibv_get_cq_event(ctx->cq,&cq,&tar);
		ibv_ack_cq_events(cq,1);
		ibv_req_notify_cq(cq,0);
		
		int num;
		while(num = ibv_poll_cq(cq,1,&wc))
		{
			int ret = on_completion(&wc);
			//printf("num is %d.\n",num);
			//printf("i_send: %d.ret is %d.\n",++i_send,ret);
			
			if (ret) break;
		}
	}
	printf("never.\n");

	return NULL;
}

void * poll_recv_cq(void * context)
{
    printf("polling recv cq...\n");

	struct rdma_context *ctx = (struct rdma_context *)context;

    struct ibv_wc wc;
	struct ibv_cq * cq;
	void * tar;

	while(1) {
		ibv_get_cq_event(ctx->cq,&cq,&tar);
		ibv_ack_cq_events(cq,1);
		ibv_req_notify_cq(cq,0);
		
		int num;
		while(num = ibv_poll_cq(cq,1,&wc))
		{
			int ret = on_completion(&wc);
			//printf("num is %d.\n",num);
			//printf("i_send: %d.ret is %d.\n",++i_send,ret);
			
			if (ret) break;
		}
	}
	printf("never.\n");

	return NULL;
}

int on_completion(struct ibv_wc *wc) {
    struct rdma_cm_id *id = (struct rdma_cm_id *)(wc->wr_id);
    struct rdma_context *ctx = (struct rdma_context *)(id->context);
    
    // 오류 처리
    if (wc->status != IBV_WC_SUCCESS) {
        printf("Operation failed, opcode: %d, status: %d.\n", wc->opcode, wc->status);
        return -1;
    }
    
    // RDMA 작업의 완료 상태에 따라 처리
    switch (wc->opcode) {
        case IBV_WC_SEND:
            printf("SEND completed. opcode is %d.\n", wc->opcode);
            struct message *msg_send = (struct message *)ctx->send_buffer;
            printf("Sent message: key = %s, value = %s.\n", msg_send->kv.key, msg_send->kv.value);
            break;
        
        case IBV_WC_RECV:
            printf("RECV completed. opcode is %d.\n", wc->opcode);
            struct message *msg_recv = (struct message *)ctx->recv_buffer;
            printf("Received message: key = %s, value = %s.\n", msg_recv->kv.key, msg_recv->kv.value);

            // 메시지 유형에 따라 처리
            if (msg_recv->type == MSG_GET) {
				msg_recv->addr = (uintptr_t)ctx->recv_buffer;
                printf("Received GET request for key: %s.\n", msg_recv->kv.key);
                // 서버 메모리에서 key에 대한 value를 검색 후 응답
                // 실제 구현 시 서버 메모리에서 데이터를 검색하고, 응답을 보내는 코드 추가 필요
            } else if (msg_recv->type == MSG_PUT) {
				msg_recv->addr = (uintptr_t)ctx->recv_buffer;
                printf("Received PUT request for key: %s, value: %s.\n", msg_recv->kv.key, msg_recv->kv.value);
                // 서버 메모리에 데이터를 저장
                // 이때 addr 필드를 사용하여 서버 메모리의 특정 위치에 데이터 저장
                // 실제 구현 시 서버 메모리에 데이터를 저장하는 코드 추가 필요
            }
            break;
        
        case IBV_WC_RDMA_READ:
            printf("RDMA READ completed. opcode is %d.\n", wc->opcode);
            struct kv_pair *read_data = (struct kv_pair *)ctx->recv_buffer;
            printf("Read data: key = %s, value = %s.\n", read_data->key, read_data->value);
            break;
        
        case IBV_WC_RDMA_WRITE:
            printf("RDMA WRITE completed. opcode is %d.\n", wc->opcode);
            struct kv_pair *written_data = (struct kv_pair *)ctx->send_buffer;
            printf("Write data: key = %s, value = %s.\n", written_data->key, written_data->value);
            break;
        
        default:
            printf("Unknown opcode: %d.\n", wc->opcode);
            return -1;
    }

    return 0;
}


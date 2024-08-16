#include "common.h"

/* These are RDMA connection related resources */
static struct rdma_context ctx;
static struct rdma_cm_id *id = NULL;
static struct rdma_event_channel *ec = NULL;
static struct rdma_cm_event *event = NULL;
static struct ibv_qp_init_attr *qp_attr;

// Function prototypes
static int setup_connection(struct sockaddr_in *addr);
static int pre_post_recv_buffer();
static int connect_server();
int on_connection();
void send_put(struct rdma_cm_id * id, struct message msg_send);
void send_get(struct rdma_cm_id *id, struct message msg_send);


int main(int argc, char **argv) {

    /* 서버 구조체 설정*/
    struct sockaddr_in addr = {
        .sin_family = AF_INET, // IPv4
        .sin_port = htons(SERVER_PORT), // 서버의 포트번호(20079) htons를 통해 byte order를 network order로 변환
        .sin_addr.s_addr = inet_addr(argv[1]) // 서버의 IP 주소를 network byte order로 변환
    };

	
    memset(&addr, 0, sizeof(addr)); // 구조체를 모두 '0'으로 초기화

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <server-ip>\n", argv[0]);
        return 1;
    }

    int ret;

    getaddrinfo(argv[1], SERVER_PORT, NULL, &addr);
    
    ret = setup_connection(&addr);
    if (ret) { 
		rdma_error("Failed to setup client connection , ret = %d \n", ret);
		return ret;
	}
    ret = connect_server();
    if (ret) { 
		rdma_error("Failed to connect server , ret = %d \n", ret);
		return ret;
	}
	ret = on_connection();
    if (ret) { 
		rdma_error("Failed to connect , ret = %d \n", ret);
		return ret;
	}

	rdma_disconnect(id);
    rdma_destroy_qp(id);
    ibv_dereg_mr(ctx.send_mr);
    ibv_dereg_mr(ctx.recv_mr);
    free(ctx.send_buffer);
	free(ctx.recv_buffer);
    ibv_destroy_cq(ctx.cq);
    ibv_destroy_comp_channel(ctx.comp_channel);
    ibv_dealloc_pd(ctx.pd);
    rdma_destroy_id(conn);
    rdma_destroy_event_channel(ec);

    return 0;
}

static int setup_connection(struct sockaddr_in *addr) {
    
    int ret;

    /*  Open a channel used to report asynchronous communication event */
    // 이벤트를 수신할 채널을 작성
    printf("Creating event channel...\n");
    ec = rdma_create_event_channel();
    if (!ec) {
        perror("rdma_create_event_channel");
        exit(EXIT_FAILURE);
    }

    /* rdma_cm_id is the connection identifier (like socket) which is used to define an RDMA connection. */
    // ID를 할당
    printf("Creating RDMA identifier...\n");
    ret = rdma_create_id(ec, &id, NULL, RDMA_PS_TCP);
    if (ret) {
        perror("rdma_create_id");
        exit(EXIT_FAILURE);
    }

    /* RDMA 연결을 위한 주어진 주소를 해석하고 RDMA 연결을 설정하기 위한 초기 작업 */
    // 로컬 RDMA 장치를 확보하여 remote 주소에 도달
    printf("Resolving address...\n");
    ret = rdma_resolve_addr(id, NULL, (struct sockaddr *)&addr, TIMEOUT_IN_MS);
    if (ret) {
        perror("rdma_resolve_addr");
        exit(EXIT_FAILURE);
    }

    /* CM (Connection Manager) event channel에서 RDMA_CM_EVENT_ADDR_RESOLVED event 수신*/
    //RDMA_CM_EVENT_ADDR_RESOLVED 이벤트 대기
    printf("RDMA_CM_EVENT_ADDR_RESOLVED event...\n");
    ret = rdma_get_cm_event(ec, &event);
    if (ret) {
        perror("rdma_get_cm_event");
        exit(EXIT_FAILURE);
    }
    
    /* ack the event */
    // 클라이언트가 서버의 IP 주소를 기반으로 RDMA 주소를 성공적으로 얻음
    printf("Acknowledge the CM event(address resolved)...\n");
    ret = rdma_ack_cm_event(event);
    if (ret) {
        perror("rdma_ack_cm_event");
        exit(EXIT_FAILURE);
    }

    /* creating QP */
    build_context(&ctx); //RDMA 자원 초기화
    build_qp_attr(&qp_attr, &ctx);
    printf("Creating QP...\n\n");
    ret = rdma_create_qp(id, ctx.pd, &qp_attr);
    if (ret) {
        perror("rdma_create_qp");
        exit(EXIT_FAILURE);
    }

    /* Resolves an RDMA route to the destination address in order to establish a connection */
    //주소가 성공적으로 해결된 후, RDMA CM이 서버로의 경로를 해결하도록 요청
    printf("Resolving routing...\n");
    ret = rdma_resolve_route(id, 2000);
    if (ret) {
        perror("rdma_resolve_route");
        exit(EXIT_FAILURE);
    }

    /* CM (Connection Manager) event channel에서 RDMA_CM_EVENT_ROUTE_RESOLVED event 수신*/
    //RDMA_CM_EVENT_ROUTE_RESOLVED 이벤트 대기
    printf("RDMA_CM_EVENT_ROUTE_RESOLVED event...\n");
    ret = rdma_get_cm_event(ec, &event);
    if (ret) {
        perror("rdma_get_cm_event");
        exit(EXIT_FAILURE);
    }
    
    /* ack the event */
    // routing 완료
    printf("Acknowledge the CM event(routing resolved)...\n\n");
    ret = rdma_ack_cm_event(event);
    if (ret) {
        perror("rdma_ack_cm_event");
        exit(EXIT_FAILURE);
    }

    return 0;
}

//QP 실험해보고 삭제하던가
static int connect_server() {

    int ret = -1;

    struct rdma_conn_param conn_param = {
		.initiator_depth = 3, //The maximum number of outstanding RDMA read and atomic operations
		.responder_resources = 3,
		.retry_count = 3 // if fail, then how many times to retry
	};

    memset(&conn_param, 0, sizeof(conn_param));

    printf("Connecting...\n");
    ret = rdma_connect(id, &conn_param);
    if (ret) {
        perror("rdma_connect");
        exit(EXIT_FAILURE);
    }
    printf("Connection initiated.\n");

    /* CM (Connection Manager) event channel에서 RDMA_CM_EVENT_ESTABLISHED event 수신*/
    //RDMA_CM_EVENT_ESTABLISHED 이벤트 대기
    printf("RDMA_CM_EVENT_ESTABLISHED event...\n");
    ret = rdma_get_cm_event(ec, &event);
    if (ret) {
        perror("rdma_get_cm_event");
        exit(EXIT_FAILURE);
    }
    printf("Connection established.\n");
    
    /* ack the event */
    printf("Acknowledge the CM event...\n\n");
    ret = rdma_ack_cm_event(event);
    if (ret) {
        perror("rdma_ack_cm_event");
        exit(EXIT_FAILURE);
    }
    printf("The client is connected successfully \n");

    //여기는 출력이 안되면 걍 생략하는 걸로
    if (!event->id->qp) {
        printf("Queue pair is NULL\n");
        exit(EXIT_FAILURE);
    }
    // 과연 출력을 할까...?
    printf("Queue pair: %p\n", event->id->qp); // id? event->id?

    return ret;
}

int on_connection()
{
	while(1) {
		char command[256];
		struct message msg_send;

		printf("Enter command (put <key> <value> /get <key>): ");
    	scanf("%s", command);

		memcpy(ctx.send_buffer,&msg_send,sizeof(struct message));

		if (strcmp(command, "put") == 0) {
            scanf("%s %s", msg_send.kv.key, msg_send.kv.value);
            send_put(id, msg_send);
        } else if (strcmp(command, "get") == 0) {
            scanf("%s", msg_send.kv.key);
            send_get(id, msg_send);
        } else {
            printf("Invalid command  \n");
        }
	}

	return 0;
}

// put
void send_put(struct rdma_cm_id * id, struct message msg_send) {

    snprintf(ctx.send_buffer, BUFFER_SIZE, "PUT %s %s", msg_send.kv.key, msg_send.kv.value);
    
    struct ibv_send_wr wr, *bad_wr = NULL;
    struct ibv_sge sge;

	memset(&wr, 0, sizeof(wr));

    wr.wr_id = (uintptr_t)id;
    wr.sg_list = &sge;
    wr.num_sge = MAX_SGE;
    wr.opcode = IBV_WR_SEND;
    wr.send_flags = IBV_SEND_SIGNALED;

	sge.addr = (uintptr_t)ctx.send_buffer;
    sge.length = BUFFER_SIZE;
    sge.lkey = ctx.send_mr->lkey;

    if (ibv_post_send(id->qp, &wr, &bad_wr)) {
        fprintf(stderr, "PUT ibv_post_send error. \n");
    }

	//recv_msg(id);
}

// get
void send_get(struct rdma_cm_id *id, struct message msg_send) {
    
    snprintf(ctx.send_buffer, BUFFER_SIZE, "GET %s", msg_send.kv.key);

    struct ibv_send_wr wr, *bad_wr = NULL;
    struct ibv_sge sge;

    memset(&wr, 0, sizeof(wr));

    wr.wr_id = (uintptr_t)id;
    wr.sg_list = &sge;
    wr.num_sge = MAX_SGE;
    wr.opcode = IBV_WR_SEND;
    wr.send_flags = IBV_SEND_SIGNALED;

	sge.addr = (uintptr_t)ctx.send_buffer;
    sge.length = BUFFER_SIZE;
    sge.lkey = ctx.send_mr->lkey;

    if (ibv_post_send(id->qp, &wr, &bad_wr)) {
        fprintf(stderr, "GET ibv_post_send error. \n");
    }

	recv_msg(&ctx);
}



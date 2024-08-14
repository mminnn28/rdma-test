#include "common.h"

/* These are RDMA connection related resources */
static struct rdma_context ctx;
static struct rdma_cm_id *id = NULL;
static struct rdma_event_channel *ec = NULL;
static struct rdma_cm_event *event = NULL;
static struct ibv_qp_init_attr *qp_attr;

/* These are memory buffers related resources */
// mr = memory region
static struct ibv_mr *client_meta_mr = NULL, 
		     *client_src_mr = NULL, 
		     *client_dst_mr = NULL, 
		     *server_meta_mr = NULL;
static struct rdma_buffer_attr client_meta_attr, server_meta_attr;
static struct ibv_send_wr client_send_wr, *bad_client_send_wr = NULL;
static struct ibv_recv_wr server_recv_wr, *bad_server_recv_wr = NULL;
static struct ibv_sge client_send_sge, server_recv_sge;

// Function prototypes
static int setup_connection(struct sockaddr_in *addr);
static int pre_post_recv_buffer();
static int connect_server();
static int exchange_metadata();
static int client_remote_memory_ops();
static int client_disconnect_and_clean();

int main(int argc, char **argv) {

    /* 서버 구조체 설정*/
    struct sockaddr_in addr; // 수신할 서버의 정보를 담은 소켓 구조체
    memset(&addr, 0, sizeof(addr)); // 구조체를 모두 '0'으로 초기화
    addr.sin_family = AF_INET; // IPv4
    addr.sin_port = htons(SERVER_PORT); // (굳이 없어도 될 듯) 서버의 포트번호(20079) htons를 통해 byte order를 network order로 변환
    addr.sin_addr.s_addr = inet_addr(argv[1]); // 서버의 IP 주소를 network byte order로 변환

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <server-ip>\n", argv[0]);
        return 1;
    }

    int ret;

    ret = setup_connection(&addr);
    if (ret) { 
		rdma_error("Failed to setup client connection , ret = %d \n", ret);
		return ret;
	}
    ret = pre_post_recv_buffer();
    if (ret) { 
		rdma_error("Failed to post receives , ret = %d \n", ret);
		return ret;
	}
    ret = connect_server();
    if (ret) { 
		rdma_error("Failed to connect server , ret = %d \n", ret);
		return ret;
	}

    //여기서부터
    ret = exchange_metadata();
	if (ret) {
		rdma_error("Failed to setup client connection , ret = %d \n", ret);
		return ret;
	}
	ret = client_remote_memory_ops();
	if (ret) {
		rdma_error("Failed to finish remote memory ops, ret = %d \n", ret);
		return ret;
	}
	// if (check_src_dst()) {
	// 	rdma_error("src and dst buffers do not match \n");
	// } else {
	// 	printf("...\nSUCCESS, source and destination buffers match \n");
	// }
	ret = client_disconnect_and_clean();
	if (ret) {
		rdma_error("Failed to cleanly disconnect and clean up resources \n");
	}

    return 0;
}

static int setup_connection(struct sockaddr_in *addr) {
    
    int ret;

    //추후 rdma_getaddrinfo 함수 추가할 예정 (destination address 검색)

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
    ret = rdma_resolve_addr(id, NULL, (struct sockaddr *)&addr, 2000);
    if (ret) {
        perror("rdma_resolve_addr");
        exit(EXIT_FAILURE);
    }

    /* CM (Connection Manager) event channel에서 RDMA_CM_EVENT_ADDR_RESOLVED event 수신*/
    //RDMA_CM_EVENT_ADDR_RESOLVED 이벤트 대기
    printf("RDMA_CM_EVENT_ADDR_RESOLVED event...\n")
    ret = rdma_get_cm_event(ec, &event);
    if (ret) {
        perror("rdma_get_cm_event");
        exit(EXIT_FAILURE);
    }
    
    /* ack the event */
    // 클라이언트가 서버의 IP 주소를 기반으로 RDMA 주소를 성공적으로 얻음
    printf("Acknowledge the CM event(address resolved)...\n")
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
    printf("Resolving routing...\n")
    ret = rdma_resolve_route(id, 2000);
    if (ret) {
        perror("rdma_resolve_route");
        exit(EXIT_FAILURE);
    }

    /* CM (Connection Manager) event channel에서 RDMA_CM_EVENT_ROUTE_RESOLVED event 수신*/
    //RDMA_CM_EVENT_ROUTE_RESOLVED 이벤트 대기
    printf("RDMA_CM_EVENT_ROUTE_RESOLVED event...\n")
    ret = rdma_get_cm_event(ec, &event);
    if (ret) {
        perror("rdma_get_cm_event");
        exit(EXIT_FAILURE);
    }
    
    /* ack the event */
    // 경로 해결이 완료되었음
    printf("Acknowledge the CM event(routing resolved)...\n\n")
    ret = rdma_ack_cm_event(event);
    if (ret) {
        perror("rdma_ack_cm_event");
        exit(EXIT_FAILURE);
    }

    return 0;
}

static int pre_post_recv_buffer()
{
	int ret = 0;

    // memory region을 등록하고 그 포인터를 server_meta_mr 에 저장
    ctx.qp = id->qp;
	server_meta_mr = rdma_buffer(&ctx);
    id->context = &ctx;

	if(!server_meta_mr){
		rdma_error("Failed to setup the server metadata mr , -ENOMEM\n");
		return -ENOMEM;
	}

    server_recv_sge.addr = (uintptr_t)ctx->buffer;
	server_recv_sge.length = BUFFER_SIZE;
	server_recv_sge.lkey = ctx->mr->lkey;

    memset(&server_recv_wr, 0, sizeof(server_recv_wr));
    server_recv_wr.wr_id = (uintptr_t)ctx;
	server_recv_wr.next = NULL;
    server_recv_wr.sg_list = &server_recv_sge;
	server_recv_wr.num_sge = 1;

	ret = ibv_post_recv(ctx->qp, &server_recv_wr, &bad_server_recv_wr)
	if (ret) {
		rdma_error("Failed to pre-post the receive buffer, errno: %d \n", ret);
		return ret;
	}
	
	return 0;
}
//QP 실험해보고 삭제하던가
static int connect_server() {
    struct rdma_conn_param conn_param;

    memset(&conn_param, 0, sizeof(conn_param);)
    conn_param.initiator_depth = 3; //The maximum number of outstanding RDMA read and atomic operations
    conn_param.responder_resources = 3;
    conn_param.retry_count = 3; // if fail, then how many times to retry

    printf("Connecting...\n");
    ret = rdma_connect(id, &conn_param);
    if (ret) {
        perror("rdma_connect");
        exit(EXIT_FAILURE);
    }
    printf("Connection initiated.\n");

    /* CM (Connection Manager) event channel에서 RDMA_CM_EVENT_ESTABLISHED event 수신*/
    //RDMA_CM_EVENT_ESTABLISHED 이벤트 대기
    printf("RDMA_CM_EVENT_ESTABLISHED event...\n")
    ret = rdma_get_cm_event(ec, &event);
    if (ret) {
        perror("rdma_get_cm_event");
        exit(EXIT_FAILURE);
    }
    printf("Connection established.\n");
    
    /* ack the event */
    printf("Acknowledge the CM event...\n\n")
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

    return 0;
}


//클라이언트 메타데이터(버퍼주소, 길이, 스택정보)를 서버와 교환
static int exchange_metadata()
{
    struct ibv_wc *wc;
    int ret = 0;
    client_src_mr = rdma_buffer(&ctx);
    if(!client_src_mr){
		rdma_error("Failed to register the first buffer, ret = %d \n", ret);
		return ret;
	}

	/* we prepare metadata for the first buffer */
	client_meta_attr.address = (uint64_t) client_src_mr->addr; 
	client_meta_attr.length = client_src_mr->length; 
	client_meta_attr.stag.local_stag = client_src_mr->lkey;

	/* now we register the metadata memory */
	client_meta_attr = rdma_buffer(&ctx);
	if(!client_meta_attr) {
		rdma_error("Failed to register the client metadata buffer, ret = %d \n", ret);
		return ret;
	}
	/* now we fill up SGE */
	client_send_sge.addr = (uint64_t) client_meta_attr->addr;
	client_send_sge.length = (uint32_t) client_meta_attr->length;
	client_send_sge.lkey = client_meta_attr->lkey;

	/* now we link to the send work request */
	memset(&client_send_wr, 0, sizeof(client_send_wr));
	client_send_wr.sg_list = &client_send_sge;
	client_send_wr.num_sge = 1;
	client_send_wr.opcode = IBV_WR_SEND;
	client_send_wr.send_flags = IBV_SEND_SIGNALED;

	/* Now we post it */
	ret = ibv_post_send(ctx->qp, &client_send_wr, &bad_client_send_wr);
	if (ret) {
		rdma_error("Failed to send client metadata, errno: %d \n", -errno);
		return -errno;
	}

	ret = process_work_completion_events(ctx.comp_channel, wc, 2);
	if(ret != 2) {
		rdma_error("We failed to get 2 work completions , ret = %d \n", ret);
		return ret;
	}
	
	show_rdma_buffer_attr(&server_metad_attr);

	return 0;
}

//실제로 클라이언트가 서버의 메모리로 데이터를 쓰고, 서버의 메모리에서 데이터를 읽어오는 작업을 수행
//클라이언트는 자신의 메모리(client_dst_mr)를 등록하고, RDMA 쓰기(IBV_WR_RDMA_WRITE)를 통해 클라이언트의 데이터를 서버의 메모리에 쓴다
static int client_remote_memory_ops() 
{
	struct ibv_wc wc;
	int ret = -1;
    
	client_dst_mr = rdma_buffer(&ctx);
	if (!client_dst_mr) {
		rdma_error("We failed to create the destination buffer, -ENOMEM\n");
		return -ENOMEM;
	}

	/* Step 1: is to copy the local buffer into the remote buffer. We will 
	 * reuse the previous variables. */
	client_send_sge.addr = (uint64_t) client_src_mr->addr;
	client_send_sge.length = (uint32_t) client_src_mr->length;
	client_send_sge.lkey = client_src_mr->lkey;

	/* now we link to the send work request */
	memset(&client_send_wr, 0, sizeof(client_send_wr));
	client_send_wr.sg_list = &client_send_sge;
	client_send_wr.num_sge = 1;
	client_send_wr.opcode = IBV_WR_RDMA_WRITE;
	client_send_wr.send_flags = IBV_SEND_SIGNALED;

	/* we have to tell server side info for RDMA */
	client_send_wr.wr.rdma.rkey = server_meta_attr.stag.remote_stag;
	client_send_wr.wr.rdma.remote_addr = server_meta_attr.address;

	/* Now we post it */
	ret = ibv_post_send(ctx->qp, &client_send_wr,&bad_client_send_wr);
	if (ret) {
		rdma_error("Failed to write client src buffer, errno: %d \n", -errno);
		return -errno;
	}

	/* at this point we are expecting 1 work completion for the write */
	ret = process_work_completion_events(ctx.comp_channel, &wc, 1);
	if(ret != 1) {
		rdma_error("We failed to get 1 work completions , ret = %d \n",ret);
		return ret;
	}
    
	/* Now we prepare a READ using same variables but for destination */
	client_send_sge.addr = (uint64_t) client_dst_mr->addr;
	client_send_sge.length = (uint32_t) client_dst_mr->length;
	client_send_sge.lkey = client_dst_mr->lkey;

	/* now we link to the send work request */
	bzero(&client_send_wr, sizeof(client_send_wr));
	client_send_wr.sg_list = &client_send_sge;
	client_send_wr.num_sge = 1;
	client_send_wr.opcode = IBV_WR_RDMA_READ;
	client_send_wr.send_flags = IBV_SEND_SIGNALED;

	/* we have to tell server side info for RDMA */
	client_send_wr.wr.rdma.rkey = server_meta_attr.stag.remote_stag;
	client_send_wr.wr.rdma.remote_addr = server_meta_attr.address;

	/* Now we post it */
	ret = ibv_post_send(ctx.comp_channel, &client_send_wr,&bad_client_send_wr);
	if (ret) {
		rdma_error("Failed to read client dst buffer from the master, errno: %d \n", -errno);
		return -errno;
	}

	/* at this point we are expecting 1 work completion for the write */
	ret = process_work_completion_events(ctx.comp_channel, &wc, 1);
	if(ret != 1) {
		rdma_error("We failed to get 1 work completions , ret = %d \n",ret);
		return ret;
	}
	
	return 0;
}

static int client_disconnect_and_clean()
{
	/* active disconnect from the client side */
	rdma_disconnect(id);
	rdma_destroy_qp(id);
	rdma_destroy_id(id);

    process_rdma_cm_event(ec, RDMA_CM_EVENT_DISCONNECTED, &event);
	rdma_ack_cm_event(event);
	
	/* Destroy CQ */
	ibv_destroy_cq(ctx.cq);
	
	/* Destroy completion channel */
	ibv_destroy_comp_channel(ctx.comp_channel);
	
	/* Destroy memory buffers */
    ibv_dereg_mr(server_meta_mr.mr);
    ibv_dereg_mr(client_meta_mr);
    ibv_dereg_mr(client_src_mr);
    ibv_dereg_mr(client_dst_mr);
	

	/* We free the buffers */
	// free(src);
	// free(dst);
    free(ctx.buffer);

	/* Destroy protection domain */
	ibv_dealloc_pd(ctx.pd);
	rdma_destroy_event_channel(ctx.comp_channel);

	printf("Client resource clean up is complete \n");
	return 0;
}

   



//     void setup_connection(const char *server_ip) {
//     char command[256];
//     while (1) {
//         printf("Enter command (put/get): ");
//         if (fgets(command, sizeof(command), stdin) == NULL) {
//             perror("fgets");
//             exit(EXIT_FAILURE);
//         }
//         command[strcspn(command, "\n")] = '\0'; // Newline 제거

//         struct message *msg = (struct message *)ctx.buffer;
//         if (strncmp(command, "put", 3) == 0) {
//             msg->type = MSG_PUT;
//             printf("Enter key: ");
//             if (fgets(msg->kv.key, KEY_VALUE_SIZE, stdin) == NULL) {
//                 perror("fgets");
//                 exit(EXIT_FAILURE);
//             }
//             msg->kv.key[strcspn(msg->kv.key, "\n")] = '\0'; // Newline 제거
//             printf("Enter value: ");
//             if (fgets(msg->kv.value, KEY_VALUE_SIZE, stdin) == NULL) {
//                 perror("fgets");
//                 exit(EXIT_FAILURE);
//             }
//             msg->kv.value[strcspn(msg->kv.value, "\n")] = '\0'; // Newline 제거
//             msg->addr = 0; // 주소는 서버가 응답할 때 설정됨

//             struct ibv_sge sge;
//             struct ibv_send_wr wr, *bad_wr = NULL;

//             sge.addr = (uintptr_t)ctx.buffer;
//             sge.length = sizeof(struct message);
//             sge.lkey = ctx.mr->lkey;

//             memset(&wr, 0, sizeof(wr));
//             wr.wr_id = (uintptr_t)&ctx;
//             wr.opcode = IBV_WR_SEND;
//             wr.sg_list = &sge;
//             wr.num_sge = 1;

//             if (ibv_post_send(ctx.qp, &wr, &bad_wr)) {
//                 perror("ibv_post_send");
//                 exit(EXIT_FAILURE);
//             }

//             post_receives(&ctx);

//         } else if (strncmp(command, "get", 3) == 0) {
//             msg->type = MSG_GET;
//             printf("Enter key: ");
//             if (fgets(msg->kv.key, KEY_VALUE_SIZE, stdin) == NULL) {
//                 perror("fgets");
//                 exit(EXIT_FAILURE);
//             }
//             msg->kv.key[strcspn(msg->kv.key, "\n")] = '\0'; // Newline 제거
//             msg->addr = 0; // 주소는 서버가 응답할 때 설정됨

//             struct ibv_sge sge;
//             struct ibv_send_wr wr, *bad_wr = NULL;

//             sge.addr = (uintptr_t)ctx.buffer;
//             sge.length = sizeof(struct message);
//             sge.lkey = ctx.mr->lkey;

//             memset(&wr, 0, sizeof(wr));
//             wr.wr_id = (uintptr_t)&ctx;
//             wr.opcode = IBV_WR_SEND;
//             wr.sg_list = &sge;
//             wr.num_sge = 1;

//             if (ibv_post_send(ctx.qp, &wr, &bad_wr)) {
//                 perror("ibv_post_send");
//                 exit(EXIT_FAILURE);
//             }

//             post_receives(&ctx);
//         }
//     }

// }

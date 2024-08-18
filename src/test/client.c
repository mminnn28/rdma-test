#include "common.h"

/* RDMA 관련 리소스 */
static struct rdma_context ctx;
static struct rdma_cm_id *id = NULL;
static struct rdma_event_channel *ec = NULL;
static struct rdma_cm_event *event = NULL;
static struct ibv_qp_init_attr qp_attr;

static struct pdata server_pdata;

struct ibv_recv_wr recv_wr, *bad_recv_wr = NULL;
struct ibv_send_wr send_wr, *bad_send_wr = NULL;
struct ibv_sge send_sge, recv_sge;
struct ibv_wc wc;
static char *send_buffer = NULL, *recv_buffer = NULL;

static void setup_connection(const char *server_ip);
static void pre_post_recv_buffer();
static void connect_server();
int on_connect();
void post_send_message();
void receive_response();
void cleanup(struct rdma_cm_id *id);
int prepare_send_notify_after_rdma_write();

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <server-ip>\n", argv[0]);
        return EXIT_FAILURE;
    }

    setup_connection(argv[1]);
    pre_post_recv_buffer();
    connect_server();
    on_connect();

    return 0;
}

static void setup_connection(const char *server_ip) {
    int ret;
    struct sockaddr_in addr;


    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SERVER_PORT);
    addr.sin_addr.s_addr = inet_addr(server_ip);

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

    ret = rdma_resolve_addr(id, NULL, (struct sockaddr *)&addr, TIMEOUT_IN_MS);
    if (ret) {
        perror("rdma_resolve_addr");
        exit(EXIT_FAILURE);
    }

    ret = rdma_get_cm_event(ec, &event);
    if (ret) {
        perror("rdma_get_cm_event");
        exit(EXIT_FAILURE);
    }

    ret = rdma_ack_cm_event(event);
    if (ret) {
        perror("rdma_ack_cm_event");
        exit(EXIT_FAILURE);
    }

    build_context(&ctx, id);
    build_qp_attr(&qp_attr, &ctx);

    printf("Creating QP...\n");
    ret = rdma_create_qp(id, ctx.pd, &qp_attr);
    if (ret) {
        perror("rdma_create_qp");
        exit(EXIT_FAILURE);
    }
    ctx.qp = id->qp;

    ret = rdma_resolve_route(id, TIMEOUT_IN_MS);
    if (ret) {
        perror("rdma_resolve_route");
        exit(EXIT_FAILURE);
    }

    ret = rdma_get_cm_event(ec, &event);
    if (ret) {
        perror("rdma_get_cm_event");
        exit(EXIT_FAILURE);
    }

    ret = rdma_ack_cm_event(event);
    if (ret) {
        perror("rdma_ack_cm_event");
        exit(EXIT_FAILURE);
    }
}
static void pre_post_recv_buffer() {
    int ret = -1;
    recv_buffer = (char *)malloc(BUFFER_SIZE);
    if (!recv_buffer) {
        perror("Failed to allocate memory for receive buffer");
        exit(EXIT_FAILURE);
    }

    ctx.recv_mr = ibv_reg_mr(ctx.pd, recv_buffer, BUFFER_SIZE, IBV_ACCESS_LOCAL_WRITE);
    if (!ctx.recv_mr) {
        perror("Failed to register receive memory region");
        exit(EXIT_FAILURE);
    }

    recv_sge.addr = (uint64_t)recv_buffer;
    recv_sge.length = BUFFER_SIZE;
    recv_sge.lkey = ctx.recv_mr->lkey;

    memset(&recv_wr, 0, sizeof(recv_wr));
    recv_wr.wr_id = 0;
    recv_wr.sg_list = &recv_sge;
    recv_wr.num_sge = 1;

    ret = ibv_post_recv(id->qp, &recv_wr, &bad_recv_wr);
    if (ret) {
        perror("Failed to post receive work request");
        exit(EXIT_FAILURE);
    }

    printf("Memory registered at address %p with LKey %u\n", recv_buffer, ctx.recv_mr->lkey);
}

static void connect_server() {
    int ret = -1;
    struct rdma_conn_param conn_param;

    server_pdata.buf_va = (uintptr_t)recv_buffer;
    server_pdata.buf_rkey = htonl(ctx.recv_mr->rkey);

    memset(&conn_param, 0, sizeof(conn_param));
    conn_param.initiator_depth = 3;
    conn_param.responder_resources = 3;
    conn_param.retry_count = 3;

    printf("Connecting...\n");
    ret = rdma_connect(id, &conn_param);
    if (ret) {
        perror("Failed to connect to remote host");
        exit(EXIT_FAILURE);
    }

    ret = rdma_get_cm_event(ec, &event);
    if (ret) {
        perror("Failed to get cm event");
        exit(EXIT_FAILURE);
    }
    printf("Connection established.\n");

    memcpy(&server_pdata,event->param.conn.private_data,sizeof(server_pdata));

    ret = rdma_ack_cm_event(event);
    if (ret) {
        perror("Failed to acknowledge cm event");
        exit(EXIT_FAILURE);
    }
    printf("The client is connected successfully. \n\n");
}

int on_connect() {
    char command[256];
    struct message msg_send;

    // 메모리 등록
    send_buffer = (char *)malloc(BUFFER_SIZE);
    if (!send_buffer) {
        perror("Failed to allocate memory for send buffer");
        exit(EXIT_FAILURE);
    }

    ctx.send_mr = ibv_reg_mr(ctx.pd, send_buffer, BUFFER_SIZE, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE);
    if (!ctx.send_mr) {
        fprintf(stderr, "Failed to register client metadata buffer.\n");
        exit(EXIT_FAILURE);
    }

    printf("Received Server Memory at address %p with LKey %u\n\n",(void *)server_pdata.buf_va, server_pdata.buf_rkey);

    while (1) {
        printf("Enter command ( put k v / get k ): ");
        if (fgets(command, sizeof(command), stdin) == NULL) {
            fprintf(stderr, "Error reading command\n");
            continue;
        }
        command[strcspn(command, "\n")] = '\0'; // 개행 문자 제거

        char *cmd = strtok(command, " ");

        if (strcmp(cmd, "put") == 0) {
            char *key = strtok(NULL, " ");
            char *value = strtok(NULL, "");

            strncpy(msg_send.kv.key, key, KEY_VALUE_SIZE - 1);
            msg_send.kv.key[KEY_VALUE_SIZE - 1] = '\0';

            strncpy(msg_send.kv.value, value, KEY_VALUE_SIZE - 1);
            msg_send.kv.value[KEY_VALUE_SIZE - 1] = '\0';
            msg_send.type = MSG_PUT;

            printf("msg key: %s, msg value: %s\n", msg_send.kv.key, msg_send.kv.value);

        } else if (strcmp(cmd, "get") == 0) {

            char *key = strtok(NULL, "");

            strncpy(msg_send.kv.key, key, KEY_VALUE_SIZE - 1);
            msg_send.kv.key[KEY_VALUE_SIZE - 1] = '\0';
            msg_send.kv.value[0] = '\0'; 
            msg_send.type = MSG_GET;

            printf("msg key: %s, msg value: %s\n", msg_send.kv.key, msg_send.kv.value);
        } else {
            printf("Invalid command\n");
            continue;
        }

        memcpy(send_buffer, &msg_send, sizeof(struct message));
        struct message *msg_in_buffer = (struct message *)send_buffer;
        printf("\nsend_buffer content:\n");
        printf("Type: %d\n", msg_in_buffer->type);
        printf("Key: %s\n", msg_in_buffer->kv.key);
        printf("Value: %s\n\n", msg_in_buffer->kv.value);


        // 메시지 전송
        post_send_message();

        // 서버로부터 응답 수신
        receive_response();
    }

    cleanup(id);
    return 0;
}
void post_send_message() {
    printf("post_send_message\n");
    send_sge.addr = (uintptr_t)send_buffer;
    send_sge.length = sizeof(send_buffer);
    send_sge.lkey = ctx.send_mr->lkey;

    send_wr.opcode = IBV_WR_RDMA_WRITE;
    send_wr.wr_id = 1;
    send_wr.sg_list = &send_sge;
    send_wr.num_sge = 1;
    send_wr.send_flags = IBV_SEND_SIGNALED;
    send_wr.wr.rdma.rkey = ntohl(server_pdata.buf_rkey); // 서버 권한 키
	send_wr.wr.rdma.remote_addr = bswap_64(server_pdata.buf_va); // 서버 메모리 주소

    //printf("Received Server Memory at address %p with LKey %u\n\n", send_wr.wr.rdma.rkey, send_wr.wr.rdma.remote_addr);
    //printf("Received Server Memory at address %p with LKey %u\n\n",(void *)server_pdata.buf_va, server_pdata.buf_rkey);
   
    if (ibv_post_send(id->qp, &send_wr, &bad_send_wr)) {
        fprintf(stderr, "Failed to post send work request: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    struct ibv_wc wc;
    int num_wc = ibv_poll_cq(ctx.qp->send_cq, 1, &wc);
    if (num_wc < 0) {
        fprintf(stderr, "Error polling CQ: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    } else if (num_wc == 0) {
        fprintf(stderr, "No completion found in CQ\n");
        exit(EXIT_FAILURE);
    }

    if (wc.status != IBV_WC_SUCCESS) {
        fprintf(stderr, "Send failed with status %s\n", ibv_wc_status_str(wc.status));
        exit(EXIT_FAILURE);
    }

    // if (ibv_poll_cq(ctx.qp->send_cq, 1, &wc) > 0) {
    //     if (wc.status != IBV_WC_SUCCESS) {
    //         fprintf(stderr, "Send failed with status %s\n", ibv_wc_status_str(wc.status));
    //         exit(EXIT_FAILURE);
    //     }
    // }
}

void receive_response() {
    printf("receive_response\n");
    // void *cq_context;
    // int end_loop = 0;
    // while(!end_loop) {
    //     if (ibv_get_cq_event(ctx.comp_channel,&ctx.evt_cq,&cq_context))
	// 		return 1;
    //     printf("ibv_get_cq_event\n");

	// 	if (ibv_req_notify_cq(ctx.cq,0))
	// 		return 1;
	// 	printf("ibv_req_notify_cq\n");
        
    //     if (ibv_poll_cq(ctx.qp, 1, &wc) != 1)
	// 		return 1;
	// 	printf("ibv_poll_cq\n");
        
    //     if (wc.status != IBV_WC_SUCCESS)
	// 		return 1;

    //     printf("switch~~~\n");    
	// 	switch (wc.wr_id) {
	// 	    case 0:
	// 		    printf("server ans : %d\n", ntohl(recv_buffer[0]));
	// 		    end_loop = 1;
	// 		    break;
	// 	    case 1:
	// 		    if (prepare_send_notify_after_rdma_write())
	// 			    return 1;
	// 		    break;
	// 	    default:
	// 		    end_loop = 1;
	// 		    break;
	// 	}
    // }
    // ibv_ack_cq_events(ctx.cq,2);
    int ret;

    recv_wr.sg_list = &recv_sge;
    recv_wr.num_sge = 1;

    ret = ibv_post_recv(ctx.qp, &recv_wr, &bad_recv_wr);
    if (ret) {
        fprintf(stderr, "Failed to post receive work request: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    struct ibv_wc wc;
    int num_wc = ibv_poll_cq(ctx.qp->recv_cq, 1, &wc);
    if (num_wc < 0) {
        fprintf(stderr, "Error polling CQ: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    } else if (num_wc == 0) {
        fprintf(stderr, "No completion found in CQ\n");
        exit(EXIT_FAILURE);
    }

    if (wc.status != IBV_WC_SUCCESS) {
        fprintf(stderr, "Receive failed with status %s\n", ibv_wc_status_str(wc.status));
        exit(EXIT_FAILURE);
    }

    struct message *response = (struct message *)recv_buffer;
    if (response->type == MSG_GET) {
        printf("Received response: Key: %s, Value: %s\n", response->kv.key, response->kv.value);
    } else {
        printf("Response: %s\n", response->kv.value);
    }
}

int prepare_send_notify_after_rdma_write()
{
	struct ibv_sge					sge; 
   	struct ibv_send_wr				send_wr = { }; 
   	struct ibv_send_wr 				*bad_send_wr; 

	uint8_t *buf = calloc(1, sizeof(uint8_t));
	struct ibv_mr *mr = ibv_reg_mr(ctx.pd, buf, sizeof(uint8_t), IBV_ACCESS_LOCAL_WRITE); 
	if (!mr) 
		return 1;
	
	sge.addr = (uintptr_t)buf; 
    sge.length = sizeof(uint8_t); 
    sge.lkey = mr->lkey;
    
	memset(&send_wr, 0, sizeof(send_wr));
	send_wr.wr_id = 2;
    send_wr.opcode = IBV_WR_SEND;
    send_wr.sg_list = &sge;
    send_wr.num_sge = 1;

    if (ibv_post_send(id->qp,&send_wr,&bad_send_wr)) 
        return 1;

	return 0;
}

void cleanup(struct rdma_cm_id *id) {

    if (send_buffer) {
        free(send_buffer);
        send_buffer = NULL;
    }

    if (recv_buffer) {
        free(recv_buffer);
        recv_buffer = NULL;
    }

    if (ctx.recv_mr) {
        ibv_dereg_mr(ctx.recv_mr);
        ctx.recv_mr = NULL;
    }

    if (ctx.send_mr) {
        ibv_dereg_mr(ctx.send_mr);
        ctx.send_mr = NULL;
    }

    if (ctx.qp) {
        rdma_destroy_qp(id);
        ctx.qp = NULL;
    }

    if (ctx.cq) {
        ibv_destroy_cq(ctx.cq);
        ctx.cq = NULL;
    }

    if (ctx.comp_channel) {
        ibv_destroy_comp_channel(ctx.comp_channel);
        ctx.comp_channel = NULL;
    }

    if (ctx.pd) {
        ibv_dealloc_pd(ctx.pd);
        ctx.pd = NULL;
    }

    if (id) {
        rdma_destroy_id(id);
        id = NULL;
    }

    if (ec) {
        rdma_destroy_event_channel(ec);
        ec = NULL;
    }
}

/*
./client 10.10.1.2
Creating QP...
Memory registered at address 0x563dc8ac78c0 with LKey 270592
Connecting...
Connection established.
The client is connected successfully. 

Received Server Memory at address 0x55ae9dbadbf0 with LKey 1484391424

Enter command ( put k v / get k ): put a b
msg key: a, msg value: b

send_buffer content:
Type: 0
Key: a
Value: b

post_send_message
No completion found in CQ

*/

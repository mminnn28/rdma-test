#include "common.h"


/* RDMA related resources */
static struct rdma_context ctx;
static struct rdma_cm_id *listen_id; 
static struct rdma_cm_id *id = NULL;
static struct rdma_event_channel *ec = NULL;
static struct rdma_cm_event *event = NULL;
static struct ibv_qp_init_attr qp_attr;
static struct pdata rep_pdata;

struct ibv_recv_wr recv_wr, *bad_recv_wr = NULL;
struct ibv_send_wr send_wr, *bad_send_wr = NULL;
struct ibv_sge recv_sge, send_sge;
struct ibv_wc wc;
static char *send_buffer = NULL, *recv_buffer = NULL;
static void *cq_context;

static void setup_connection();
static int handle_event();
static void on_connect();
static int pre_post_recv_buffer();
static int check_notify_before_using_rdma_write();
static void process_message();
void cleanup(struct rdma_cm_id *id);

int main() {
    
    setup_connection();
    return EXIT_SUCCESS;
}

static void setup_connection() {
    struct sockaddr_in addr;

    /* Bind address to RDMA ID */
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SERVER_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    /* Create RDMA event channel */
    ec = rdma_create_event_channel();
    if (!ec) {
        perror("rdma_create_event_channel");
        exit(EXIT_FAILURE);
    }

    /* Create RDMA ID */
    if (rdma_create_id(ec, &listen_id, NULL, RDMA_PS_TCP)) {
        perror("rdma_create_id");
        exit(EXIT_FAILURE);
    }

    if (rdma_bind_addr(listen_id, (struct sockaddr *)&addr)) {
        perror("rdma_bind_addr");
        exit(EXIT_FAILURE);
    }

    /* Listen for incoming connections */
    if (rdma_listen(listen_id, 1)) {
        perror("rdma_listen");
        exit(EXIT_FAILURE);
    }

    printf("Listening for incoming connections...\n\n");

    while (1) {
        /* Wait for an event */
        if (rdma_get_cm_event(ec, &event)) {
            perror("rdma_get_cm_event");
            exit(EXIT_FAILURE);
        }

        id = event->id;

        if (handle_event()) {
            break;
        }

        /* Acknowledge the event */
        if (rdma_ack_cm_event(event)) {
            perror("rdma_ack_cm_event");
            exit(EXIT_FAILURE);
        }
    }
}
static int handle_event() {

    printf("Event type: %s\n", rdma_event_str(event->event));

    if (event->event == RDMA_CM_EVENT_CONNECT_REQUEST) {
        printf("Connection request received.\n\n");
        on_connect();
    } else if(event->event == RDMA_CM_EVENT_ESTABLISHED) {
		printf("connect established.\n\n");
        id = event->id;
        process_message();
    } else if (event->event == RDMA_CM_EVENT_DISCONNECTED) {
        printf("Disconnected from client.\n");
        cleanup(id);
        exit(EXIT_FAILURE);
    }

    return 0;
}

static void on_connect() {
    struct rdma_conn_param conn_param;

    /* Allocate resources */
    build_context(&ctx, id);
    build_qp_attr(&qp_attr, &ctx);

    printf("Creating QP...\n");
    if (rdma_create_qp(id, ctx.pd, &qp_attr)) {
        perror("rdma_create_qp");
        exit(EXIT_FAILURE);
    }
    printf("Queue Pair created: %p\n\n", (void*)id->qp);

    pre_post_recv_buffer();

    rep_pdata.buf_va = (uintptr_t)recv_buffer;
    rep_pdata.buf_rkey = htonl(ctx.recv_mr->rkey);

    memset(&conn_param, 0, sizeof(conn_param));
	conn_param.initiator_depth = 3;
    conn_param.responder_resources = 3;
    conn_param.retry_count = 3;
    conn_param.private_data = &rep_pdata; 
    conn_param.private_data_len = sizeof(rep_pdata);

    if (rdma_accept(id, &conn_param)) {
        perror("rdma_accept");
        exit(EXIT_FAILURE);
    }
    printf("Connection accepted.\n\n");
    
    memcpy(&rep_pdata,event->param.conn.private_data,sizeof(rep_pdata));
    printf("Received client Memory at address %p with LKey %u\n", (void *)rep_pdata.buf_va, ntohl(rep_pdata.buf_rkey));
}

static int pre_post_recv_buffer() {
    
    recv_buffer = (char *)malloc(BUFFER_SIZE);
    if (!recv_buffer) {
        perror("Failed to allocate memory for receive buffer");
        exit(EXIT_FAILURE);
    }

    ctx.recv_mr = ibv_reg_mr(ctx.pd, recv_buffer, BUFFER_SIZE, IBV_ACCESS_LOCAL_WRITE); 
	if (!ctx.recv_mr) 
		exit(EXIT_FAILURE);

    recv_sge.addr = (uintptr_t)recv_buffer;
    recv_sge.length = BUFFER_SIZE;
    recv_sge.lkey = ctx.recv_mr->lkey;

    memset(&recv_wr, 0, sizeof(recv_wr));
    recv_wr.wr_id = 0;
    recv_wr.sg_list = &recv_sge;
    recv_wr.num_sge = 1;

    if (ibv_post_recv(id->qp, &recv_wr, &bad_recv_wr)) {
        perror("Failed to post receive work request");
        return 1;
    }
    printf("Memory registered at address %p with LKey %u\n\n", recv_buffer, ctx.recv_mr->lkey);

    return 0;
}

static int check_notify_before_using_rdma_write()
{
    struct ibv_wc wc;
    struct ibv_cq *evt_cq;
    void *cq_context;

    // CQ 이벤트를 가져와야 함
	if (ibv_get_cq_event(ctx.comp_channel,&evt_cq,&cq_context))
		return 1;
    
    // CQ에 대한 알림 요청
    if (ibv_req_notify_cq(ctx.cq,0))
		return 1;
    
   // CQ에서 작업 완료를 폴링
    int num_wc = ibv_poll_cq(ctx.cq, 1, &wc);
    if (num_wc < 0) {
        perror("ibv_poll_cq failed");
        return 1;
    } else if (num_wc == 0) {
        printf("No completions found\n");
        return 1;
    }
    
    // 작업 완료 상태 확인
    // if (wc.status != IBV_WC_SUCCESS)
	// 	return 1;


    printf("check_notify_before_using_rdma_write ended\n");

    return 0;
}


static void process_message() {

    //memcpy(&rep_pdata,event->param.conn.private_data,sizeof(rep_pdata));

    /* we need to check IBV_WR_RDMA_WRITE is done, so we post_recv at first */
    // if (pre_post_recv_buffer())
    //     exit(EXIT_FAILURE);
    /* we need to check we already receive RDMA_WRITE done notification */
    if (check_notify_before_using_rdma_write())
        exit(EXIT_FAILURE);

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
    
    struct message *msg = (struct message *)recv_buffer;

     printf("Received message - Type: %d, Key: %s, Value: %s\n",
           msg->type,
           msg->kv.key,
           msg->kv.value);

   

    /* Handle the received message */
    if (msg->type == MSG_PUT) {
        // Store the key-value pair
        // Here we just print it for simplicity
        printf("PUT operation: Key: %s, Value: %s\n", msg->kv.key, msg->kv.value);
        snprintf(send_buffer, BUFFER_SIZE, "PUT %s %s", msg->kv.key, msg->kv.value);
    } else if (msg->type == MSG_GET) {
        // Retrieve the value for the given key
        // Here we just provide a dummy value for simplicity
        snprintf(send_buffer, BUFFER_SIZE, "GET %s", msg->kv.key);
        printf("GET operation: Key: %s, Value: dummy_value\n", msg->kv.key);
    }

    /* register post send, here we use IBV_WR_SEND */
    send_sge.addr = (uintptr_t)send_buffer;
    send_sge.length = BUFFER_SIZE;
    send_sge.lkey = ctx.send_mr->lkey;

    send_wr.opcode = IBV_WR_SEND;
    send_wr.send_flags = IBV_SEND_SIGNALED;
    send_wr.sg_list = &send_sge;
    send_wr.num_sge = 1;
    send_wr.wr_id = 1;

    send_wr.wr.rdma.rkey = ntohl(rep_pdata.buf_rkey); // 클라이언트 권한 키
	send_wr.wr.rdma.remote_addr = bswap_64(rep_pdata.buf_va); // 클라이언트 메모리 주소

    if (ibv_post_send(id->qp, &send_sge, &bad_send_wr)) {
        perror("ibv_post_recv");
        exit(EXIT_FAILURE);
    }

    
    if (ibv_get_cq_event(ctx.comp_channel,&ctx.evt_cq,&cq_context))
		exit(EXIT_FAILURE);
	ibv_ack_cq_events(ctx.cq,1);
	if (ibv_req_notify_cq(ctx.cq,0))
		exit(EXIT_FAILURE);
	if (ibv_poll_cq(ctx.cq,1,&wc) != 1)
		exit(EXIT_FAILURE);
	if (wc.status != IBV_WC_SUCCESS)
		exit(EXIT_FAILURE);
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


/**
./server
Listening for incoming connections...

Event type: RDMA_CM_EVENT_CONNECT_REQUEST
Connection request received.

Creating QP...
Queue Pair created: 0x5647122260b8

Memory registered at address 0x5647122269e0 with LKey 127143

Connection accepted.

Received client Memory at address 0x55f6de1618c0 with LKey 143633

Event type: RDMA_CM_EVENT_ESTABLISHED
connect established.

check_notify_before_using_rdma_write ended
Received message - Type: -694706976, Key: , Value: 
Segmentation fault


 */


/**
 * ./server
Listening for incoming connections...
Event type: RDMA_CM_EVENT_CONNECT_REQUEST
Connection request received.

Creating QP...
Queue Pair created: 0x55ae9dbad2c8

Memory registered at address 0x55ae9dbadbf0 with LKey 293464

Memory registered at address (nil) with LKey 0

Connection accepted.

Event type: RDMA_CM_EVENT_ESTABLISHED
connect established.

check_notify_before_using_rdma_write ended
Received client Memory at address (nil) with LKey 0

Received message - Type: -546312992, Key: , Value: 
Segmentation fault

 */

#include "common.h"

/* RDMA related resources */
static struct rdma_context ctx = {0};
static struct rdma_cm_id *id = NULL;
static struct rdma_event_channel *ec = NULL;
static struct rdma_cm_event *event = NULL;
static struct ibv_qp_init_attr qp_attr;

struct ibv_mr *server_mr = NULL;
static struct rdma_buffer_attr server_attr;
struct ibv_recv_wr recv_wr, *bad_recv_wr = NULL;
struct ibv_send_wr send_wr, *bad_send_wr = NULL;
struct ibv_sge recv_sge, send_sge;

static void setup_connection();
static int handle_event();
static void on_connect(struct rdma_cm_id *id);
static void pre_post_recv_buffer();
static void process_message(struct message *msg);
static char *send_buffer = NULL, *recv_buffer = NULL;

int main() {
    
    setup_connection();
    return EXIT_SUCCESS;
}

static void setup_connection() {
    struct sockaddr_in addr;

    /* Create RDMA event channel */
    ec = rdma_create_event_channel();
    if (!ec) {
        perror("rdma_create_event_channel");
        exit(EXIT_FAILURE);
    }

    /* Create RDMA ID */
    if (rdma_create_id(ec, &id, NULL, RDMA_PS_TCP)) {
        perror("rdma_create_id");
        exit(EXIT_FAILURE);
    }

    /* Bind address to RDMA ID */
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SERVER_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (rdma_bind_addr(id, (struct sockaddr *)&addr)) {
        perror("rdma_bind_addr");
        exit(EXIT_FAILURE);
    }

    /* Listen for incoming connections */
    if (rdma_listen(id, 1)) {
        perror("rdma_listen");
        exit(EXIT_FAILURE);
    }

    printf("Listening for incoming connections...\n");

    while (1) {
        /* Wait for an event */
        printf("RDMA_CM_EVENT_CONNECT_REQUEST event...\n");
        if (rdma_get_cm_event(ec, &event)) {
            perror("rdma_get_cm_event");
            cleanup(id);
            if (send_buffer) {
                free(send_buffer);
                send_buffer = NULL;
            }
            if (recv_buffer) {
                free(recv_buffer);
                recv_buffer = NULL;
            }
            exit(EXIT_FAILURE);
        }

        id = event->id;

        if (handle_event()) {
            break;
        }

        /* Acknowledge the event */
        if (rdma_ack_cm_event(event)) {
            perror("rdma_ack_cm_event");
            cleanup(id);
            if (send_buffer) {
                free(send_buffer);
                send_buffer = NULL;
            }
            if (recv_buffer) {
                free(recv_buffer);
                recv_buffer = NULL;
            }
            exit(EXIT_FAILURE);
        }
    }
}
static int handle_event() {

    printf("Event type: %s\n", rdma_event_str(event->event));

    if (event->event == RDMA_CM_EVENT_CONNECT_REQUEST) {
        printf("Connection request received.\n");
        on_connect(event->id);
    } else if(event->event == RDMA_CM_EVENT_ESTABLISHED) {
		printf("connect established.\n");

		// Process messages from RDMA
        struct message *msg = (struct message *)recv_buffer;
        process_message(msg);
	}else if (event->event == RDMA_CM_EVENT_DISCONNECTED) {
        printf("Disconnected from client.\n");
        cleanup(id);
        if (send_buffer) {
            free(send_buffer);
            send_buffer = NULL;
        }
        if (recv_buffer) {
            free(recv_buffer);
            recv_buffer = NULL;
        }
        exit(EXIT_FAILURE);
    }

    return 0;
}

static void on_connect(struct rdma_cm_id *id) {
    struct rdma_conn_param conn_param;

    /* Allocate resources */
    build_context(&ctx, id);
    build_qp_attr(&qp_attr, &ctx);

    printf("Creating QP...\n");
    if (rdma_create_qp(id, ctx.pd, &qp_attr)) {
        perror("rdma_create_qp");
        exit(EXIT_FAILURE);
    }

    /* Register memory and post receive buffer */
    pre_post_recv_buffer();

    /* Accept the connection request */
    memset(&conn_param, 0, sizeof(conn_param));
    conn_param.initiator_depth = 3;
    conn_param.responder_resources = 3;
    conn_param.retry_count = 3;

    if (rdma_accept(id, &conn_param)) {
        perror("rdma_accept");
        exit(EXIT_FAILURE);
    }

    printf("Connection accepted.\n\n");
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
    recv_wr.sg_list = &recv_sge;
    recv_wr.num_sge = 1;

    ret = ibv_post_recv(ctx.qp, &recv_wr, &bad_recv_wr);
    if (ret) {
        perror("Failed to post receive work request");
        exit(EXIT_FAILURE);
    }

    printf("Memory registered at address %p with LKey %u\n", recv_buffer, ctx.recv_mr->lkey);
}

static void process_message(struct message *msg) {

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

    /* Handle the received message */
    if (msg->type == MSG_PUT) {
        // Store the key-value pair
        // Here we just print it for simplicity
        printf("PUT operation: Key: %s, Value: %s\n", msg->kv.key, msg->kv.value);
        snprintf(ctx.buf, BUFFER_SIZE, "PUT %s %s", msg->kv.key, msg->kv.value);
    } else if (msg->type == MSG_GET) {
        // Retrieve the value for the given key
        // Here we just provide a dummy value for simplicity
        snprintf(ctx.buf, BUFFER_SIZE, "GET %s", msg->kv.key);
        printf("GET operation: Key: %s, Value: dummy_value\n", msg->kv.key);
    }

    /* Post receive for next message */
    recv_sge.addr = (uintptr_t)ctx.buf;
    recv_sge.length = BUFFER_SIZE;
    recv_sge.lkey = ctx.recv_mr->lkey;

    recv_wr.wr_id = (uintptr_t)id;
    recv_wr.sg_list = &recv_sge;
    recv_wr.num_sge = 1;
    recv_wr.next = NULL;

    if (ibv_post_recv(id->qp, &recv_wr, &bad_recv_wr)) {
        perror("ibv_post_recv");
        exit(EXIT_FAILURE);
    }
}


/*
./server
Listening for incoming connections...
RDMA_CM_EVENT_CONNECT_REQUEST event...
Event type: RDMA_CM_EVENT_CONNECT_REQUEST
Connection request received.
Creating QP...
ibv_post_recv error.
Memory registered at address 0x556c0f48c9e0 with RKey 43912
Connection accepted.
RDMA_CM_EVENT_CONNECT_REQUEST event...
Event type: RDMA_CM_EVENT_ESTABLISHED
connect established.
RDMA_CM_EVENT_CONNECT_REQUEST event...
Event type: RDMA_CM_EVENT_DISCONNECTED
Disconnected from client.
*/

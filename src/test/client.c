#include "common.h"

/* RDMA 관련 리소스 */
static struct rdma_context ctx = {0};
static struct rdma_cm_id *id = NULL;
static struct rdma_event_channel *ec = NULL;
static struct rdma_cm_event *event = NULL;
static struct ibv_qp_init_attr qp_attr;
static struct rdma_buffer_attr send_attr, recv_attr;
struct ibv_recv_wr recv_wr, *bad_recv_wr = NULL;
struct ibv_send_wr send_wr, *bad_send_wr = NULL;
struct ibv_sge send_sge, recv_sge;
static char *send_buffer = NULL, *recv_buffer = NULL;

static void setup_connection(const char *server_ip);
static void pre_post_recv_buffer();
static void connect_server();
void post_send_message(struct message *msg);
void receive_response();
int on_connect();
static void cleanup(struct rdma_cm_id *id);

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
    recv_wr.sg_list = &recv_sge;
    recv_wr.num_sge = 1;

    ret = ibv_post_recv(ctx.qp, &recv_wr, &bad_recv_wr);
    if (ret) {
        perror("Failed to post receive work request");
        exit(EXIT_FAILURE);
    }

    printf("Memory registered at address %p with LKey %u\n", recv_buffer, ctx.recv_mr->lkey);
}

static void connect_server() {
    int ret = -1;
    struct rdma_conn_param conn_param;

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

    ret = rdma_ack_cm_event(event);
    if (ret) {
        perror("Failed to acknowledge cm event");
        exit(EXIT_FAILURE);
    }
    printf("The client is connected successfully. \n\n");
}

void post_send_message(struct message *msg) {
    struct ibv_sge send_sge = {
        .addr = (uintptr_t)send_buffer,
        .length = sizeof(struct message),
        .lkey = ctx.send_mr->lkey,
    };

    struct ibv_send_wr send_wr = {
        .opcode = IBV_WR_SEND,
        .sg_list = &send_sge,
        .num_sge = 1,
        .send_flags = IBV_SEND_SIGNALED,
    };

    struct ibv_send_wr *bad_send_wr;
    if (ibv_post_send(ctx.qp, &send_wr, &bad_send_wr)) {
        fprintf(stderr, "Failed to post send work request: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    struct ibv_wc wc;
    if (ibv_poll_cq(ctx.qp->send_cq, 1, &wc) > 0) {
        if (wc.status != IBV_WC_SUCCESS) {
            fprintf(stderr, "Send failed with status %s\n", ibv_wc_status_str(wc.status));
            exit(EXIT_FAILURE);
        }
    }
}

void receive_response() {
    struct message *response = (struct message *)recv_buffer;

    struct ibv_recv_wr recv_wr = {
        .sg_list = &recv_sge,
        .num_sge = 1,
    };

    struct ibv_recv_wr *bad_recv_wr;
    if (ibv_post_recv(ctx.qp, &recv_wr, &bad_recv_wr)) {
        fprintf(stderr, "Failed to post receive work request: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    struct ibv_wc wc;
    if (ibv_poll_cq(ctx.qp->recv_cq, 1, &wc) > 0) {
        if (wc.status != IBV_WC_SUCCESS) {
            fprintf(stderr, "Receive failed with status %s\n", ibv_wc_status_str(wc.status));
            exit(EXIT_FAILURE);
        }
        if (response->type == MSG_GET) {
            printf("Received response: Key: %s, Value: %s\n", response->kv.key, response->kv.value);
        } else {
            printf("Response: %s\n", response->kv.value); // 또는 성공 여부
        }
    }
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
        } else if (strcmp(cmd, "get") == 0) {
            char *key = strtok(NULL, "");
            strncpy(msg_send.kv.key, key, KEY_VALUE_SIZE - 1);
            msg_send.kv.key[KEY_VALUE_SIZE - 1] = '\0';
            msg_send.kv.value[0] = '\0'; 
            msg_send.type = MSG_GET;
        } else {
            printf("Invalid command\n");
            continue;
        }

        memcpy(send_buffer, &msg_send, sizeof(struct message));

        // 메시지 전송
        post_send_message(&msg_send);

        // 서버로부터 응답 수신
        receive_response();
    }

    cleanup(id);
    return 0;
}

static void cleanup(struct rdma_cm_id *id) {
    if (ctx.send_mr) {
        ibv_dereg_mr(ctx.send_mr);
        ctx.send_mr = NULL;
    }

    if (ctx.recv_mr) {
        ibv_dereg_mr(ctx.recv_mr);
        ctx.recv_mr = NULL;
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

    if (send_buffer) {
        free(send_buffer);
        send_buffer = NULL;
    }
    if (recv_buffer) {
        free(recv_buffer);
        recv_buffer = NULL;
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
Memory registered at address 0x559b2887b8c0 with LKey 64732
Connecting...
Connection established.
The client is connected successfully. 

Enter command ( put k v / get k ): put a b
Enter command ( put k v / get k ): get a
Send failed with status local protection error
*/

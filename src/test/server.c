#include "common.h"

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
static int count = 0;


static void setup_connection();
static int handle_event();
static void on_connect();

static int pre_post_recv_buffer();
static void wait_for_completion();
static void process_message();
int post_and_wait(struct ibv_send_wr *wr, const char *operation_name);
void cleanup(struct rdma_cm_id *id);


#define HASH_SIZE 100

static struct kv_pair *hash_table[HASH_SIZE];

unsigned int hash(const char *key) {
    unsigned int hash = 0;
    while (*key) {
        hash = (hash << 5) + *key++;
    }
    return hash % HASH_SIZE;
}

void put(const char *key, const char *value) {
    unsigned int index = hash(key);
    printf("PUT operation hash key: %d\n", index);
    
    struct kv_pair *new_entry = malloc(sizeof(struct kv_pair));
    strncpy(new_entry->key, key, KEY_VALUE_SIZE);
    strncpy(new_entry->value, value, KEY_VALUE_SIZE);
    new_entry->next = hash_table[index];
    hash_table[index] = new_entry;
    //printf("PUT operation: Key: %s, Value: %s\n\n", key, value);
}

char *get(const char *key) {
    unsigned int index = hash(key);
    printf("GET operation hash key: %d\n", index);
    struct kv_pair *entry = hash_table[index];
    while (entry != NULL) {
        if (strncmp(entry->key, key, KEY_VALUE_SIZE) == 0) {
            printf("GET operation: Key: %s, Value: %s\n", key, entry->value);
            return entry->value;
        }
        entry = entry->next;
    }
    //printf("GET operation: Key: %s, Value: not found\n\n", key);
    return NULL;
}

int main() {
    setup_connection();
    return EXIT_SUCCESS;
}

static void setup_connection() {
    struct sockaddr_in addr;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SERVER_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    ec = rdma_create_event_channel();
    if (!ec) {
        perror("rdma_create_event_channel");
        exit(EXIT_FAILURE);
    }

    if (rdma_create_id(ec, &listen_id, NULL, RDMA_PS_TCP)) {
        perror("rdma_create_id");
        exit(EXIT_FAILURE);
    }

    if (rdma_bind_addr(listen_id, (struct sockaddr *)&addr)) {
        perror("rdma_bind_addr");
        exit(EXIT_FAILURE);
    }

    if (rdma_listen(listen_id, 1)) {
        perror("rdma_listen");
        exit(EXIT_FAILURE);
    }

    //printf("Listening for incoming connections...\n");

    while (1) {
        
        count++;
        //printf("count: %d\n", count);

        if (rdma_get_cm_event(ec, &event)) {
            perror("rdma_get_cm_event");
            exit(EXIT_FAILURE);
        }

        id = event->id;

        if (handle_event()) {
            break;
        }

        if (rdma_ack_cm_event(event)) {
            perror("rdma_ack_cm_event");
            exit(EXIT_FAILURE);
        }
    }
}

static int handle_event() {

    printf("Event type: %s\n", rdma_event_str(event->event));

    if (event->event == RDMA_CM_EVENT_CONNECT_REQUEST) {
        printf("Connection request received.\n");
        on_connect();
    } else if(event->event == RDMA_CM_EVENT_ESTABLISHED) {
		printf("connect established.\n");
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
    printf("Queue Pair created: %p\n", (void*)id->qp);

    pre_post_recv_buffer();

    rep_pdata.buf_va = htonll((uintptr_t) recv_buffer);
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
    printf("Received client Memory at address %p with RKey %u\n", (void *)rep_pdata.buf_va, ntohl(rep_pdata.buf_rkey));
}

static int pre_post_recv_buffer() {
    static int buffer_initialized = 0;

    if (!buffer_initialized) {
        recv_buffer = calloc(2, sizeof(struct message));  // 메시지 두 개를 받을 수 있도록 설정
        if (!recv_buffer) {
            perror("Failed to allocate memory for receive buffer");
            exit(EXIT_FAILURE);
        }

        ctx.recv_mr = ibv_reg_mr(ctx.pd, recv_buffer, sizeof(struct message), 
            IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE);

        if (!ctx.recv_mr) {
            perror("Failed to register memory region");
            exit(EXIT_FAILURE);
        }

        buffer_initialized = 1;  // 버퍼 초기화 완료
        printf("Memory registered at address %p with LKey %u\n", recv_buffer, ctx.recv_mr->lkey);
    }

    recv_sge.addr = (uintptr_t)recv_buffer;
    recv_sge.length = sizeof(struct message);  // 한 번에 한 메시지를 처리한다고 가정

    recv_sge.lkey = ctx.recv_mr->lkey;

    memset(&recv_wr, 0, sizeof(recv_wr));
    recv_wr.wr_id = 0;
    recv_wr.sg_list = &recv_sge;
    recv_wr.num_sge = 1;

    if (ibv_post_recv(id->qp, &recv_wr, &bad_recv_wr)) {
        perror("Failed to post receive work request");
        return 1;
    }
    

    return 0;
}


static void wait_for_completion()
{
    int ret;

    do {
        ret = ibv_poll_cq(ctx.cq, 1, &wc);
    } while (ret == 0);

    if (ret < 0) {
        perror("ibv_poll_cq");
        exit(EXIT_FAILURE);
    }
    
    if (wc.status != IBV_WC_SUCCESS) {
        fprintf(stderr, "Work completion error: %s\n", ibv_wc_status_str(wc.status));
        exit(EXIT_FAILURE);
    }

    //printf("wait_for_completion ended\n");
}

static void process_message() {

    while(1) {
        //printf("here. \n\n");
        
        struct message *msg = (struct message *)recv_buffer;
        wait_for_completion();
        
        send_buffer = (char *)calloc(2, sizeof(struct message));
        //send_buffer = (char *)malloc(sizeof(uint32_t));
        if (!send_buffer) {
            perror("Failed to allocate memory for send buffer");
            exit(EXIT_FAILURE);
        }

        ctx.send_mr = ibv_reg_mr(ctx.pd, send_buffer, sizeof(struct message), IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE);
        if (!ctx.send_mr) {
            fprintf(stderr, "Failed to register client metadata buffer.\n");
            exit(EXIT_FAILURE);
        }

        if (msg == NULL) {
            printf("Received null message.\n");
            exit(EXIT_FAILURE);
        }

        //printf("Packet size: %lu bytes\n\n", sizeof(struct message));
        //printf("Received message - Type: %d, Key: %s, Value: %s\n", msg->type, msg->kv.key, msg->kv.value);
        // printf("\nrecv_buffer content:\n");
        // printf("Type: %d\n", msg->type);
        // printf("Key: %s\n", msg->kv.key);
        // printf("Value: %s\n\n", msg->kv.value);
    
        if (msg->type == MSG_PUT) {
            put(msg->kv.key, msg->kv.value);
            //printf("PUT operation: Key: %s, Value: %s\n", msg->kv.key, msg->kv.value);

            //snprintf(send_buffer, BUFFER_SIZE, "PUT %s %s", msg->kv.key, msg->kv.value);
            strncpy(send_buffer, "PUT ", sizeof(msg->type));
            strncat(send_buffer, msg->kv.key, sizeof(msg->kv.key));
            strncat(send_buffer, " ", sizeof(msg->type));
            strncat(send_buffer, msg->kv.value, sizeof(msg->kv.value));

        } else if (msg->type == MSG_GET) {
            //printf("GET operation: Key: %s, Value: dummy_value\n", msg->kv.key);

            char *value = get(msg->kv.key);
            if (value) {
                strncpy(msg->kv.value, value, KEY_VALUE_SIZE);
            } else {
                strncpy(msg->kv.value, "NOT_FOUND", KEY_VALUE_SIZE);
            }
            
            //snprintf(send_buffer, BUFFER_SIZE, "GET %s", msg->kv.key);
            strncpy(send_buffer, "GET ", BUFFER_SIZE);
            strncat(send_buffer, msg->kv.key, BUFFER_SIZE - strlen(send_buffer) - 1);
        }

        send_sge.addr = (uintptr_t)send_buffer;
        send_sge.length = sizeof(struct message);
        //send_sge.length = sizeof(uint32_t);
        send_sge.lkey = ctx.send_mr->lkey;

        //memset(&send_wr, 0, sizeof(send_wr));
        send_wr.opcode = IBV_WR_SEND;
        send_wr.send_flags = IBV_SEND_SIGNALED;
        send_wr.sg_list = &send_sge;
        send_wr.num_sge = 1;
        send_wr.wr_id = 1;

        send_wr.wr.rdma.rkey = ntohl(rep_pdata.buf_rkey);
	    send_wr.wr.rdma.remote_addr = ntohll(rep_pdata.buf_va); 

        struct message *msg_in_buffer = (struct message *)send_buffer;
        memcpy(msg_in_buffer, msg, sizeof(struct message));

        // printf("\nsend_buffer content:\n");
        // printf("Type: %d\n", msg_in_buffer->type);
        // printf("Key: %s\n", msg_in_buffer->kv.key);
        // printf("Value: %s\n\n", msg_in_buffer->kv.value);

        if (ibv_post_send(id->qp, &send_wr, &bad_send_wr)) {
            fprintf(stderr, "Failed to post send work request: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
   
        wait_for_completion();

        printf("Send completed successfully\n\n");


        // 이벤트 채널에서 완료 큐 이벤트 기다리기
        if (ibv_get_cq_event(ctx.comp_channel,&ctx.evt_cq,&cq_context)) {
            perror("ibv_get_cq_event");
            free(send_buffer);
            ibv_dereg_mr(ctx.send_mr);
            exit(EXIT_FAILURE);
        }

        // 완료 큐에서 이벤트를 처리
        ibv_ack_cq_events(ctx.cq,1);

	    if (ibv_req_notify_cq(ctx.cq,0)) {
            free(send_buffer);
            ibv_dereg_mr(ctx.send_mr);
            exit(EXIT_FAILURE);
        }
        
		pre_post_recv_buffer();
    }
}



void cleanup(struct rdma_cm_id *id) {
    if (send_buffer) {
        assert(send_buffer != NULL); 
        free(send_buffer);
        send_buffer = NULL; 
    }

    if (recv_buffer) {
        assert(recv_buffer != NULL); 
        free(recv_buffer);
        recv_buffer = NULL; 
    }

    if (ctx.recv_mr) {
        assert(ctx.recv_mr != NULL); 
        ibv_dereg_mr(ctx.recv_mr);
        ctx.recv_mr = NULL;
    }

    if (ctx.send_mr) {
        assert(ctx.send_mr != NULL); 
        ibv_dereg_mr(ctx.send_mr);
        ctx.send_mr = NULL; 
    }

    if (ctx.qp) {
        assert(ctx.qp != NULL); 
        rdma_destroy_qp(id);
        ctx.qp = NULL; 
    }

    if (ctx.cq) {
        assert(ctx.cq != NULL); 
        ibv_destroy_cq(ctx.cq);
        ctx.cq = NULL; 
    }

    if (ctx.comp_channel) {
        assert(ctx.comp_channel != NULL);
        ibv_destroy_comp_channel(ctx.comp_channel);
        ctx.comp_channel = NULL; 
    }

    if (ctx.pd) {
        assert(ctx.pd != NULL);
        ibv_dealloc_pd(ctx.pd);
        ctx.pd = NULL; 
    }

    if (id) {
        assert(id != NULL);
        rdma_destroy_id(id);
        id = NULL; 
    }

    if (ec) {
        assert(ec != NULL); 
        rdma_destroy_event_channel(ec);
        ec = NULL;
    }

    printf("here.\n");
}


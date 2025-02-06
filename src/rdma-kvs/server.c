#include "common.h"

#define MAX_TENANT_NUM 5

struct ibv_recv_wr recv_wr, *bad_recv_wr = NULL;
struct ibv_send_wr send_wr, *bad_send_wr = NULL;
struct ibv_sge recv_sge, send_sge;
struct ibv_wc wc;
static char *send_buffer = NULL, *recv_buffer = NULL;
static void *cq_context;
static int count = 0;


// 자원 관리 (메모리 공유)
struct perf_shm_context {
    uint32_t next_tenant_id;
    uint32_t tenant_num;
    uint32_t active_tenant_num;
    uint64_t active_qps_num;
    uint32_t active_stenant_num;
    uint32_t active_dtenant_num;
    uint32_t active_mtenant_num;
    uint32_t active_rrtenant_num;
    uint32_t max_qps_limit;

    uint32_t active_qps_per_tenant[MAX_TENANT_NUM];
    uint32_t additional_qps_num[MAX_TENANT_NUM];
    uint64_t avg_msg_size[MAX_TENANT_NUM];

    pthread_mutex_t perf_thread_lock[MAX_TENANT_NUM];
    pthread_cond_t perf_thread_cond[MAX_TENANT_NUM];
    pthread_mutex_t lock;
};

struct tenant_context {
    struct rdma_context ctx;
    struct rdma_cm_id* id;
    struct rdma_cm_id* listen_id;
    struct rdma_event_channel* ec;
    struct rdma_cm_event* event;
    struct ibv_qp_init_attr qp_attr;
    struct pdata rep_pdata;  
};

struct tenant_context ctx[MAX_TENANT_NUM];


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
    printf("PUT operation: Key: %s, Value: %s\n\n", key, value);
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
    printf("GET operation: Key: %s, Value: not found\n\n", key);
    return NULL;
}

int main() {
    // 공유 메모리 생성 및 초기화
    struct perf_shm_context* shm_ctx = NULL;
    int shm_fd;

    printf("Init perf_shm\n");
    shm_fd = shm_open("/perf-shm", O_CREAT | O_RDWR, 0666);

    if (shm_fd == -1)
    {
        printf("Open perf_shm is failed\n");
        exit(1);
    }

    if (ftruncate(shm_fd, sizeof(struct perf_shm_context)) < 0)
        printf("ftruncate error\n");

    shm_ctx = (struct perf_shm_context*)mmap(0, sizeof(struct perf_shm_context), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

    if (shm_ctx == MAP_FAILED) {
        printf("Error mapping shared memory perf_shm");
        exit(1);
    }

    shm_ctx->next_tenant_id = 0;
    shm_ctx->tenant_num = 0;
    shm_ctx->active_tenant_num = 0;
    shm_ctx->active_qps_num = 0;
    shm_ctx->active_stenant_num = 0;
    shm_ctx->active_mtenant_num = 0;
    shm_ctx->active_dtenant_num = 0;

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&(shm_ctx->lock), &attr);

    pthread_condattr_t attrcond;
    pthread_condattr_init(&attrcond);
    pthread_condattr_setpshared(&attrcond, PTHREAD_PROCESS_SHARED);

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

    printf("Listening for incoming connections...\n\n");

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
// 이벤트 처리
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

void* create_thread(void* arg) {
    struct rdma_cm_id* id = (struct rdma_cm_id*)arg;
    struct rdma_context* client_ctx = NULL;

    // 공유 메모리에서 새로운 tenant_context 할당
    int tenant_id = shm_ctx->next_tenant_id;
    if (tenant_id >= MAX_TENANT_NUM) {
        fprintf(stderr, "Maximum number of tenants reached.\n");
        rdma_disconnect(id);
        return NULL;
    }

    // 연결 가능한지 확인 후 연결
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (ctx[i].qp == NULL) { 
            client_ctx = &ctx[i];
            break;
        }
    }
    
    if (!client_ctx) {
        fprintf(stderr, "Max clients reached, rejecting connection.\n");
        rdma_disconnect(id);  
        return NULL;
    }

    // !! 여기가 qp 생성~~
    build_context(client_ctx, id);
    build_qp_attr(&qp_attr, client_ctx);

    printf("Creating QP for client...\n");
    if (rdma_create_qp(id, client_ctx->pd, &qp_attr)) {
        perror("rdma_create_qp");
        exit(EXIT_FAILURE);
    }

    printf("Queue Pair created for client: %p\n", (void*)id->qp);
    pre_post_recv_buffer();

    struct rdma_conn_param conn_param;
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

    // 클라이언트 정보 복사
    memcpy(&client_ctx->rep_pdata, client_ctx->event->param.conn.private_data, sizeof(client_ctx->rep_pdata));
    printf("Received client Memory at address %p with RKey %u\n", (void*)client_ctx->rep_pdata.buf_va, ntohl(client_ctx->rep_pdata.buf_rkey));

    // 고유한 tenant_id 할당
    shm_ctx->next_tenant_id = (tenant_id + 1) % MAX_TENANT_NUM;  


    return NULL;
}

static void on_connect(struct rdma_cm_event* event) {
    struct rdma_cm_id* id = event->id;

    // 멀티 스레드 생성
    pthread_t thread;
    int ret = pthread_create(&thread, NULL, create_thread, (void*)id);
    if (ret != 0) {
        perror("pthread_create");
        exit(EXIT_FAILURE);
    }

    // 비동기처리
    pthread_detach(thread);
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
    printf("Memory registered at address %p with LKey %u\n", recv_buffer, ctx.recv_mr->lkey);

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

    printf("wait_for_completion ended\n");
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
        printf("\nrecv_buffer content:\n");
        printf("Type: %d\n", msg->type);
        printf("Key: %s\n", msg->kv.key);
        printf("Value: %s\n\n", msg->kv.value);
    
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

        printf("\nsend_buffer content:\n");
        printf("Type: %d\n", msg_in_buffer->type);
        printf("Key: %s\n", msg_in_buffer->kv.key);
        printf("Value: %s\n\n", msg_in_buffer->kv.value);

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


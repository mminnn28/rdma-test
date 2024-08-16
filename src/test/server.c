#include "common.h"

/* These are RDMA connection related resources */
static struct rdma_context ctx;
static struct rdma_cm_id *id = NULL;
static struct rdma_event_channel *ec = NULL;
static struct rdma_cm_event *event = NULL;
static struct ibv_qp_init_attr *qp_attr;

#define KV_STORE_SIZE 1024

struct kv_store {
    char key[KEY_VALUE_SIZE];
    char value[KEY_VALUE_SIZE];
} kv_store[KV_STORE_SIZE];

void setup_connection(struct sockaddr_in *addr);
int on_event(struct rdma_cm_event * event);
void on_connect(struct rdma_cm_id *id);
void on_completion(struct ibv_wc *wc);


int main() {

    struct sockaddr_in addr = {
        .sin_family = AF_INET, // IPv4
        .sin_port = htons(SERVER_PORT), // 서버의 포트번호(20079) htons를 통해 byte order를 network order로 변환
        .sin_addr.s_addr = INADDR_ANY // 서버의 IP 주소를 network byte order로 변환
    };

    memset(&addr, 0, sizeof(addr));

    setup_connection(&addr);
    return 0;
}

void setup_connection(struct sockaddr_in *addr) {

    int ret;

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


    ret = rdma_bind_addr(id, (struct sockaddr *)&addr);
    if (ret) {
        perror("rdma_bind_addr");
        exit(EXIT_FAILURE);
    }

    ret = rdma_listen(id, 0);
    if (ret) {
        perror("rdma_listen");
        exit(EXIT_FAILURE);
    }

    //printf("Server listening on port %d...\n", SERVER_PORT);

    printf("Server is listening successfully at: %s , port: %d \n", inet_ntoa(addr->sin_addr), ntohs(addr->sin_port));

    while (1) {
        //RDMA_CM_EVENT_CONNECT_REQUEST
        ret = rdma_get_cm_event(ec, &event);
        if (ret) {
            perror("rdma_get_cm_event");
            exit(EXIT_FAILURE);
        }
        id = event->id;

        //successful get and ack should be one-to-one correspondence.
		struct rdma_cm_event event_copy;
		memcpy(&event_copy,event,sizeof(*event));
     
        if (rdma_ack_cm_event(event)) {
		    rdma_error("Failed to acknowledge the cm event errno: %d \n", -errno);
		    return -errno;
	    }

        //on_event hold on the retrieved event.
		if(on_event(&event_copy)) {
			break;
		}
    }
}

int on_event(struct rdma_cm_event * event)
{
	printf("event type: %s.\n",rdma_event_str(event->event));
    int ret = 0;

	if (event->event == RDMA_CM_EVENT_CONNECT_REQUEST)
	{
		printf("connect request.\n");

        struct rdma_cm_id *client_id = event->id;
        build_context(&ctx);
        build_qp_attr(&qp_attr, &ctx);
        rdma_create_qp(client_id, ctx.pd, &qp_attr);
        rdma_accept(id, NULL);
	}
	else if(event->event == RDMA_CM_EVENT_ESTABLISHED)
	{
		printf("connect established.\n");
        ret = on_connect(event->id);
	}
	else if(event->event == RDMA_CM_EVENT_DISCONNECTED)
	{
		printf("disconnected.\n");

		ret = on_disconnect(event->id);
	}
	else
	{
		printf("on_event: unknown event.\n");
	}

	return ret;
}

void on_connect(struct rdma_cm_id *id) {
    printf("Client connected.\n");
    struct rdma_context *ctx = (struct rdma_context *)id->context;
    recv_msg(&ctx);

    rdma_ack_cm_event(event);
    while (1) {
        struct ibv_wc wc;
        int num_completions = ibv_poll_cq(ctx->cq, 1, &wc);
        if (num_completions > 0) {
            on_completion(&wc);
        }
    }
}

void on_completion(struct ibv_wc *wc) {
    struct rdma_context *ctx = (struct rdma_context *)(uintptr_t)wc->wr_id;
    struct message *msg = (struct message *)ctx->send_buffer;

    if (wc->opcode == IBV_WC_RECV) {
        if (msg->type == MSG_PUT) {
            // PUT 요청 처리
            int i;
            for (i = 0; i < KV_STORE_SIZE; i++) {
                if (kv_store[i].key[0] == '\0' || strcmp(kv_store[i].key, msg->kv.key) == 0) {
                    strcpy(kv_store[i].key, msg->kv.key);
                    strcpy(kv_store[i].value, msg->kv.value);
                    msg->addr = (uint64_t)&kv_store[i];
                    printf("Stored key '%s' at memory address: %p\n", kv_store[i].key, &kv_store[i]);
                    break;
                }
            }
        } else if (msg->type == MSG_GET) {
            // GET 요청 처리
            int i;
            for (i = 0; i < KV_STORE_SIZE; i++) {
                if (strcmp(kv_store[i].key, msg->kv.key) == 0) {
                    strcpy(msg->kv.value, kv_store[i].value);
                    msg->addr = (uint64_t)&kv_store[i];
                    printf("Retrieved key '%s' from memory address: %p\n", kv_store[i].key, &kv_store[i]);
                    break;
                }
            }
        }

        struct ibv_sge sge;
        struct ibv_send_wr wr, *bad_wr = NULL;

        sge.addr = (uintptr_t)ctx->send_buffer;
        sge.length = sizeof(struct message);
        sge.lkey = ctx->send_mr->lkey;

        memset(&wr, 0, sizeof(wr));
        wr.wr_id = (uintptr_t)ctx;
        wr.opcode = IBV_WR_SEND;
        wr.sg_list = &sge;
        wr.num_sge = 1;

        ibv_post_send(ctx->qp, &wr, &bad_wr);
        post_receives(ctx);
    }
}


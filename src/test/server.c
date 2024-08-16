#include "common.h"

/* These are RDMA connection related resources */
static struct rdma_context ctx = {0};
static struct rdma_cm_id *id = NULL;
static struct rdma_event_channel *ec = NULL;
static struct rdma_cm_event *event = NULL;
static struct ibv_qp_init_attr *qp_attr;

struct kv_store {
    char key[KEY_VALUE_SIZE];
    char value[KEY_VALUE_SIZE];
} kv_store[KEY_VALUE_SIZE];

void setup_connection(struct sockaddr_in *addr);
int on_event();

void on_connect(struct rdma_cm_id *id);
void on_complete(struct ibv_wc *wc);
int on_disconnect(struct rdma_cm_id * id);


int main(int argc, char **argv) {

    struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET; 
    addr.sin_port = htons(SERVER_PORT);
	addr.sin_addr.s_addr = htonl(INADDR_ANY); 

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


    ret = rdma_bind_addr(id, (struct sockaddr *)addr);
    if (ret) {
        perror("rdma_bind_addr");
        exit(EXIT_FAILURE);
    }

    ret = rdma_listen(id, 0);
    if (ret) {
        perror("rdma_listen");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d...\n", SERVER_PORT);

    while (1) {
        //add generate a RDMA_CM_EVENT_CONNECT_REQUEST
        //printf("RDMA_CM_EVENT_CONNECT_REQUEST event...\n");
        ret = rdma_get_cm_event(ec, &event);
        if (ret) {
            perror("rdma_get_cm_event");
            exit(EXIT_FAILURE);
        }

        id = event->id;

        if(on_event(id)) {
			break;
		}
    
        if (rdma_ack_cm_event(event)) {
		    perror("rdma_ack_cm_event");
            exit(EXIT_FAILURE);
	    }
    }
}

//disconnected 수정해야됨
int on_event() {
    
    struct rdma_conn_param params;

	printf("event type: %s.\n",rdma_event_str(event->event));
    int ret = 0;

	if (event->event == RDMA_CM_EVENT_CONNECT_REQUEST) {
		
        //printf("Connect request.\n");

        /* RDMA resource & QP attr (common.c) */
        build_context(&ctx, id);
        build_qp_attr(&qp_attr, &ctx);
        

        /* creating QP */
        printf("Creating QP...\n");
        ret = rdma_create_qp(id, ctx.pd, &qp_attr);
        if (ret) {
            perror("rdma_create_qp");
            exit(EXIT_FAILURE);
        }

        memset(&params, 0, sizeof(params));
        params.initiator_depth = 3;
        params.responder_resources = 3;

        printf("Connecting...\n\n");
        ret = rdma_accept(id, &params);
        if (ret) {
	        perror("rdma_accept");
            exit(EXIT_FAILURE);
        }

	} else if(event->event == RDMA_CM_EVENT_ESTABLISHED) {
		printf("Connect established.\n");
        on_connect(id);
	}
	else if(event->event == RDMA_CM_EVENT_DISCONNECTED) {
		printf("disconnected.\n");
      
        ibv_destroy_cq(ctx.cq);
        ibv_destroy_comp_channel(ctx.comp_channel);
        ibv_dealloc_pd(ctx.pd);
        rdma_destroy_id(id);

		ret = on_disconnect(event->id);
	}

	return ret;
}

void on_connect(struct rdma_cm_id *id) {
    printf("Client connected.\n\n");


    struct rdma_context *ctx = (struct rdma_context *)id->context;

    
    while (1) {
        struct ibv_wc wc;
        int num_completions = ibv_poll_cq(ctx->cq, 1, &wc);
        if (num_completions > 0) {
            on_complete(&wc);
        }
    }
}

void on_complete(struct ibv_wc *wc) {
    struct rdma_context *ctx = (struct rdma_context *)(uintptr_t)wc->wr_id;
    struct message *msg = (struct message *)ctx->send_buffer;

    if (wc->opcode == IBV_WC_RECV) {
        if (msg->type == MSG_PUT) {
            // PUT 요청 처리
            int i;
            for (i = 0; i < KEY_VALUE_SIZE; i++) {
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
            for (i = 0; i < KEY_VALUE_SIZE; i++) {
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
        recv_msg(ctx);
    }
}

int on_disconnect(struct rdma_cm_id * id)
{
	int ret = 0;
    
    free(ctx.send_buffer);
    free(ctx.recv_buffer);
    ctx.send_buffer = NULL;
    ctx.recv_buffer = NULL;

	ret = ibv_dereg_mr(ctx.send_mr);
        if (ret)
        {
                printf("ibv_dereg_mr(ctx->send_mr) error");
                return ret;
        }

        ibv_dereg_mr(ctx.recv_mr);
        if (ret)
        {
                printf("ibv_dereg_mr(ctx->send_mr) error");
                return ret;
        }

	free(id->context);

    rdma_destroy_qp(id);
	rdma_destroy_id(id);

	return ret;
}	

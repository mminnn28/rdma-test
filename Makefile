# Makefile

# 변수 정의
CC = gcc
CFLAGS = -Wall -g
LIBS = -lrdmacm -libverbs

# 실행 파일 이름
TARGETS = rdma_server rdma_client

# 소스 파일과 객체 파일 정의
SRCS = rdma_server.c rdma_client.c
OBJS = $(SRCS:.c=.o)

# 기본 목표
all: $(TARGETS)

# 실행 파일 빌드

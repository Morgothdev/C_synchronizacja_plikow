#ifndef TYPES_H_
#define TYPES_H_

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <pthread.h>
#include <sys/socket.h>
#include <errno.h>
#include <netinet/in.h>
#include <unistd.h>
#include <regex.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/param.h>
//#define _XOPEN_SOURCE
#include <sys/stat.h>
#include <time.h>
#include <ftw.h>
#include <sys/file.h>
#include <dirent.h>

#define CONNECTION_OK  1
#define FULL_CLIENTS  2
#define LOGOUT  3
#define TEST_CONNECTION  4
#define CHECK_FILE  5
#define DELETE_FILE 6
#define SERVER_DOESNT_HAVE 7
#define RECEIVE 8
#define SEND_ME  9
#define VER_OK 10
#define SYNC  11
#define NEXT_FILE  12
#define CONTINUE  13

#define ERRMSG_LEN  512
#define DEFAULT_PORT 9090
#define CLIENTS_MAX  20
#define BUFFER_SIZE 5000
#define TIME_BUFFER_SIZE 25

char errmsg[ERRMSG_LEN];
const char *VERSION_FORMAT = "%Y-%m-%d__%H-%M-%S";

typedef struct {
	char free;
	pthread_t con_thr;
	int sockFD;
} conn_t;

typedef struct {
	conn_t connections[CLIENTS_MAX];
	pthread_rwlock_t lock;
	int count;
} conn_list_t;

typedef struct {
	int type;
	int val;
} request;

typedef struct {
	int len;
	char data[BUFFER_SIZE];
} buffer_t;

#endif

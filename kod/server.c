#include "types.h"

int sfd;
int active;

conn_list_t conn_list;
pthread_key_t sockets;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

int create_and_connect_socket(uint16_t port) {
	int sfd;
	if ((sfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)) < 0) {
		sprintf(errmsg, "Error with creating socket: %s", strerror(errno));
		return -1;
	}
	struct sockaddr_in sock;
	memset(&sock, 0, sizeof(sock));
	sock.sin_family = AF_INET;
	sock.sin_port = htons(port);
	sock.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(sfd, (struct sockaddr*) &sock, sizeof(struct sockaddr_in))) {
		sprintf(errmsg, "Error with binding socket: %s", strerror(errno));
		close(sfd);
		return -1;
	}
	sprintf(errmsg, "Success with creating and connecting socket.");
	return sfd;
}

int logout(int index) {
	pthread_rwlock_wrlock(&conn_list.lock);
	conn_list.count--;
	conn_list.connections[index].free = 1;
	printf("Client logout.\n");
	pthread_rwlock_unlock(&conn_list.lock);
	return 0;
}

char *make_str_version(time_t tim, char *ver) {
	struct tm *tm2;
	tm2 = localtime(&tim);
	strftime(ver, TIME_BUFFER_SIZE, VERSION_FORMAT, tm2);
	return ver;
}

time_t make_tim_version(char *ver) {
	struct tm t;
	strptime(ver, VERSION_FORMAT, &t);
	return mktime(&t);
}

int set_version_of_file(const char *filepath, char *version, int delete) {

	char version_file[MAXPATHLEN];
	sprintf(version_file, "%s/version", filepath);
	FILE *ver = fopen(version_file, "w");
	if (ver == NULL) {
		sprintf(errmsg, "error with opening version file.");
		return -1;
	}
	fprintf(ver, "%d|%s", delete, version);
	if (fclose(ver)) {
		sprintf(errmsg, "error with closing version file.");
		return -2;
	}

	return 0;
}

time_t get_version(char *filepath, int *deleted) {

	struct stat inf;
	if (stat(filepath, &inf)) {
		*deleted = 0;
		return 0;
	};
	if (S_ISDIR(inf.st_mode)) {
		char date[TIME_BUFFER_SIZE];
		char version_file[MAXPATHLEN];
		sprintf(version_file, "%s/version", filepath);
		FILE *ver = fopen(version_file, "r");
		if (ver == NULL) {
			return 0;
		}
		fscanf(ver, "%d|%s", deleted, date);
		fclose(ver);
		return make_tim_version(date);
	} else {
		printf("something happen wrong :(\n");
		return 0;
	}
}

int send_file(const char *filepath, int sock) {
	FILE *file = fopen(filepath, "r");
	if (file == NULL) {
		printf("error with opening file %s to send, because %s\n", filepath,
				strerror(errno));
	}
	while (!flock(fileno(file), LOCK_SH)) {
	}

	int readed;
	buffer_t buf;
	printf("sizeof buf: %ld\n", sizeof(buf));
	readed = fread(buf.data, sizeof(char), BUFFER_SIZE, file);
	do {
		printf("readed %d\n", readed);
		buf.len = readed;
		if (send(sock, &buf, sizeof(buf), 0) < BUFFER_SIZE) {
			printf("error with sending file\n");
		}
		readed = fread(buf.data, sizeof(char), BUFFER_SIZE, file);
	} while (readed > 0);
	if (fclose(file)) {
		sprintf(errmsg, "error with closing file.");
		return -2;
	}
	return 0;
}

int receive_file(char *filepath, int sock) {
	FILE *file = fopen(filepath, "w");
	if (file == NULL) {
		printf("error with opening file to rewrite.\n");
		return -1;
	}
	buffer_t buf;
	while (recv(sock, &buf, sizeof(buf), 0) > 0) {
		fwrite(buf.data, sizeof(char), buf.len, file);
		if (buf.len < BUFFER_SIZE)
			break;
	}
	if (fclose(file)) {
		sprintf(errmsg, "error with closing file.");
		return -1;
	}
	return 0;
}

int get_file(char *filepath, char *version, int sock) {

	int inf = SEND_ME;
	if (send(sock, &inf, sizeof(int), 0) == 0) {
		sprintf(errmsg, "cannot send SEND_ME request to receive file");
		printf("error with sending SEND_ME signal\n");
		return -1;
	} else {
		printf("signal sent.\n");
	}

	char buffer[BUFFER_SIZE + TIME_BUFFER_SIZE];
	sprintf(buffer, "%s/%s", filepath, version);
	if (receive_file(buffer, sock)) {
		printf("error with receiving file\n");
		return -1;
	}

	if (set_version_of_file(filepath, version, 0)) {
		char tmp[ERRMSG_LEN];
		sprintf(tmp, "error with setting version: %s", errmsg);
		strcpy(errmsg, tmp);
		return -1;
	}
	return 0;
}

int give_file(char *filepath, time_t version, int sock) {

	int inf = RECEIVE;
	if (send(sock, &inf, sizeof(inf), 0) != sizeof(inf)) {
		sprintf(errmsg, "cannot send RECEIVE request to receive file");
		printf("error with sending RECEIVE signal\n");
		return -1;
	} else {
		printf("signal sent.\n");
	}

	char buffer[MAXPATHLEN];
	char data[TIME_BUFFER_SIZE];
	sprintf(buffer, "%s/%s", filepath, make_str_version(version, data));

	if (send(sock, &version, sizeof(version), 0) != sizeof(version)) {
		printf("error with sending version to client\n");
	}

	if (send_file(buffer, sock)) {
		printf("error with sending file\n");
		return -1;
	}

	return 0;
}

int check_file(int sock, int path_len) {
	char filepath[BUFFER_SIZE];
	if (recv(sock, filepath, path_len, 0) != path_len) {
		sprintf(errmsg, "error with reading from socket:  %s", strerror(errno));
		return -1;
	}
	filepath[path_len] = 0;
	time_t cli_ver;
	if (recv(sock, &cli_ver, sizeof(cli_ver), 0) != sizeof(cli_ver)) {
		sprintf(errmsg, "error with reading from socket:  %s", strerror(errno));
		return -1;
	}
	char date[TIME_BUFFER_SIZE];
	make_str_version(cli_ver, date);
	printf("check file:\"%s\"\n\twith date \"%s\"(%ld).\n", filepath, date,
			cli_ver);

	time_t serv_ver;
	int deleted;
	char buffer[BUFFER_SIZE + TIME_BUFFER_SIZE];
	sprintf(buffer, "mkdir -p \"%s\"", filepath);
	system(buffer);
	printf("blokada jest\n");
	if ((serv_ver = get_version(filepath, &deleted)) == (time_t) 0) {
		printf("creating file\n");
		if (get_file(filepath, date, sock)) {
			printf("error with getting file from client: %s\n", errmsg);
			return -1;
		}
	} else {
		printf("file exists\n");
		if (difftime(cli_ver, serv_ver) > 0) {
			char date2[TIME_BUFFER_SIZE];
			printf("server has old version \n\t client %s\n\tserver %s\n", date,
					make_str_version(serv_ver, date2));
			if (get_file(filepath, date, sock)) {
				printf("error with getting file from client: %s\n", errmsg);
				return -1;
			}
		} else if (difftime(cli_ver, serv_ver) < 0) {
			printf("server has newer version\n");
			if (deleted) {
				int inf = DELETE_FILE;
				if (send(sock, &inf, sizeof(int), 0) <= 0) {
					printf("error with sendind DELETE_FILE signal\n");
					return -1;
				} else {
					printf("delete file %s sent.\n", filepath);
				}
			} else {
				if (give_file(filepath, serv_ver, sock)) {
					printf("error with sending file\n");
					return -1;
				}
			}
		} else {
			int inf = VER_OK;
			printf("versions are equal\n");
			if (send(sock, &inf, sizeof(inf), 0) <= 0) {
				printf("error with sending VER_OK: %s\n", strerror(errno));
			}
		}
	}

	return 0;
}

int look(const char *fpath, const struct stat *sb, int typeflag) {
	if (typeflag == FTW_D) {
		char version_file[MAXPATHLEN];
		sprintf(version_file, "%s/version", fpath);
		printf("\n----------------------------------\n %s\n-"
				"-----------------------------\n", version_file);
		FILE *ver = fopen(version_file, "r");
		if (ver == NULL) {
			return 0;
		}

		//plik jest
		int deleted;
		printf("send file %s\n", fpath);
		char date[TIME_BUFFER_SIZE];
		fscanf(ver, "%d|%s", &deleted, date);
		fclose(ver);
		if (deleted == 0) {
			time_t serv_ver = make_tim_version(date);

			int sock = *((int*) pthread_getspecific(sockets));
			printf("socket klient: %d\n", sock);
			unsigned int path_len = strlen(fpath);

			printf("ilosc znakow: %d\n", path_len);
			if (send(sock, &path_len, sizeof(path_len), 0)
					!= sizeof(path_len)) {
				printf("error with sending path_len\n");
				return 0;
			}
			printf("sciezka: %s\n", fpath);
			if (send(sock, fpath, path_len, 0) != path_len) {
				printf("error with sending path\n");
				return 0;
			}

			int task;
			if (recv(sock, &task, sizeof(task), 0) != sizeof(task)) {
				sprintf(errmsg, "error with reading task from socket:  %s",
						strerror(errno));
				printf("error with reading task from socket:  %s",
						strerror(errno));

				return -1;
			}
			printf("received semitask %d\n", task);
			if (task == CONTINUE) {
				if (send(sock, &serv_ver, sizeof(serv_ver), 0)
						!= sizeof(serv_ver)) {
					printf("error with sending version\n");
					return 0;
				}
				char datel[TIME_BUFFER_SIZE];
				printf("wersja serverowa: %s\n",
						make_str_version(serv_ver, datel));
				recv(sock, &task, sizeof(task), 0);
				printf("wiadomość zwrotna %d\n", task);
				if (task == SEND_ME) {
					sprintf(version_file, "%s/%s", fpath, date);
					if (send_file(version_file, sock)) {
						printf("error with sending file to server: %s\n",
								errmsg);
						return -1;
					}
				} else if (task == DELETE_FILE) {
					char date[TIME_BUFFER_SIZE];
					make_str_version(time(NULL), date);
					set_version_of_file(fpath, date, 1);
				} else {
					printf("something wrong.\n");
				}
			}
		}

	}
	return 0;
}

int synchronize_with_client() {
	printf("synchonizuje pliki z klientem\n");
	//////sleep(5);
	ftw("./", look, 40);
	int sock = *((int*) pthread_getspecific(sockets));
	int inf = -1;
	if (send(sock, &inf, sizeof(inf), 0) <= 0) {
		printf("error with sending end synchronization signal\n");
	}
	return 0;
}

void *connection_thread(void *args) {

	int index, sock;
	index = *((int*) args);
	pthread_rwlock_rdlock(&conn_list.lock);
	sock = conn_list.connections[index].sockFD;
	int *spec = (int*) malloc(sizeof(int));
	*spec = sock;
	pthread_setspecific(sockets, spec);
	printf("socket klienta do set specific: %d\n", sock);
	pthread_rwlock_unlock(&conn_list.lock);

	int info = CONNECTION_OK;

	if (send(sock, &info, sizeof(info), MSG_DONTWAIT) <= 0) {
		printf("unable send CONNECTION_OK to client\n");
	} else {
		printf("Connection accepted.\n");
	}
	info = 6543;
	if (send(sock, &info, sizeof(info), MSG_DONTWAIT) <= 0) {
		printf("unable send CONNECTION_OK to client\n");
	} else {
		printf("Connection accepted.\n");
	}
	request rq;
	while (active) {
		printf("ready to request\n");
		if (recv(sock, (void *) &rq, sizeof(rq), 0) > 0) {
			printf("connection gets request type %d\n", rq.type);
			pthread_mutex_lock(&lock);
			switch (rq.type) {
			case (CHECK_FILE):
				if (check_file(sock, rq.val)) {
					printf("Error with execute checking file: %s.\n", errmsg);
				}
				printf("file synchronized\n");
				break;
			case (TEST_CONNECTION):
				printf("client is testing connection\n");
				break;
			case (LOGOUT):
				logout(index);
				break;
			case (SYNC):
				synchronize_with_client();
				break;
			default:
				printf("unrecognized request type\n");
				break;
			}
			pthread_mutex_unlock(&lock);
		}
	}

	close(sock);

	return NULL;
}

int create_connection(int sockFD) {
	int result = 0, info;
	if (conn_list.count < CLIENTS_MAX) {
		int i = 0, is = 0;
		pthread_rwlock_wrlock(&conn_list.lock);
		while ((!is) && (i < CLIENTS_MAX)) {
			if (conn_list.connections[i].free) {
				printf("create znalazł index %d\n", i);
				conn_list.connections[i].sockFD = sockFD;
				conn_list.count++;
				int *in = malloc(sizeof(int));
				*in = i;
				if (pthread_create(&conn_list.connections[i].con_thr, NULL,
						connection_thread, (void*) in)) {
					sprintf(errmsg,
							"Cannot create new thread for connection: %s",
							strerror(errno));
					info = FULL_CLIENTS;
					if (send(sockFD, (void*) &info, sizeof(info), MSG_DONTWAIT)
							<= 0) {
						strcat(errmsg, " and cannot send info about it.");
					}
					result = -1;
					break;
				}
				conn_list.connections[i].free = 0;
				is = 1;
			}
			++i;
		}
		pthread_rwlock_unlock(&conn_list.lock);
	} else {
		result = -1;
		info = FULL_CLIENTS;
		send(sockFD, (void*) &info, sizeof(info), MSG_DONTWAIT);
	}
	return result;
}

//poboczne procedury, niezbyt istotne dla funkcjonalności, implementowane poniżej main
int parse_arguments(char **args, uint16_t *port, char *path);
void handler(int arg);
int prepare_data();
int set_work_directory(char *path);

int main(int argc, char **argv) {

	if (argc != 3) {
		printf(
				"Illegal number of arguments, should be\nprogram port root_directory\n");
		return -1;
	}
	if (prepare_data()) {
		printf("Cannot prepare data for server, program exit!.\n");
		return -1;
	}

	uint16_t port;
	char path[MAXPATHLEN];

	if (parse_arguments(argv, &port, path)) {
		printf("Invalid arguments: %s.\n", errmsg);
		return -1;
	}

	if (set_work_directory(path)) {
		printf("Error with setting directory: %s.\n", errmsg);
		return -1;
	}

	if ((sfd = create_and_connect_socket(port)) < 0) {
		printf("Cannot create socket, program exits: %s.\n", errmsg);
		return -1;
	}

	if (listen(sfd, 20)) {
		perror("Error with setting listen mode on internet socket");
		shutdown(sfd, SHUT_RDWR);
		return -1;
	}

	signal(SIGINT, handler);
	int new;
	printf("start waiting\n");
	while (active) {
		if ((new = accept(sfd, NULL, NULL)) > 0) {
			printf("Server has new connection.\n");
			if (create_connection(new)) {
				printf("Cannot create connection ; %s\n", errmsg);
			} else {
				printf("connection created\n");
			}
		};
	};

	shutdown(sfd, SHUT_RDWR);
	return 0;
}

int parse_arguments(char **args, uint16_t *port, char *path) {

	regex_t regex;
	if (regcomp(&regex, "^[0-9]*$", 0) != 0) {
		sprintf(errmsg, "Couldn't compile regex for port.");
		return -1;
	}
	if (regexec(&regex, args[1], 0, NULL, 0)) {
		sprintf(errmsg, "First argument isn't a number.");
		return -1;
	}
	sscanf(args[1], "%d", (unsigned int*) port);
	if (regcomp(&regex,
			"^\.?(/(([a-zA-Z0-9\.\?!\"@#`~'_=<>:;%&,\$\^\*\(\)\+\|\{\}])|-)+)+/?$",
			REG_EXTENDED) != 0) {
		sprintf(errmsg, "Couldn't compile regex for path.");
		return -1;
	}
	if (regexec(&regex, args[2], 0, NULL, 0)) {
		sprintf(errmsg, "Third argument isn't a path.");
		return -1;
	}
	strcpy(path, args[2]);
	return 0;
}

void handler(int arg) {
	active = 0;
}

void sockets_destructor(void *arg) {
	free(arg);
}

int prepare_data() {
	active = 1;
	pthread_rwlock_init(&conn_list.lock, NULL);
	conn_list.count = 0;
	int i;
	for (i = 0; i < CLIENTS_MAX; ++i)
		conn_list.connections[i].free = 1;
	pthread_key_create(&sockets, sockets_destructor);
	return 0;
}

int set_work_directory(char *path) {
	if (path[0] == '.') {
		//relative path
		char tmp_path[MAXPATHLEN];
		if (realpath(path, tmp_path) == NULL) {
			sprintf(errmsg, "error with resolving path: %s", strerror(errno));
			return -1;
		}
		strcpy(path, tmp_path);
	}
	printf("setting work directory to \"%s\"\n", path);
	if (chdir(path)) {
		sprintf(errmsg, "Cannot set working directory for process: %s",
				strerror(errno));
		return -1;
	}
	return 0;
}

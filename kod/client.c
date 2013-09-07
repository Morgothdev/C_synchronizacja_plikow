#include "types.h"

int active;
time_t time_last_checking = (time_t) 0;
int sock;
int cwd_len;

char *make_str_version(time_t tim, char *ver) {
	struct tm *tm2;
	tm2 = localtime(&tim);
	strftime(ver, TIME_BUFFER_SIZE, VERSION_FORMAT, tm2);
	return ver;
}

char *make_touch_version(time_t tim, char *ver) {
	struct tm *tm2;
	tm2 = localtime(&tim);
	strftime(ver, TIME_BUFFER_SIZE, "%Y%m%d%H%M.%S", tm2);
	return ver;
}
int create_and_connect_socket(uint16_t port, char *addr) {
	int sfd;
	if ((sfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		sprintf(errmsg, "Error with creating socket: %s.", strerror(errno));
		return -1;
	}
	struct sockaddr_in sock;
	memset(&sock, 0, sizeof(sock));
	sock.sin_family = AF_INET;
	sock.sin_port = htons(port);
	if (inet_pton(AF_INET, addr, &sock.sin_addr) != 1) {
		sprintf(errmsg, "Error with creating address: %s.", strerror(errno));
		close(sfd);
		return -1;
	}
	if (connect(sfd, (struct sockaddr*) &sock, sizeof(struct sockaddr_in))) {
		sprintf(errmsg, "Error with connecting Internet socket: %s.",
				strerror(errno));
		close(sfd);
		return -1;
	}
	int accept;
	if (recv(sfd, &accept, sizeof(accept), 0) < 0) {
		sprintf(errmsg, "Error with waiting for accepting connection: %s.",
				strerror(errno));
		close(sfd);
		return -1;
	} else {
		if (accept != CONNECTION_OK) {
			sprintf(errmsg, "Server is full of clients, please try run later.");
			close(sfd);
			return -1;
		}
	}
	sprintf(errmsg, "Success with creating and connecting socket.");
	return sfd;
}

int send_file(const char *filepath, int sock) {
	FILE *file = fopen(filepath, "r");
	if (file == NULL) {
		printf("error with opening file to send\n");
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

int receive_file(const char *filepath, time_t version) {
	FILE *file = fopen(filepath, "w");
	if (file == NULL) {
		printf("error with opening file %s to rewrite: %s.\n", filepath,
				strerror(errno));
		return -1;
	}
	printf("I'm receiving file %s\n", filepath);
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
	char touch[BUFFER_SIZE + TIME_BUFFER_SIZE + 20];
	char date[TIME_BUFFER_SIZE];
	sprintf(touch, "touch -m -t %s \"%s\"", make_touch_version(version, date),
			filepath);
	system(touch);
	return 0;
}

int get_file(char *filepath, time_t version) {

	int inf = SEND_ME;
	if (send(sock, &inf, sizeof(int), 0) == 0) {
		sprintf(errmsg, "cannot send SEND_ME request to receive file");
		printf("error with sending SEND_ME signal\n");
		return -1;
	} else {
		printf("signal sent.\n");
	}

	char buffer[BUFFER_SIZE];
	sprintf(buffer, "mkdir -p \"%s\"", filepath);
	system(buffer);
	if (rmdir(filepath)) {
		printf("nieudane usunięcie ostatniego katalogu\n");
	}
	if (receive_file(filepath, version)) {
		printf("error with receiving file\n");
		return -1;
	}
	return 0;
}

int look(const char *fpath, const struct stat *sb, int typeflag) {
	//strftime(&(newName[leng]),25,"%Y-%m-%d:%H-%M-%S",t2);
	if (typeflag == FTW_F) {
		request rq;
		printf("relative file: \"%s\"\n", fpath);
		rq.type = CHECK_FILE;
		rq.val = strlen(fpath);
		send(sock, &rq, sizeof(rq), 0);
		send(sock, fpath, rq.val, 0);
		send(sock, &sb->st_mtime, sizeof(&sb->st_mtime), 0);
		recv(sock, &rq.type, sizeof(int), 0);
		printf("wiadomość zwrotna %d\n", rq.type);
		if (rq.type == SEND_ME) {
			if (send_file(fpath, sock)) {
				printf("error with sending file to server: %s\n", errmsg);
				return -1;
			}
		} else if (rq.type == RECEIVE) {
			time_t version;
			if (recv(sock, &version, sizeof(version), 0) != sizeof(version)) {
				printf("error with sending version to client\n");
			}
			if (receive_file(fpath, version)) {
				printf("error with receiving file\n");
			}
		} else if (rq.type == DELETE_FILE) {
			char buf[MAXPATHLEN + 20];
			sprintf(buf, "rm -f \"%s\"", fpath);
			system(buf);
			printf("file deleted\n");
		} else if (rq.type == VER_OK) {
			printf("ver_ok\n");
		} else {
			printf("something wrong.\n");
		}

	}
	return 0;
}

int synchronize_and_send_local_directory(char *dir) {
	ftw(dir, look, 40);
	printf("updating done.\n");
	return 0;
}

int synchonize_and_download_virtual_directory() {

	int inf = SYNC;
	if (send(sock, &inf, sizeof(inf), 0) != sizeof(inf)) {
		printf("error with sending SYNC command\n");
		return -1;
	}
	printf("signal SYNC sent\n");
	unsigned int path_len = 0;
	char filepath[BUFFER_SIZE + 5];
	do {
		printf("===============================================\n");
		if (recv(sock, &path_len, sizeof(path_len), 0) != sizeof(path_len)) {
			sprintf(errmsg, "error with reading path_len from socket:  %s",
					strerror(errno));
			printf("problem with getting path_len to receive path\n");
			return -1;
		}

		printf("ilosc znakow: %d\n", path_len);
		if (path_len == -1)
			break;

		if (recv(sock, filepath, path_len, 0) != path_len) {
			sprintf(errmsg, "error with reading path from socket:  %s",
					strerror(errno));
			return -1;
		}
		filepath[path_len] = 0;
		printf("sciezka: %s\n", filepath);

		inf = NEXT_FILE;
		struct stat sb;
		if (stat(filepath, &sb)) {
			if (errno == ENOTDIR || errno == ENOENT || errno == EFAULT) {
				inf = CONTINUE;
			}
		}
		printf("sending answer %d\n", inf);
		if (send(sock, &inf, sizeof(inf), 0) != sizeof(inf)) {
			printf("error with sending answer command\n");
			return -1;
		}
		if (inf == CONTINUE) {
			time_t serv_ver;
			if (recv(sock, &serv_ver, sizeof(serv_ver), 0)
					!= sizeof(serv_ver)) {
				sprintf(errmsg,
						"error with reading server version from socket:  %s",
						strerror(errno));
				return -1;
			}
			char date[TIME_BUFFER_SIZE];
			printf("wersja serverowa: %s\n", make_str_version(serv_ver, date));
			//mamy wersję
			if (difftime(serv_ver, time_last_checking) < 0) {
				//była pobierana już ostatnia wersja, ale jej nie ma, więc usuwamy
				inf = DELETE_FILE;
				if (send(sock, &inf, sizeof(inf), 0) <= 0) {
					printf("error with sendind DELETE_FILE signal\n");
					return -1;
				} else {
					printf("delete file %s sent.\n", filepath);
				}
			} else {
				//na serwerze jest nowsza wersja - powstała po ostatnim sprawdzaniu
				if (get_file(filepath, serv_ver)) {
					printf("error with getting file %s from server\n",
							filepath);
				}
			}
		}
	} while (1);
	return 0;
}

int synchronize() {
	int ret;
	printf("---------------------------------------------------\n"
			"starting synchronizing local\n"
			"--------------------------------------------------------\n");

	synchronize_and_send_local_directory("./");
	printf("---------------------------------------------------\n"
			"local dir sunchronized\n"
			"--------------------------------------------------------\n");
	ret = synchonize_and_download_virtual_directory();
	printf("---------------------------------------------------\n"
			"virtual dir sunchronized %s\n"
			"--------------------------------------------------------\n",
			((ret != 0) ? errmsg : "without errrors"));
	time_last_checking = time(NULL);
	return 0;
}

//poboczne procedury, niezbyt istotne dla funkcjonalności, implementowane poniżej main
int parse_arguments(char **args, uint16_t *port, char *address, char *path);
int set_work_directory(char *path);
void handler(int arg);
int prepare_data();

int main(int argc, char **argv) {

	if (argc != 4) {
		printf("Illegal number of arguments, should be\n\tport "
				"serwer_address sync_directory.\n");
		return -1;
	}

	prepare_data();

	uint16_t port;
	char *address = (char*) malloc(30 * sizeof(char));
	char path[MAXPATHLEN];
	if (parse_arguments(argv, &port, address, path)) {
		printf("Error with parsing arguments: %s.\n", errmsg);
		return -1;
	}

	if (set_work_directory(path)) {
		printf("Error with setting directory: %s.\n", errmsg);
		return -1;
	}

	unsigned char *adr = gethostbyname(address)->h_addr_list[0];
	printf("connecting to \"%u.%u.%u.%u\"\n", adr[0], adr[1], adr[2], adr[3]);
	sprintf(address,"%u.%u.%u.%u", adr[0], adr[1], adr[2], adr[3]);
	//address = inet_ntoa(*((struct in_addr*) adr));

	if ((sock = create_and_connect_socket(port, address)) < 0) {
		printf("Error with creating socket, exit: %s\n", errmsg);
		return -1;
	}

	signal(SIGINT, handler);

	int accept;
	if (recv(sock, &accept, sizeof(accept), 0) < 0) {
		sprintf(errmsg, "Error with waiting for accepting connection: %s.",
				strerror(errno));
		close(sock);
		return -1;
	}
	printf("serverrrrrrrrrrr: %d\n",accept);

	request rq;
	rq.type = TEST_CONNECTION;
	while (active) {
		if (send(sock, &rq, sizeof(rq), 0)<=0) {
			perror("error with sending test connection");
		}
		synchronize();
		sleep(30);
	}

	rq.type = LOGOUT;
	send(sock, &rq, sizeof(rq), 0);

	free(address);
	close(sock);
	return 0;
}
;

int parse_arguments(char **args, uint16_t *port, char *address, char *path) {

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
	regex_t reg2;
	if (regcomp(&reg2, "^([a-z]*\.)*[a-z]*$", REG_EXTENDED) != 0) {
		sprintf(errmsg, "Couldn't compile regex for hostname.");
		return -1;
	}
	int ret = -1;
	if ((ret = regexec(&reg2, args[2], 0, NULL, 0))) {
		sprintf(errmsg, "Second argument isn't a hostname ret: %d.", ret);
		return -1;
	}
	strcpy(address, args[2]);
	if (regcomp(&regex, "^\.?(/(([a-zA-Z0-9\.\?!\"@#`~'_=<>:;%&,\$\^\*"
			"\(\)\+\|\{\}])|-)+)+/?$", REG_EXTENDED) != 0) {
		sprintf(errmsg, "Couldn't compile regex for path.");
		return -1;
	}
	if (regexec(&regex, args[3], 0, NULL, 0)) {
		sprintf(errmsg, "Third argument isn't a path.");
		return -1;
	}
	strcpy(path, args[3]);
	return 0;
}

int prepare_data() {
	active = 1;

	return 0;
}

void handler(int arg) {
	active = 0;
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

	if (chdir(path)) {
		sprintf(errmsg, "Cannot set working directory for process: %s",
				strerror(errno));
		return -1;
	}
	cwd_len = strlen(path);
	return 0;
}


/*
 * a simple http web service
 * date: 2009-08-01 15:38:50
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>

#define MY_HTTPD_CONF "myhttpd.conf"

#define BACKLOG 10
#define HOSTLEN 32

#define MAX(a,b) ((a) > (b) ? (a) : (b))

typedef struct _MYHTTPD_CONF {
	int port;
	char root_dir[255];
} MYHTTPD_CONF;

static MYHTTPD_CONF conf = {0};

void sigchld_handler(int s)
{
	while (waitpid(-1, NULL, WNOHANG) > 0);
}

int myhttpd_read_conf(const char *file, MYHTTPD_CONF *conf)
{
	FILE *fp;
	char buf[256];
	int len = 0;
	char *name;
	char *value;

	fp = fopen(file, "r");
	if (fp == NULL) {
		return -1;
	}

	while ((fgets(buf, sizeof(buf), fp)) != NULL) {
		len = strlen(buf);

		if (buf[len - 1 ] == '\n') {
			buf[len - 1] = '\0';
		}

		name  = strtok(buf, "=");
		value = strtok(0, "=");

		if (name && value) {
			if (strcmp(name, "Directory") == 0) {
				strncpy(conf->root_dir, value, sizeof(conf->root_dir));
			} else if (strcmp(name, "Port") == 0) {
				conf->port = atoi(value);
			}
		}
	}

	return 1;
}

int make_server_socket_q(int portnum, int backlog)
{
	struct sockaddr_in saddr;
	int sock_id;
	socklen_t opt = 1;

	sock_id = socket(AF_INET, SOCK_STREAM, 0);
	if (sock_id == -1) {
		perror("call to socket");
		return -1;
	}

	setsockopt(sock_id, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	memset((void *) &saddr, 0, sizeof(saddr));
	saddr.sin_port = htons(portnum);
	saddr.sin_family = AF_INET;
	saddr.sin_addr.s_addr = INADDR_ANY;

	if (bind(sock_id, (struct sockaddr *) &saddr, sizeof(saddr)) != 0) {
		perror("call to bind");
		return -1;
	}

	if (listen(sock_id, backlog) != 0) {
		perror("call to listen");
		return -1;
	} else {
		return sock_id;
	}
}

int make_server_socket(int portnum)
{
	return make_server_socket_q(portnum, BACKLOG);
}

void do_404(const char *item, int fd)
{
	FILE *fp = fdopen(fd, "w");

	fprintf(fp, "HTTP/1.0 404 Not Found\r\n");
	fprintf(fp, "Content-type: text/plain\r\n");
	fprintf(fp, "\r\n");
	fprintf(fp, "The requested URL %s is not found on this server.\r\n", item);
	fclose(fp);
}

void canot_do(int fd)
{
	FILE *fp = fdopen(fd, "w");

	fprintf(fp, "HTTP/1.0 501 Not Implemented\r\n");
	fprintf(fp, "Content-type: text/plain\r\n");
	fprintf(fp, "\r\n");
	fprintf(fp, "That command is not yet implemented\r\n");
	fclose(fp);
}


void header(FILE *fp, const char *content_type)
{
	fprintf(fp, "HTTP/1.0 200 OK\r\n");
	if (content_type) {
		fprintf(fp, "Content-type: %s\r\n", content_type);
	}
}


int not_exist(const char *f)
{
	struct stat info;

	return (stat(f, &info) == -1);
}

int isadir(const char *f)
{
	struct stat st;

	return (stat(f, &st) != 0 && S_ISDIR(st.st_mode));
}

int do_ls(const char *dir, int fd)
{
	FILE *fp;

	fp = fdopen(fd, "w");
	header(fp, "text/plain");
	fprintf(fp, "\r\n");
	fflush(fp);

	dup2(fd, STDOUT_FILENO);		/* bind socket to standar output */
	dup2(fd, STDOUT_FILENO);		/* bind socket to standar error */

	close(fd);
	execlp("ls", "ls", "-l", dir, NULL);	/* exec ls -l cmd */
	perror(dir);
	exit(1);
}

/*
 * skip over all request info until a CRNL is seen
 */
void read_til_crnl(FILE *fp)
{
	char buf[BUFSIZ] = { 0 };
	while (fgets(buf, BUFSIZ, fp) != NULL && strcmp(buf, "\r\n") != 0);
}

/* detect the filename's externsion */
char *file_type(const char *f)
{
	char *cp;

	if ((cp = strrchr(f, '.')) != NULL) {
		return cp + 1;
	}

	return "";
}

int isexec(const char *f)
{
	struct stat st;

	if (stat(f, &st) < 0) {
		perror("stat");
		return -1;
	}

	return st.st_mode & S_IEXEC;
}

/* execute cmd */
int do_exec(char *prog, int fd)
{
	FILE *fp;

	fp = fdopen(fd, "w");
	header(fp, NULL);
	fflush(fp);
	dup2(fd, STDOUT_FILENO);
	dup2(fd, STDERR_FILENO);
	close(fd);

	execl(prog, prog, NULL);
	perror(prog);

	return 0;
}

/* view the file's content */
int do_cat(const char *f, int fd)
{
	char *extension = file_type(f);
	char *content = "text/plain";
	FILE *fpsock = NULL;
	FILE *fpfile = NULL;
	int c;

	if (strcasecmp(extension, "html") == 0)
		content = "text/html";
	else if (strcasecmp(extension, "gif") == 0)
		content = "image/gif";
	else if (strcasecmp(extension, "jpg") == 0)
		content = "image/jpg";
	else if (strcasecmp(extension, "jpeg") == 0)
		content = "image/jpeg";

	fpsock = fdopen(fd, "w");
	fpfile = fopen(f, "r");
	if (fpsock != NULL && fpfile != NULL) {
		header(fpsock, content);
		fprintf(fpsock, "\r\n");

		while ((c = getc(fpfile)) != EOF) {
			putc(c, fpsock);
		}

		fclose(fpfile);
		fclose(fpsock);
	}

	exit(0);
}

void process_rq(char *rq, int fd)
{
	char cmd[BUFSIZ] = {0};
	char arg[BUFSIZ] = {0};

	if (fork() != 0) 	/* if is child pid, continue. */
		return; 		/* if is parent pid, return. */

	strcpy(arg, conf.root_dir);

	if (sscanf(rq, "%s %s", cmd, arg + strlen(conf.root_dir)) != 2)
		return;

	printf("arg == %s\n", arg);

	if (strcmp(cmd, "GET") != 0)
		canot_do(fd);
	else if (not_exist(arg))
		do_404(arg, fd);
	else if (isadir(arg))
		do_ls(arg, fd);
	else if (isexec(arg))
		do_exec(arg, fd);
	else
		do_cat(arg, fd);
}

int main(int argc, char *argv[])
{
	int sock, new_sock;
	struct sockaddr_in pin;
	socklen_t addrlen;
	struct sigaction sa;
	int max_fd;
	FILE *fpin;
	fd_set rfds;

	char request[BUFSIZ] = { 0 };
	
	if (myhttpd_read_conf(MY_HTTPD_CONF, &conf) < 0) {
		fprintf(stderr, "read %s config file failed\n", MY_HTTPD_CONF);
		return -1;
	}

	sock = make_server_socket(conf.port);
	if (sock == -1) {
		perror("make socket");
		exit(2);
	}

	FD_ZERO(&rfds);
	FD_SET(sock, &rfds);
	max_fd = sock;

	sa.sa_handler = sigchld_handler; // reap all dead processes
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGCHLD, &sa, NULL) == -1) {
		perror("sigaction");
		exit(1);
	}

	while (1) {
		int fd;
		fd_set r;

		/* make local copy read rfds */
		memcpy(&r, &rfds, sizeof(fd_set));

		select(max_fd + 1, &r, NULL, NULL, NULL);

		if (FD_ISSET(sock, &r)) {
			new_sock = accept(sock, (struct sockaddr*)&pin, &addrlen);
			printf("server: got connection from %s\n", inet_ntoa(pin.sin_addr));
			FD_SET(new_sock, &rfds);
			max_fd = MAX(max_fd, new_sock);
		}

		for (fd = sock + 1; fd < max_fd + 1; ++fd) {
			if (FD_ISSET(fd, &r)) {
				fpin = fdopen(fd, "r");

				fgets(request, BUFSIZ, fpin);
				printf("got a call: request = %s", request);
				read_til_crnl(fpin); 
				process_rq(request, fd);
				fclose(fpin);
			}
		}
	}

	return 0;
}


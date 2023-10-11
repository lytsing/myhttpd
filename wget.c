/* vi: set sw=4 ts=4: */
/*
 * wget - retrieve a file using HTTP or FTP
 *
 * Chip Rosenthal Covad Communications <chip@laserlink.net>
 *
 */

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ioctl.h>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

struct host_info {
	char *host;
	int port;
	char *path;
	char *user;
};

static void parse_url(char *url, struct host_info *h);
static struct sockaddr_in *lookup_host(char *host);
static FILE *open_socket(struct sockaddr_in *s_in, int port);
static char *gethdr(char *buf, size_t bufsiz, FILE *fp, int *istrunc);
static char *get_last_path_component(char *path);

/* Globals (can be accessed from signal handlers */
static off_t filesize = 0;		/* content-length of the file */
static int chunked = 0;			/* chunked transfer encoding */

void show_usage(void)
{
	printf("usage : ./wget url\n");
}

void chomp(char *s)
{
	if (!(s && *s)) return;
	while (*s && (*s != '\n')) s++;
	*s = 0;
}

/* Find out if the last character of a string matches the one given.
 *  * Don't underrun the buffer if the string length is 0.
 *   */
char* last_char_is(const char *s, int c)
{
	if (s && *s) {
		size_t sz = strlen(s) - 1;
		s += sz;
		if ( (unsigned char)*s == c)
			return (char*)s;
	}
	return NULL;
}


static void close_and_delete_outfile(FILE* output, char *fname_out, int do_continue)
{
	if (output != stdout && do_continue==0) {
		fclose(output);
		unlink(fname_out);
	}
}

/* Read NMEMB elements of SIZE bytes into PTR from STREAM.  Returns the
 * number of elements read, and a short count if an eof or non-interrupt
 * error is encountered.  */
static size_t safe_fread(void *ptr, size_t size, size_t nmemb, FILE *stream)
{
	size_t ret = 0;

	do {
		clearerr(stream);
		ret += fread((char *)ptr + (ret * size), size, nmemb - ret, stream);
	} while (ret < nmemb && ferror(stream) && errno == EINTR);

	return ret;
}

/* Write NMEMB elements of SIZE bytes from PTR to STREAM.  Returns the
 * number of elements written, and a short count if an eof or non-interrupt
 * error is encountered.  */
static size_t safe_fwrite(void *ptr, size_t size, size_t nmemb, FILE *stream)
{
	size_t ret = 0;

	do {
		clearerr(stream);
		ret += fwrite((char *)ptr + (ret * size), size, nmemb - ret, stream);
	} while (ret < nmemb && ferror(stream) && errno == EINTR);

	return ret;
}

/* Read a line or SIZE - 1 bytes into S, whichever is less, from STREAM.
 * Returns S, or NULL if an eof or non-interrupt error is encountered.  */
static char *safe_fgets(char *s, int size, FILE *stream)
{
	char *ret;

	do {
		clearerr(stream);
		ret = fgets(s, size, stream);
	} while (ret == NULL && ferror(stream) && errno == EINTR);

	return ret;
}

#define close_delete_and_die(s...) { \
	close_and_delete_outfile(output, fname_out, do_continue); \
	printf(s); }



int main(int argc, char **argv)
{
	int n, try=5, status;
	char *s, buf[512];
	struct stat sbuf;
	char extra_headers[1024];
	int extra_headers_left = sizeof(extra_headers);
	struct host_info server, target;
	struct sockaddr_in *s_in;

	FILE *sfp = NULL;			/* socket to web/ftp server			*/
	FILE *dfp = NULL;			/* socket to ftp server (data)		*/
	char *fname_out = NULL;		/* where to direct output (-O)		*/
	int do_continue = 0;		/* continue a prev transfer (-c)	*/
	long beg_range = 0L;		/*   range at which continue begins	*/
	int got_clen = 0;			/* got content-length: from server	*/
	FILE *output;				/* socket to web server				*/

	if (argc != 2) {
		show_usage();
		return -1;
	}

	parse_url(argv[1], &target);
	server.host = target.host;
	server.port = target.port;


	/* Guess an output filename */
	if (!fname_out) {
		fname_out =
			get_last_path_component(target.path);
		if (fname_out == NULL || strlen(fname_out) < 1) {
			fname_out =
				"index.html";
		}
	}
	if (do_continue && !fname_out)
		printf("cannot specify continue (-c) without a filename (-O)");


	/*
	 * Open the output file stream.
	 */
	if (strcmp(fname_out, "-") == 0) {
		output = stdout;
	} else {
		output = fopen(fname_out, (do_continue ? "a" : "w"));
	}

	/*
	 * Determine where to start transfer.
	 */
	if (do_continue) {
		if (fstat(fileno(output), &sbuf) < 0)
			printf("fstat()");
		if (sbuf.st_size > 0)
			beg_range = sbuf.st_size;
		else
			do_continue = 0;
	}

	s_in = lookup_host (server.host);

	if (1) {
		/*
		 *  HTTP session
		 */
		do {
			if (! --try)
				close_delete_and_die("too many redirections");

			/*
			 * Open socket to http server
			 */
			if (sfp) fclose(sfp);
			sfp = open_socket(s_in, server.port);

			/*
			 * Send HTTP request.
			 */
			fprintf(sfp, "GET /%s HTTP/1.1\r\n", target.path);

			fprintf(sfp, "Host: %s\r\nUser-Agent: Wget\r\n", target.host);


			if (do_continue)
				fprintf(sfp, "Range: bytes=%ld-\r\n", beg_range);
			if(extra_headers_left < sizeof(extra_headers))
				fputs(extra_headers,sfp);
			fprintf(sfp,"Connection: close\r\n\r\n");

			/*
		 	* Retrieve HTTP response line and check for "200" status code.
		 	*/
read_response:		if (fgets(buf, sizeof(buf), sfp) == NULL)
				close_delete_and_die("no response from server");

			for (s = buf ; *s != '\0' && !isspace(*s) ; ++s)
			;
			for ( ; isspace(*s) ; ++s)
			;
			switch (status = atoi(s)) {
				case 0:
				case 100:
					while (gethdr(buf, sizeof(buf), sfp, &n) != NULL);
					goto read_response;
				case 200:
					if (do_continue && output != stdout)
						output = freopen(fname_out, "w", output);
					do_continue = 0;
					break;
				case 300:	/* redirection */
				case 301:
				case 302:
				case 303:
					break;
				case 206:
					if (do_continue)
						break;
					/*FALLTHRU*/
				default:
					chomp(buf);
					close_delete_and_die("server returned error %d: %s", atoi(s), buf);
			}

			/*
			 * Retrieve HTTP headers.
			 */
			while ((s = gethdr(buf, sizeof(buf), sfp, &n)) != NULL) {
				if (strcasecmp(buf, "content-length") == 0) {
					filesize = atol(s);
					got_clen = 1;
					continue;
				}
				if (strcasecmp(buf, "transfer-encoding") == 0) {
					if (strcasecmp(s, "chunked") == 0) {
						chunked = got_clen = 1;
					} else {
					close_delete_and_die("server wants to do %s transfer encoding", s);
					}
				}
				if (strcasecmp(buf, "location") == 0) {
					if (s[0] == '/')
						target.path = strdup(s+1);
					else {
						parse_url(strdup(s), &target);
						server.host = target.host;
						server.port = target.port;
					}
				}
			}
		} while(status >= 300);

		dfp = sfp;
	}


	/*
	 * Retrieve file
	 */
	if (chunked) {
		fgets(buf, sizeof(buf), dfp);
		filesize = strtol(buf, (char **) NULL, 16);
	}
	do {
		while ((filesize > 0 || !got_clen) && (n = safe_fread(buf, 1, chunked ? (filesize > sizeof(buf) ? sizeof(buf) : filesize) : sizeof(buf), dfp)) > 0) {
		if (safe_fwrite(buf, 1, n, output) != n)
			printf("fwrite");
		if (got_clen)
			filesize -= n;
	}

		if (chunked) {
			safe_fgets(buf, sizeof(buf), dfp); /* This is a newline */
			safe_fgets(buf, sizeof(buf), dfp);
			filesize = strtol(buf, (char **) NULL, 16);
			if (filesize==0) chunked = 0; /* all done! */
		}

	if (n == 0 && ferror(dfp))
		printf("network read error");
	} while (chunked);
	exit(EXIT_SUCCESS);
}


void parse_url(char *url, struct host_info *h)
{
	char *cp, *sp, *up;

	if (strncmp(url, "http://", 7) == 0) {
		h->port = 80;
		h->host = url + 7;
	} else
		printf("not an http or ftp url: %s", url);

	sp = strchr(h->host, '/');
	if (sp != NULL) {
		*sp++ = '\0';
		h->path = sp;
	} else
		h->path = strdup("");

	up = strrchr(h->host, '@');
	if (up != NULL) {
		h->user = h->host;
		*up++ = '\0';
		h->host = up;
	} else
		h->user = NULL;

	cp = strchr(h->host, ':');
	if (cp != NULL) {
		*cp++ = '\0';
		h->port = atoi(cp);
	}

}


static struct sockaddr_in *lookup_host(char *host)
{
	static struct sockaddr_in s_in;
	struct hostent *hp;

	memset(&s_in, 0, sizeof(s_in));
	s_in.sin_family = AF_INET;
	hp = gethostbyname(host);
	memcpy(&s_in.sin_addr, hp->h_addr_list[0], hp->h_length);

	return &s_in;
}


FILE *open_socket(struct sockaddr_in *s_in, int port)
{
	int fd;
	FILE *fp;

	s_in->sin_port = htons(port);

	/*
	 * Get the server onto a stdio stream.
	 */
	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		printf("socket()");
	if (connect(fd, (struct sockaddr *) s_in, sizeof(*s_in)) < 0)
		printf("connect()");
	if ((fp = fdopen(fd, "r+")) == NULL)
		printf("fdopen()");

	return fp;
}


char *gethdr(char *buf, size_t bufsiz, FILE *fp, int *istrunc)
{
	char *s, *hdrval;
	int c;

	*istrunc = 0;

	/* retrieve header line */
	if (fgets(buf, bufsiz, fp) == NULL)
		return NULL;

	/* see if we are at the end of the headers */
	for (s = buf ; *s == '\r' ; ++s)
		;
	if (s[0] == '\n')
		return NULL;

	/* convert the header name to lower case */
	for (s = buf ; isalnum(*s) || *s == '-' ; ++s)
		*s = tolower(*s);

	/* verify we are at the end of the header name */
	if (*s != ':')
		printf("bad header line: %s", buf);

	/* locate the start of the header value */
	for (*s++ = '\0' ; *s == ' ' || *s == '\t' ; ++s)
		;
	hdrval = s;

	/* locate the end of header */
	while (*s != '\0' && *s != '\r' && *s != '\n')
		++s;

	/* end of header found */
	if (*s != '\0') {
		*s = '\0';
		return hdrval;
	}

	/* Rats!  The buffer isn't big enough to hold the entire header value. */
	while (c = getc(fp), c != EOF && c != '\n')
		;
	*istrunc = 1;
	return hdrval;
}

static char *get_last_path_component(char *path)
{
	char *s = path + strlen(path)-1;

	/* strip trailing slashes */
	while (s != path && *s == '/') {
		*s-- = '\0';
	}

	/* find last component */
	s = strrchr(path, '/');
	if (s == NULL || s[1] == '\0')
		return path;
	else
		return s+1;
}


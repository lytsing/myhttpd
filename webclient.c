/**
 *
 * This code will connect to a host and attempt to download
 * the source for an http page.
 *
 * webclient.c written by detour@metalshell.com
 * run: ./webclient <host>
 *
 * http://www.metalshell.com/
 *
 */

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>

#define PROTOCOL      "tcp"
#define SERVICE       "http"
#define GET           "GET / HTTP/1.1\n\n"
#define BUFSIZE       4096
#define HOSTNAME_SIZE 128

int main(int argc, char *argv[]) {
    int sockid;
    int bufsize;
    char host[HOSTNAME_SIZE];
    char buffer[BUFSIZE];
    struct sockaddr_in socketaddr;
    struct hostent *hostaddr;
    struct servent *servaddr;
    struct protoent *protocol;

    if (argc != 2) {
        fprintf(stderr, "Usage:%s hostname \n", argv[0]);
        exit(1);
    }

    strcpy(host, argv[1]);

    /* Resolve the host name */
    if (!(hostaddr = gethostbyname(host))) {
        fprintf(stderr, "Error resolving host.");
        exit(1);
    }

    /* clear and initialize socketaddr */
    memset(&socketaddr, 0, sizeof(socketaddr));
    socketaddr.sin_family = AF_INET;

    /* setup the servent struct using getservbyname */
    servaddr = getservbyname(SERVICE, PROTOCOL);
    socketaddr.sin_port = servaddr->s_port;

    memcpy(&socketaddr.sin_addr, hostaddr->h_addr, hostaddr->h_length);

    /* protocol must be a number when used with socket()
       since we are using tcp protocol->p_proto will be 0 */
    protocol = getprotobyname(PROTOCOL);

    sockid = socket(AF_INET, SOCK_STREAM, protocol->p_proto);
    if (sockid < 0) {
        fprintf(stderr, "Error creating socket.");
        exit(1);
    }

    /* everything is setup, now we connect */
    if (connect(sockid, (struct sockaddr *)&socketaddr, sizeof(socketaddr)) == -1) {
        fprintf(stderr, "Error connecting.");
        exit(1);
    }

    /* send our get request for http */
    if (send(sockid, GET, strlen(GET), 0) == -1) {
        fprintf(stderr, "Error sending data.");
        exit(1);
    }

    /* read the socket until its clear then exit */
    while ( (bufsize = read(sockid, buffer, sizeof(buffer) - 1))) {
        write(1, buffer, bufsize);
    }

    printf("\n");
    close(sockid);

    return 0;
}


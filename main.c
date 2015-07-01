
// Sargis Dudaklyan


#include "csapp.h"
#define PROXY_LOG "proxy.log"

typedef struct {
    int myid;
    int connfd;
    struct sockaddr_in clientaddr;
} arglist_t;

FILE *log_file;
void *process_request(void* vargp);
int parse_uri(char *uri, char *target_addr, char *path, int  *port);
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri, int size);

int main(int argc, char **argv)
{
    int listenfd;
    int port;
    pthread_t tid;
    int clientlen;
    arglist_t *argp = NULL;
    int request_count = 0;
    if (argc == 2){
        port = atoi(argv[1]); // port inputted by user
    }else{
        port = 8080; //default port
    }
    log_file = Fopen(PROXY_LOG, "a"); //open proxy.log and append to it
    listenfd = Open_listenfd(port);   //create a listening descriptor and ready to recieve connection requests
    while (1) {
        argp = (arglist_t *)Malloc(sizeof(arglist_t));
        clientlen = sizeof(argp->clientaddr);
        /*waits for a connection request from a client to arrive on the listening descriptor listenfd, then fills in the client’s socket address in addr, and returns a connected descriptor that can be used to communicate with the client using Unix I/O functions.*/
        argp->connfd = Accept(listenfd, (SA *)&argp->clientaddr, (socklen_t *) &clientlen); /*IGH*/
        argp->myid = request_count++;
        //process_request(argp); // Use without thread, but results in slow connection
        pthread_create(&tid, NULL, process_request, argp);
    }
}


void *process_request(void *vargp)
{
    arglist_t arglist;
    struct sockaddr_in clientaddr;
    int connfd;
    int serverfd;
    char *request;
    char *request_uri;
    char *request_uri_end;
    char *rest_of_request;
    int request_len;
    int response_len;
    int i, n;
    int realloc_factor;
    char hostname[MAXLINE];
    char pathname[MAXLINE];
    int port;
    char log_entry[MAXLINE];
    rio_t rio;
    char buf[MAXLINE];
    arglist = *((arglist_t *)vargp);
    connfd = arglist.connfd;
    clientaddr = arglist.clientaddr;
    Free(vargp);
    request = (char *)Malloc(MAXLINE);
    request[0] = '\0';
    realloc_factor = 2;
    request_len = 0;
    Rio_readinitb(&rio, connfd);
    while (1) {
        if ((n = rio_readlineb(&rio, buf, MAXLINE)) <= 0) {
            close(connfd);
            free(request);
            return NULL;
        }
        if (request_len + n + 1 > MAXLINE)
            Realloc(request, MAXLINE*realloc_factor++);
        strcat(request, buf);
        request_len += n;
        if (strcmp(buf, "\r\n") == 0)
            break;
    }
    request_uri = request + 4;
    request_uri_end = NULL;
    for (i = 0; i < request_len; i++) {
        if (request_uri[i] == ' ') {
            request_uri[i] = '\0';
            request_uri_end = &request_uri[i];
            break;
        }
    }
    if ( i == request_len ) {
        printf("process_request: Couldn't find the end of the URI\n");
        close(connfd);
        free(request);
        return NULL;
    }
    if (strncmp(request_uri_end + 1, "HTTP/1.0\r\n", strlen("HTTP/1.0\r\n")) &&
        strncmp(request_uri_end + 1, "HTTP/1.1\r\n", strlen("HTTP/1.1\r\n"))) {
        printf("process_request: client issued a bad request (4).\n");
        close(connfd);
        free(request);
        return NULL;
    }
    rest_of_request = request_uri_end + strlen("HTTP/1.0\r\n") + 1;
    if (parse_uri(request_uri, hostname, pathname, &port) < 0) {
        printf("process_request: cannot parse uri\n");
        close(connfd);
        free(request);
        return NULL;
    }
    /*
     * Forward the request to the end server
establishes a connection with a server running on host hostname and listening for connection requests on the well-known port port. It returns an open socket descriptor that is ready for input and output using the Unix I/O functions     */
    if ((serverfd = open_clientfd(hostname, port)) < 0) {
        printf("process_request: Unable to connect to end server.\n");
        free(request);
        return NULL;
    }
    rio_writen(serverfd, "GET /", strlen("GET /"));
    rio_writen(serverfd, pathname, strlen(pathname));
    rio_writen(serverfd, " HTTP/1.0\r\n", strlen(" HTTP/1.0\r\n"));
    rio_writen(serverfd, rest_of_request, strlen(rest_of_request));
    Rio_readinitb(&rio, serverfd);
    response_len = 0;
    while( (n = rio_readn(serverfd, buf, MAXLINE)) > 0 ) {
        response_len += n;
        rio_writen(connfd, buf, n);

        bzero(buf, MAXLINE);
    }
    format_log_entry(log_entry, &clientaddr, request_uri, response_len);
    fprintf(log_file, "%s %d\n", log_entry, response_len);
    fflush(log_file);
    close(connfd);
    close(serverfd);
    free(request);
    return NULL;
}

int parse_uri(char *uri, char *hostname, char *pathname, int *port)
{
    char *hostbegin;
    char *hostend;
    char *pathbegin;
    int len;
    if (strncasecmp(uri, "http://", 7) != 0) {
        hostname[0] = '\0';
        return -1;
    }
    hostbegin = uri + 7;
    hostend = strpbrk(hostbegin, " :/\r\n\0");
    len = hostend - hostbegin;
    strncpy(hostname, hostbegin, len);
    hostname[len] = '\0';
    *port = 80;
    if (*hostend == ':')
        *port = atoi(hostend + 1);
    pathbegin = strchr(hostbegin, '/');
    if (pathbegin == NULL) {
        pathname[0] = '\0';
    }
    else {
        pathbegin++;
        strcpy(pathname, pathbegin);
    }
    return 0;
}

void format_log_entry(char *logstring, struct sockaddr_in *sockaddr,
                      char *uri, int size)
{
    time_t now;
    char time_str[MAXLINE];
    unsigned long host;
    unsigned char a, b, c, d;
    now = time(NULL);
    strftime(time_str, MAXLINE, "%a %d %b %Y %H:%M:%S %Z", localtime(&now));
    host = ntohl(sockaddr->sin_addr.s_addr);
    a = host >> 24;
    b = (host >> 16) & 0xff;
    c = (host >> 8) & 0xff;
    d = host & 0xff;
    sprintf(logstring, "%s: %d.%d.%d.%d %s", time_str, a, b, c, d, uri);
}


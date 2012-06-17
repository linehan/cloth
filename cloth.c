#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/param.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "textutils.h"
#include "http_status.h"

/*
 * accept() makes use of the restrict keyword 
 * introduced in C99. 
 */
#ifdef USE_RESTRICT
#else
#define restrict
#endif


/* For static buffers */
#define BUFSIZE 8096
#define LOGBUF 256
/* Default path of the log file (relative to -d) */
#define LOG_PATH "cloth.log"
/* Default path of procinfo file (relative to -d) */
#define INFO_PATH "cloth.info"
/* The strftime() format string for common log time. */
#define COMMON_LOG_TIME "%d/%b/%Y:%H:%M:%S %z"

/* Message printed on illegal argument usage. */
#define HELP_MESSAGE "usage: cloth <PORT> <WWW-DIRECTORY>\n"
/* Type of message to be printed in the log. */ 
/*enum log_genre { OOPS=42, WARN, INFO };*/



/*
 * Non-allowed directories. The program will abort with 
 * an error message if any of these are passed as an 
 * argument to the -d flag.
 */
static const 
char *bad_dir[]={"/","/etc","/bin","/lib","/tmp","/usr","/dev","/sbin",NULL};


/*
 * Supported filetypes and extensions. If an HTTP 
 * request asks for anything not on this list, the 
 * request will be greeted with an error message.
 */
struct ext_t { char *ext; char *filetype; };
struct ext_t supported_ext[]={ 
        {"gif", "image/gif" },
	{"jpg", "image/jpeg"}, 
	{"jpeg","image/jpeg"},
	{"png", "image/png" },  
	{"zip", "image/zip" },  
	{"gz",  "image/gz"  },  
	{"tar", "image/tar" },  
	{"htm", "text/html" },  
	{"html","text/html" },  
	{"css", "text/css"  },  
	{0,0} 
};


/* Buffer to store argument to -d parameter. */
char www_path[BUFSIZE];



/****************************************************************************** 
 * LOGS
 * Implements the common log format and standard log time format.
 ******************************************************************************/
/**
 * A log entry consists of the 7-tuple 
 *
 *      { code, file, local-addr, action, remote-addr, time, message } 
 *
 * code: the type of log entry, one of
 *      START - the server is starting up
 *      STOP  - the server is shutting down
 *      INFO  - standard entry which indicates an action
 *      WARN  - an action has failed
 *      OUCH  - an unrecoverable error has occured
 *
 * file: the filename of the source being requested
 *
 * local-addr: the address or domain name specified by the remote host, viz.
 *      myserver.mydomain.org or 42.112.5.25.some.isp.net. Because different
 *      names may resolve to the same address, this field will specifically
 *      reflect the label that the *remote host* is using to contact your
 *      server.
 *
 * action: the nature of the interaction between the local and remote hosts,
 *      being one of the 9 HTTP methods ("verbs"), or else a response verb
 *      emitted by the server. The action may be printed by name or in 
 *      symbolic fashion.
 *
 *      HEAD    - Ask for response (metadata) without response body
 *      GET     - Request a representation of the specified source
 *      POST    - Submits data to be processed
 *      PUT     - Uploads a representation of the specified resource
 *      DELETE  - Deletes the specified resource
 *      TRACE   - Echoes back the received request
 *      OPTIONS - Check if the server supports a specific request
 *      CONNECT - Converts the connection to a transparent TCP/IP tunnel
 *      PATCH   - Apply partial modifications to a resource
 *      
 *      Cloth only supports the GET routine, for simplicity, and a successful
 *      response is marked SEND. 
 *
 *      Alternate representations
 *
 *      GET     <---
 *      SEND    --->
 *      WARN    !---
 *              ---!
 *      OUCH    x--x
 *
 * remote-addr: similar to the local address, except for the remote host
 *
 * time: The time at which the request was processed by the server
 *      Time is formatted in ISO format: yyyy-mm-dd HH:mm:ss
 *
 * message: An optional plaintext message, e.g. explaining an error context or
 *      specifying the user agent of the remote host
 *
 * An example log snippet:
 *
 *      INFO index.html cloth.homeunix.org:80 <--- 22.85.117.2:34205 (2012-06-17 06:49:58) "Mozilla/5.0 (Windows; U; Windows..."
 *      INFO index.html cloth.homeunix.org:80 ---> 22.85.117.2:34206 (2012-06-17 06:49:59)
 *      INFO jindex.baz cloth.homeunix.org:80 <--- 22.85.117.2:31102 (2012-06-17 06:50:01) "Mozilla/5.0 (Windows; U; Windows..."
 *      WARN jindex.baz cloth.homeunix.org:80 ---! 22.85.117.2:31102 (2012-06-17 06:50:02) "Extension type not supported"
 */


struct session_t {
        int  socket;        /* File descriptor of the socket */
        char *host;         /* Hostname the remote end wants to connect to */
        char *agent;        /* Remote user-agent id */
        char *resource;     /* Resource (file) being requested */
        char *remote_addr;  /* Address of the remote host */
        short remote_port;  /* Port of the remote host */
};


/** 
 * http_info -- Parse an HTTP request and identify relevant information
 * @request: the HTTP request buffer
 * @entry  : the log_entry struct to be (partially) filled out
 *
 * Determines the following:
 *      0. The resource being requested
 *      1. The hostname targeted by the remote agent 
 *      2. The remote User-Agent ID
 *
 */
void http_info(struct session_t *session, char *request)
{
        static char req_copy[BUFSIZE];
        char *token;
        char *buf;

        bwipe(req_copy);
        strcpy(req_copy, request);

        /* 
         * Search tokens are truncated before being placed in the struct.
         * See field() in textutils.h for details.
         */
        for (token  = strtok(req_copy, "**"); 
             token != NULL; 
             token  = strtok(NULL, "**")) 
        {
                if (buf = field(token, "GET "), buf != NULL)
                        /* e.g. 'GET /index.html' */
                        pumpf(&session->resource, "%s", buf);

                if (buf = field(token, "Host: "), buf != NULL)
                        /* e.g. 'Host: www.something.com' */
                        pumpf(&session->host, "%s", buf);

                if (buf = field(token, "User-Agent: "), buf != NULL)
                        /* e.g. 'User-Agent: Mozilla/3.0 ...' */
                        pumpf(&session->agent, "%s", buf);
        }
}


/**
 * addr_info -- fill out the remote host information in the session struct
 * @session: pointer to a session struct
 * @remote : pointer to a copy of the remote sockaddr_in 
 */
void addr_info(struct session_t *session, struct sockaddr_in *remote) 
{
        if (!remote)
                return;

        session->remote_addr = inet_ntoa(remote->sin_addr);
        session->remote_port = ntohs(remote->sin_port);
}


/**
 * session_info -- fill out the session structure
 * @session: pointer to a session struct
 * @socket : file descriptor of active socket
 * @remote : sockaddr of remote client
 * @request: HTTP request
 */
void session_info(struct session_t *session, int socket, struct sockaddr_in *remote, char *request)
{
        http_info(session, request);
        addr_info(session, remote);
        session->socket = socket;
}



void time_info(char *buffer, time_t time)
{
        #define TIME_FORMAT "%Y-%m-%d %H:%M:%S"
        strftime(buffer, LOGBUF, TIME_FORMAT, gmtime(&time));
}



char *new_entry(struct http_status *status, struct session_t *session)
{
        char *timebuf;
        char *buffer;

        timebuf = malloc(LOGBUF * sizeof(char));
        time_info(timebuf, time(NULL));

        pumpf(&buffer, "%s %s %s %s:%hd (%s)",
                session->resource,
                session->host,
                status->figure,
                session->remote_addr,
                session->remote_port,
                timebuf);

        free(timebuf);

        return buffer;
}


void log(int code, struct session_t *session, char *msg)
{
        struct http_status *status;
        char *entry;
	char *buf;
	int fd;

        status = &STATUS[code];

        if (status->code != OUCH)
                entry = new_entry(status, session);

	switch (status->code) 
        {
	case OUCH: 
                pumpf(&buf, "OUCH: \"%s\" (%d)", msg, errno); 
                break;
        case WARN:
                /* Write over socket */
		pumpf(&buf, "cloth says: %hd %s\r", status->code, msg);
		write(session->socket, buf, strlen(buf));
                /* Write to log */
		pumpf(&buf, "WARN: %s \"%hd\"", entry, status->http); 
		break;
	case INFO: 
                pumpf(&buf, "INFO: %s \"%s\"", entry, msg);
                break;
	}	
        
        /* Write the log buffer to the log file */
	if ((fd = open(LOG_PATH, O_CREAT| O_WRONLY | O_APPEND, 0644)) >= 0) {
		write(fd, buf, strlen(buf)); 
		write(fd, "\n", 1);      
		close(fd);
	}

        if (status->code != OUCH)
                free(entry);

        free(buf);
        
	if (status->code == OUCH || status->code == WARN) 
                exit(3);
}


/****************************************************************************** 
 * HTTP 
 * The main functions called by the child process when a request is made
 * on the socket being listened to by the server.
 ******************************************************************************/
/** 
 * Work out the file type and check we support it 
 */
inline char *get_file_extension(char *buf, size_t buflen)
{
        size_t len;
        int i;

	for (i=0; supported_ext[i].ext != NULL; i++) {
		len = strlen(supported_ext[i].ext);
		if (!strncmp(&buf[buflen-len], supported_ext[i].ext, len)) {
			return supported_ext[i].filetype;
		}
	}
        return NULL;
}


struct sockaddr_in *copyaddr(struct sockaddr_in *addr)
{
        struct sockaddr_in *new;

        new = calloc(1, sizeof(struct sockaddr_in));

        new->sin_family = addr->sin_family;
        new->sin_port   = addr->sin_port;
        new->sin_addr   = addr->sin_addr;

        return new;
}

/**
 * web -- child web process that gets forked (so we can exit on error)
 * @fd : socket file descriptor 
 * @hit: request count 
 */
void web(int fd_socket, struct sockaddr_in *remote, int hit)
{
        struct session_t sess;
	static char request[BUFSIZE];
        char *buf;
        int fd_file;
	char *fstr;
        long ret;

        /********************************************** 
         * Receive a new request                      *
         **********************************************/
        /* Read the request from the socket into the buffer */
	if (ret = read(fd_socket, request, BUFSIZE), ret <= 0 || ret >= BUFSIZE)
		log(BAD_REQUEST, &sess, "");

        /* Nul-terminate the buffer. */
	request[ret] = '\0'; 

        /* Replace CR and/or NL with '*' delimiter */
	for (buf = request; *buf; buf++) {
		if (*buf=='\r' || *buf=='\n') 
                        *buf = '*';
        }

        session_info(&sess, fd_socket, remote, request);

	log(ACCEPT, &sess, "");


        /********************************************** 
         * Verify that the request is legal           *
         **********************************************/
        /* Only the GET operation is allowed */
	if (strncmp(request, "GET ", 4) && strncmp(request, "get ", 4))
		log(BAD_METHOD, &sess, "Only GET supported");

        /* Truncate the request after the filename being requested */
        for (buf=&request[4]; *buf; buf++) {
                if (*buf == ' ') {*buf = '\0'; break;};
        }

        /* Catch any illegal relative pathnames (..) */
        if (strstr(request, ".."))
                log(BAD_REQUEST, &sess, "Relative paths not supported");

        /* In the absence of an explicit filename, default to index.html */
        if (!strncmp(request, "GET /\0", 6) || !strncmp(request, "get /\0", 6))
		strcpy(request, "GET /index.html");

        /* Scan for filename extensions and check against valid ones. */
        if (fstr = get_file_extension(request, strlen(request)), fstr == NULL)
                log(NO_METHOD, &sess, "file extension not supported");

        /* Open the requested file */
	if ((fd_file = open(&request[5], O_RDONLY)) == -1)
		log(ERROR, &sess, "failed to open file");

	log(RESPONSE, &sess, "");


        /********************************************** 
         * Write the HTTP response to the socket      *
         **********************************************/
        /* 
         * Format and print the HTTP response to the buffer, then write the 
         * buffer contents to the socket.
         */ 
	sprintf(request, "HTTP/1.0 200 OK\r\nContent-Type: %s\r\n\r\n", fstr);
	write(fd_socket, request, strlen(request));

	/* 
         * Write the file to the socket in blocks of 8KB (last block may 
         * be smaller) 
         */
	while ((ret = read(fd_file, request, BUFSIZE)) > 0) {
		write(fd_socket, request, ret);
	}

        free(remote);

        #ifdef LINUX
	sleep(1);	/* to allow socket to drain */
        #endif
	exit(1);
}


/**
 * cloth -- the main loop that establishes a socket and listens for requests
 * @www : the www directory to serve files from
 * @port: the port number
 */
void cloth(int port)
{
	static struct sockaddr_in client_addr; 
	static struct sockaddr_in server_addr;
	socklen_t length;
        int fd_socket;
        int fd_listen;
        int hit;
        int pid;
        int i;

        /********************************************** 
         * Prepare the process to run as a daemon     *
         **********************************************/
        for (i=0; i<NOFILE; i++) /* Close files inherited from parent */
                close(i);

        umask(0);                /* Reset file access creation mask */
	signal(SIGCLD, SIG_IGN); /* Ignore child death */
	signal(SIGHUP, SIG_IGN); /* Ignore terminal hangups */
	setpgrp();               /* Create new process group */

        /*log(INFO, 0, "cloth is starting up...", "", getpid());*/

        /**********************************************
         * Establish the server side of the socket    *
         **********************************************/
	server_addr.sin_family      = AF_INET;
	server_addr.sin_port        = htons(port);
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	/* Initialize the socket */
	if ((fd_listen = socket(AF_INET, SOCK_STREAM, 0)) < 0)
                log(FATAL, NULL, "socket");

        /* Attempt to bind the server's address to the socket */
	if (bind(fd_listen, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
                log(FATAL, NULL, "bind");

        /* Attempt to listen on the socket */
	if (listen(fd_listen, 64) < 0)
                log(FATAL, NULL, "listen");


        /**********************************************
         * Loop forever, listening on the socket      *
         **********************************************/
	for (hit=1; ; hit++) {
		length = sizeof(client_addr);

                /* Attempt to accept on socket */
		if ((fd_socket = accept(fd_listen, (struct sockaddr *)&client_addr, &length)) < 0)
                        log(FATAL, NULL, "accept");

                /* Fork a new process to handle the request */
		if ((pid = fork()) < 0)
                        log(FATAL, NULL, "fork");

                /* Child */
                if (pid == 0) {
                        close(fd_listen);
                        web(fd_socket, copyaddr(&client_addr), hit); /* never returns */
                /* Parent */
                } else { 
                        close(fd_socket);
                }
	}
}


/****************************************************************************** 
 * MAIN
 * The start of the program, which creates a socket for the requested port
 * and then proceeds to listen on it for requests. It will serve the file
 * parameter given at program start if a valid request is received.
 ******************************************************************************/
/**
 * main -- start the server, check the args, and fork into the background 
 */
int main(int argc, char **argv)
{
        #define DEFAULT_PORT 55555
        #define MAX_PORT     60000
        int port;
        int ch;
        int i;

        port = DEFAULT_PORT; 

        /* Check that all required arguments have been supplied */
        while ((ch = getopt(argc, argv, "p:d:?")) != -1) {
                switch (ch) {
                case 'p':
                        port = atoi(optarg);
                        break;
                case 'd':
                        sprintf(www_path, "%s", optarg);
                        break;
                case '?':
                        printf("%s", HELP_MESSAGE);
                        exit(1);
                default:
                        printf("%s", HELP_MESSAGE);
                        exit(1);
                }
        }

        /* Check that www directory is legal */
        for (i=0; bad_dir[i] != NULL; i++) {
                if (!strncmp(www_path, bad_dir[i], strlen(bad_dir[i])+1)) {
		        printf("ERROR: Bad www directory %s\n", www_path);
		        exit(3);
                }
        }

        /* Change working directory to the one provided by the caller */
	if (chdir(www_path) == -1) { 
		printf("ERROR: Can't change to directory %s\n", www_path);
		exit(4);
	}

        /* Ensure that the port number is legal */
	if (port < 0 || port > MAX_PORT) {
		printf("ERROR: Invalid port number %d (> 60000)", port);
                exit(3);
        }

	/* 
         * Fork the process. The child will enter cloth() and be daemonized
         * while the parent returns 0 to the shell. 
         */
	if (fork() == 0)
                cloth(port);

        return 0;
}


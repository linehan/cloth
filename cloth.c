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
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "textutils.h"
#include "log.h"

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


/* Message printed on illegal argument usage. */
#define HELP_MESSAGE "usage: cloth <PORT> <WWW-DIRECTORY>\n"


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
 * HELPERS 
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


/**
 * copyaddr -- allocate and return a copy of a sockaddr_in structure
 */
inline struct sockaddr_in *copyaddr(struct sockaddr_in *addr)
{
        struct sockaddr_in *new;

        new = calloc(1, sizeof(struct sockaddr_in));

        new->sin_family = addr->sin_family;
        new->sin_port   = addr->sin_port;
        new->sin_addr   = addr->sin_addr;

        return new;
}


/****************************************************************************** 
 * HTTP 
 * The main functions called by the child process when a request is made
 * on the socket being listened to by the server.
 ******************************************************************************/
/**
 * web -- child web process that gets forked (so we can exit on error)
 * @fd : socket file descriptor 
 * @hit: request count 
 */
void web(int fd_socket, struct sockaddr_in *remote, int hit)
{
        struct ses_t session;
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
		log(BAD_REQUEST, &session, "");

        /* Nul-terminate the buffer. */
	request[ret] = '\0'; 

        /* Replace CR and/or NL with '*' delimiter */
	for (buf = request; *buf; buf++) {
		if (*buf=='\r' || *buf=='\n') 
                        *buf = '*';
        }

        sesinfo(&session, fd_socket, remote, request);

	log(ACCEPT, &session, "");


        /********************************************** 
         * Verify that the request is legal           *
         **********************************************/
        /* Only the GET operation is allowed */
	if (strncmp(request, "GET ", 4) && strncmp(request, "get ", 4))
		log(BAD_METHOD, &session, "Only GET supported");

        /* Truncate the request after the filename being requested */
        for (buf=&request[4]; *buf; buf++) {
                if (*buf == ' ') {*buf = '\0'; break;};
        }

        /* Catch any illegal relative pathnames (..) */
        if (strstr(request, ".."))
                log(BAD_REQUEST, &session, "Relative paths not supported");

        /* In the absence of an explicit filename, default to index.html */
        if (!strncmp(request, "GET /\0", 6) || !strncmp(request, "get /\0", 6))
		strcpy(request, "GET /index.html");

        /* Scan for filename extensions and check against valid ones. */
        if (fstr = get_file_extension(request, strlen(request)), fstr == NULL)
                log(NO_METHOD, &session, "file extension not supported");

        /* Open the requested file */
	if ((fd_file = open(&request[5], O_RDONLY)) == -1)
		log(ERROR, &session, "failed to open file");

	log(RESPONSE, &session, "");


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
	sleep(1); // allow socket to drain
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


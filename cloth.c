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
/* Default path of the log file (relative to -d) */
#define LOG_PATH "cloth.log"
/* The strftime() format string for common log time. */
#define COMMON_LOG_TIME "%d/%b/%Y:%H:%M:%S %z"
/* Message printed on illegal argument usage. */
#define HELP_MESSAGE "usage: cloth <PORT> <WWW-DIRECTORY>\n"
/* Type of message to be printed in the log. */ 
enum log_genre { OOPS=42, WARN, INFO };


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
 * mklog -- fill a destination buffer with a properly formatted log entry 
 * @buffer: the full request string to be parsed
 * @time  : the current time from time(NULL)
 * @pid   : the process id from getpid()
 */
char *mklog(char *raw, time_t time, pid_t pid)
{
        static char copy[BUFSIZE];
        static char date[BUFSIZE];
        static char host[BUFSIZE];
        static char agent[BUFSIZE];
        static char req[BUFSIZE];
        char *token;
        char *field;
        char *dest;

        dest = malloc(BUFSIZE * sizeof(char)); /* This gets returned */

        strcpy(copy, raw);

        /* Parse the raw buffer, fishing out the important bits */
        for (token  = strtok(copy, "**"); 
             token != NULL; 
             token  = strtok(NULL, "**")) {

                if (field = strstr(token, "GET"), field != NULL)
                        sprintf(req, "%s", field);

                if (field = strstr(token, "Host:"), field != NULL)
                        sprintf(host, "%s", &field[6]);

                if (field = strstr(token, "User-Agent:"), field != NULL)
                        sprintf(agent, "%s", &field[12]);
        }

        /* Print the formatted date string */
        strftime(date, BUFSIZE, COMMON_LOG_TIME, gmtime(&time));

        /* Print all of this into the dest buffer */
        sprintf(dest, "%s %s [%s] \"%s\" %d -", host, agent, date, req, pid);

        return dest;
}


/**
 * log -- prints server status and messages to the log file 
 * @genre: what sort of log entry (see enum log_genre) 
 * @s1   : message 1
 * @s2   : message 2
 * @num  : errorno
 */
void log(enum log_genre genre, char *msg, char *raw, int socket)
{
	char buf[BUFSIZE*2];
        char *log_entry;
	int fd;

        log_entry = mklog(raw, time(NULL), getpid());

	switch (genre) 
        {
	case OOPS: 
                sprintf(buf, "OOPS: %s ERRNO %d", msg, errno); 
                break;
	case WARN: 
		sprintf(buf, "cloth says: %s\r", msg);
		write(socket, buf, strlen(buf));
		sprintf(buf, "WARN: %s %s", msg, log_entry); 
		break;
	case INFO: 
                sprintf(buf, "INFO: %s %s", msg, log_entry);
                break;
	}	
        
        /* Write the log buffer to the log file */
	if ((fd = open(LOG_PATH, O_CREAT| O_WRONLY | O_APPEND, 0644)) >= 0) {
		write(fd, buf, strlen(buf)); 
		write(fd, "\n", 1);      
		close(fd);
	}

        free(log_entry);

	if (genre == OOPS || genre == WARN) 
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


/**
 * web -- child web process that gets forked (so we can exit on error)
 * @fd : socket file descriptor 
 * @hit: request count 
 */
void web(int fd_socket, int hit)
{
	static char buffer[BUFSIZE];
        char *buf;
        int fd_file;
	char *fstr;
        long ret;

        /********************************************** 
         * Receive a new request                      *
         **********************************************/
        /* Read the request from the socket into the buffer */
	if (ret = read(fd_socket, buffer, BUFSIZE), ret <= 0 || ret >= BUFSIZE)
		log(WARN, "failed to read browser request", "", fd_socket);

        /* Nul-terminate the buffer. */
	buffer[ret] = '\0'; 

        /* Replace CR and/or NL with '*' delimiter */
	for (buf = buffer; *buf; buf++) {
		if (*buf=='\r' || *buf=='\n') 
                        *buf = '*';
        }

	log(INFO, "request", buffer, hit);


        /********************************************** 
         * Verify that the request is legal           *
         **********************************************/
        /* Only the GET operation is allowed */
	if (strncmp(buffer, "GET ", 4) && strncmp(buffer, "get ", 4))
		log(WARN, "Only GET operation supported", buffer, fd_socket);

        /* Truncate the request after the filename being requested */
        for (buf=&buffer[4]; *buf; buf++) {
                if (*buf == ' ') {*buf = '\0'; break;};
        }

        /* Catch any illegal relative pathnames (..) */
        if (strstr(buffer, ".."))
                log(WARN, "Relative paths not supported", buffer, fd_socket);

        /* In the absence of an explicit filename, default to index.html */
        if (!strncmp(buffer, "GET /\0", 6) || !strncmp(buffer, "get /\0", 6))
		strcpy(buffer, "GET /index.html");

        /* Scan for filename extensions and check against valid ones. */
        if (fstr = get_file_extension(buffer, strlen(buffer)), fstr == NULL)
                log(WARN, "file extension not supported", buffer, fd_socket);

        /* Open the requested file */
	if ((fd_file = open(&buffer[5], O_RDONLY)) == -1)
		log(WARN, "failed to open file", &buffer[5], fd_socket);

	log(INFO, "SEND", &buffer[5], hit);


        /********************************************** 
         * Write the HTTP response to the socket      *
         **********************************************/
        /* 
         * Format and print the HTTP response to the buffer, then write the 
         * buffer contents to the socket.
         */ 
	sprintf(buffer, "HTTP/1.0 200 OK\r\nContent-Type: %s\r\n\r\n", fstr);
	write(fd_socket, buffer, strlen(buffer));

	/* 
         * Write the file to the socket in blocks of 8KB (last block may 
         * be smaller) 
         */
	while ((ret = read(fd_file, buffer, BUFSIZE)) > 0) {
		write(fd_socket, buffer, ret);
	}

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


	log(INFO, "cloth is starting up...", "", getpid());


        /**********************************************
         * Establish the server side of the socket    *
         **********************************************/
	server_addr.sin_family      = AF_INET;
	server_addr.sin_port        = htons(port);
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	/* Initialize the socket */
	if ((fd_listen = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		log(OOPS, "system call", "socket", 0);

        /* Attempt to bind the server's address to the socket */
	if (bind(fd_listen, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
		log(OOPS, "system call", "bind", 0);

        /* Attempt to listen on the socket */
	if (listen(fd_listen, 64) < 0)
		log(OOPS, "system call", "listen", 0);


        /**********************************************
         * Loop forever, listening on the socket      *
         **********************************************/
	for (hit=1; ; hit++) {
		length = sizeof(client_addr);

                /* Attempt to accept on socket */
		if ((fd_socket = accept(fd_listen, (struct sockaddr *)&client_addr, &length)) < 0)
			log(OOPS, "system call", "accept", 0);

                /* Fork a new process to handle the request */
		if ((pid = fork()) < 0)
			log(OOPS, "system call", "fork", 0);

                /* Child */
                if (pid == 0) {
                        close(fd_listen);
                        web(fd_socket, hit); /* never returns */
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


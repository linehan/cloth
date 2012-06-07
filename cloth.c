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
#include <netinet/in.h>
#include <arpa/inet.h>
#include "cloth.h"

#ifdef USE_RESTRICT
#else
#define restrict
#endif

#define LOG_PATH         "cloth.log"
#define COMMON_LOG_TIME  "%d/%b/%Y:%H:%M:%S %z"
#define HELP_MESSAGE     "usage: cloth <PORT> <WWW-DIRECTORY>\n"

char www_path[BUFSIZE];
char log_path[BUFSIZE];
char port_num[BUFSIZE];


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
		sprintf(buf, "<html><body>cloth: %s</body></html>\r", msg);
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
 * WEB 
 * The main function called by the child process when a request is made
 * on the socket being listened to by the server.
 ******************************************************************************/
/** 
 * Work out the file type and check we support it 
 */
inline char *get_file_extension(char *buf, size_t buflen)
{
        size_t len;
        int i;

	for (i=0; extensions[i].ext != NULL; i++) {
		len = strlen(extensions[i].ext);
		if (!strncmp(&buf[buflen-len], extensions[i].ext, len)) {
			return extensions[i].filetype;
		}
	}
        return NULL;
}


/**
 * web -- child web process that gets forked (so we can exit on error)
 * @fd : socket file descriptor 
 * @hit: request count 
 */
void web(int fd, int hit)
{
	static char buffer[BUFSIZE];
        int file_fd;
        int buflen;
	char *fstr;
        long ret;
	long i;

        /* Read a web request, filling 'buffer' with the data at 'fd' */
	if (ret = read(fd, buffer, BUFSIZE), ret == 0 || ret == -1)
		log(WARN, "failed to read browser request", "", fd);

        /* Terminate the buffer if the return code is a valid length. */
	if (ret > 0 && ret < BUFSIZE)
		buffer[ret] = '\0';
	else 
                buffer[0] = '\0';

	buflen = strlen(buffer);

        /* Scan the buffer and remove any carriage returns or newlines */
	for (i=0; i<buflen; i++) {
		if (buffer[i] == '\r' || buffer[i] == '\n') 
                        buffer[i] = '*';
        }

	log(INFO, "request", buffer, hit);

        /* Only the GET operation is allowed */
	if (strncmp(buffer, "GET ", 4) && strncmp(buffer, "get ", 4))
		log(WARN, "Only GET operation supported", buffer, fd);

        /* 
         * NUL-terminate after the second space of the request. We don't
         * care about the extra stuff after the filename being requested.
         */
        for (i=4; i<BUFSIZE; i++) {
                if (buffer[i] == ' ') { 
                        buffer[i] = '\0';
                        break;
                }
        }

        /* Catch any illegal relative pathnames (..) */
        if (strstr(buffer, ".."))
                log(WARN, "Relative pathnames not supported", buffer, fd);

        /* In the absence of an explicit filename, default to index.html */
        if (!strncmp(buffer, "GET /\0", 6) || !strncmp(buffer, "get /\0", 6))
		strcpy(buffer, "GET /index.html");

        /* Changed after truncation and/or appending */
        buflen = strlen(buffer); 

        /* Scan for filename extensions and check against valid ones. */
        if (fstr = get_file_extension(buffer, buflen), fstr == NULL)
                log(WARN, "file extension type not supported", buffer, fd);

        /* Open the file for reading */
	if ((file_fd = open(&buffer[5], O_RDONLY)) == -1)
		log(WARN, "failed to open file", &buffer[5], fd);

	log(INFO, "SEND", &buffer[5], hit);

        /* 
         * Format and print the HTTP response to the buffer, then write the 
         * buffer contents to the socket.
         */ 
	sprintf(buffer, "HTTP/1.0 200 OK\r\nContent-Type: %s\r\n\r\n", fstr);
	write(fd, buffer, strlen(buffer));

	/* 
         * Write the file to the socket in blocks of 8KB (last block may 
         * be smaller) 
         */
	while ((ret = read(file_fd, buffer, BUFSIZE)) > 0) {
		write(fd, buffer, ret);
	}

        #ifdef LINUX
	sleep(1);	/* to allow socket to drain */
        #endif
	exit(1);
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
	static struct sockaddr_in client_addr; 
	static struct sockaddr_in server_addr;
	int i, port, pid, listenfd, socketfd, hit;
	socklen_t length;
        int ch;

        port = DEFAULT_PORT; 

        /* Check that all required arguments have been supplied */
        while ((ch = getopt(argc, argv, "p:d:l:?")) != -1) {
                switch (ch) {
                case 'p':
                        port = atoi(optarg);
                        break;
                case 'd':
                        sprintf(www_path, "%s", optarg);
                        break;
                case 'l':
                        sprintf(log_path, "%s", optarg);
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

        /* Check that directory exists */
	if (chdir(www_path) == -1) { 
		printf("ERROR: Can't change to directory %s\n", www_path);
		exit(4);
	}

	/* 
         * Daemonize the process and instantly return OK to the
         * shell. The child is on its own, we aren't going to wait()
         * around for it.  
         */
	if (fork() != 0)
		return 0; 

	signal(SIGCLD, SIG_IGN); /* ignore child death */
	signal(SIGHUP, SIG_IGN); /* ignore terminal hangups */

        /* Close all open files */
	for (i=0; i<32; i++) {
		close(i);
        }
	setpgrp(); /* break away from process group */

	log(INFO, "cloth is starting up...", "", getpid());

	/* Initialize the network socket */
	if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		log(OOPS, "system call", "socket", 0);

        /* Ensure that the port number is legal */
	if (port < 0 || port > MAX_PORT)
		log(OOPS, "Invalid port number (> 60000)", "", 0);

        /* Fill out the socket address struct */
	server_addr.sin_family      = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port        = htons(port);

        /* Attempt to bind address to socket */
	if (bind(listenfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
		log(OOPS, "system call", "bind", 0);

        /* Attempt to listen on socket */
	if (listen(listenfd, 64) < 0)
		log(OOPS, "system call", "listen", 0);

	for (hit=1; ; hit++) {
		length = sizeof(client_addr);

                /* Attempt to accept on socket */
		if ((socketfd = accept(listenfd, (struct sockaddr *)&client_addr, &length)) < 0)
			log(OOPS, "system call", "accept", 0);

                /* Fork a new process to handle the request */
		if ((pid = fork()) < 0)
			log(OOPS, "system call", "fork", 0);

                /* 
                 * If I am the child process, fork() will return 0, and
                 * I can move into web() and handle the HTTP request.
                 * Otherwise, I am the parent, and I should close the socket
                 * I received the request on. 
                 */
                if (pid == 0) {
                        close(listenfd);
                        web(socketfd, hit); /* never returns */
                } else { 
                        close(socketfd);
                }
	}
}


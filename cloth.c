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

#define BUFSIZE 8096

enum log_genre { ERROR=42, WARN, INFO };

const char *bad_dir[]={
        "/", "/etc", "/bin", "/lib", "/tmp", "/usr", "/dev", "/sbin", NULL
};

struct ext_t {
        char *ext;
        char *filetype;
};

struct ext_t extensions[]={ 
        {"gif", "image/gif" },
	{"jpg", "image/jpeg"}, 
	{"jpeg","image/jpeg"},
	{"png", "image/png" },  
	{"zip", "image/zip" },  
	{"gz",  "image/gz"  },  
	{"tar", "image/tar" },  
	{"htm", "text/html" },  
	{"html","text/html" },  
	{0,0} 
};


/****************************************************************************** 
 * LOGS
 * Implements the common log format and standard log time format.
 ******************************************************************************/
#define COMMON_LOG_TIME "%d/%b/%Y:%H:%M:%S %z"
#define LOG_PATH "cloth.log"

struct logf_t {
        char *host;
        char *ident;
        char *agent;
        char *date;
        char *request;
        char *status;  /* Not implemented */
        char *bytes;   /* Not implemented */
        pid_t pid;
};

/** 
 * cruft to free() a logf_t 
 */
inline void del_log(struct logf_t *log)
{
        free(log->host);
        free(log->ident);
        free(log->agent);
        free(log->date);
        free(log->request);
        free(log->status);
        free(log->bytes);
}

/**
 * mklog -- create a logf_t given req string, the current time, and the pid
 * @buffer: the full request string to be parsed
 * @time  : the current time from time(NULL)
 * @pid   : the process id from getpid()
 */
struct logf_t *make_log(char *buffer, time_t time, pid_t pid)
{
        static char copy[BUFSIZE*2];
        static char date[BUFSIZE];
        struct logf_t *new;
        char *token;
        char *field;

        new = calloc(1, sizeof(struct logf_t));

        strcpy(copy, buffer);

        /* 
         * Parse the provided buffer, filling the fields in the 
         * struct log_format
         */
        for (token  = strtok(copy, "**"); 
             token != NULL; 
             token  = strtok(NULL, "**")) {

                if (field = strstr(token, "GET"), field != NULL)
                        new->request = strdup(field);

                if (field = strstr(token, "Host:"), field != NULL)
                        new->host = strdup(&field[6]);

                if (field = strstr(token, "User-Agent:"), field != NULL)
                        new->agent = strdup(&field[12]);
        }

        /* Format and print the time string */
        strftime(date, BUFSIZE, COMMON_LOG_TIME, gmtime(&time));
        new->date = strdup(date); 

        new->pid = pid;
        
        return (new);
}


/**
 * log -- prints server status and messages to the log file 
 * @genre: what sort of log entry (see enum log_genre) 
 * @s1   : message 1
 * @s2   : message 2
 * @num  : errorno
 */
void log(int type, char *msg, char *requester, int socket_num)
{
	char logbuf[BUFSIZE*2];
        char reqbuf[BUFSIZE*2];
        struct logf_t *log;
	int fd;

        log = make_log(requester, time(NULL), getpid());

        sprintf(reqbuf, "%s %s %s %s %d %d", log->host, log->agent, 
                                             log->date, log->request, 
                                             log->pid,  socket_num);

	switch (type) 
        {
	case ERROR: 
                sprintf(logbuf, "ERROR: %s ERRNO=%d PID=%d SOCKET:%d\n", 
                        msg, errno, getpid(), socket_num); 
                break;

	case WARN: 
		sprintf(logbuf, "<HTML><BODY><H1>cloth HTTP server says: %s"
                                "</H1></BODY></HTML>\r\n", msg);
		write(socket_num, logbuf, strlen(logbuf));
		sprintf(logbuf, "WARN: %s %% %s\n", msg, reqbuf); 
		break;

	case INFO: 
                sprintf(logbuf, "INFO: %s %% %s\n", msg, reqbuf);
                break;
	}	
        
        /* Write the log buffer to the log file */
	if ((fd = open(LOG_PATH, O_CREAT| O_WRONLY | O_APPEND, 0644)) >= 0) {
		write(fd, logbuf, strlen(logbuf)); 
		write(fd, "\n", 1);      
		close(fd);
	}

        /* Clean up the formatted log struct */
        del_log(log);
        free(log);

	if (type == ERROR || type == WARN) 
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
 * @hit: ???
 */
void web(int fd, int hit)
{
	static char buffer[BUFSIZE+1]; /* static so zero filled */
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
		log(WARN, "Only simple GET operation supported", buffer, fd);

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
        if (!strncmp(buffer, "GET /\0", 6) 
        ||  !strncmp(buffer, "get /\0", 6))
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
        #define MAX_PORT 60000
	static struct sockaddr_in cli_addr; 
	static struct sockaddr_in serv_addr;
	int i, port, pid, listenfd, socketfd, hit;
	size_t length;

        /* Check that all required arguments have been supplied */
	if (argc < 3 || argc > 3 || !strcmp(argv[1], "-?")) {
	        printf("usage: cloth <PORT> <WWW-DIRECTORY>\n\n"
	               "\tcloth is a miniscule HTTP server.\n");
		exit(0);
	}

        /* Check that top-directory is legal */
        for (i=0; bad_dir[i] != NULL; i++) {
                if (!strncmp(argv[2], bad_dir[i], strlen(bad_dir[i])+1)) {
		        printf("ERROR: Bad top directory %s\n", argv[2]);
		        exit(3);
                }
        }

        /* Check that directory exists */
	if (chdir(argv[2]) == -1) { 
		printf("ERROR: Can't Change to directory %s\n", argv[2]);
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

	log(INFO, "cloth is starting up...", argv[1], getpid());

	/* Initialize the network socket */
	if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		log(ERROR, "system call", "socket", 0);

        /* Ensure that the port number is legal */
	port = atoi(argv[1]);
	if (port < 0 || port > MAX_PORT)
		log(ERROR, "Invalid port number (try 1->60000)", argv[1], 0);

        /* Fill out the socket address struct */
	serv_addr.sin_family      = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port        = htons(port);

        /* Attempt to bind address to socket */
	if (bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
		log(ERROR, "system call", "bind", 0);

        /* Attempt to listen on socket */
	if (listen(listenfd, 64) < 0)
		log(ERROR, "system call", "listen", 0);

	for (hit=1; ; hit++) {
		length = sizeof(cli_addr);

                /* Attempt to accept on socket */
		if ((socketfd = accept(listenfd, (struct sockaddr *)&cli_addr, &length)) < 0)
			log(ERROR, "system call", "accept", 0);

                /* Fork a new process to handle the request */
		if ((pid = fork()) < 0)
			log(ERROR, "system call", "fork", 0);

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


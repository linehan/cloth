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
#include "log.h"


/* Default path of the log file (relative to -d) */
#define LOG_PATH "cloth.log"
#define INFO_PATH "cloth.info"

/* strftime format strings */
#define COMMON_LOG_TIME "%d/%b/%Y:%H:%M:%S %z"
#define ISO_TIME        "%Y-%m-%d %H:%M:%S"


/******************************************************************************
 * SESSION INFORMATION
 * 
 * A session collects the 7 values that are unique to each new connection
 * over a socket:
 *
 *      0. socket      - the outbound socket being communicated over
 *      1. time        - the current time as a formatted string
 *      2. host        - hostname given by remote client (resolves to local ip)
 *      3. agent       - the user-agent id of the remote client
 *      4. resource    - the file requested by the remote client
 *      5. remote_addr - the address of the remote client
 *      6. remote_port - the port of the remote client
 *
 * It also contains a 'buffer' member to accomodate the formatted string 
 * produced from the 7 values above, which will be an element in the log.
 *
 ******************************************************************************/
/** 
 * sesinfo_http -- Insert parsed HTTP request into the session struct 
 * @session: the uninitialized session struct 
 * @request: the raw text of the HTTP request 
 *
 * PROVIDES: resource, host, agent 
 */
void sesinfo_http(struct ses_t *session, char *request)
{
        char *copy;  // copy of HTTP request
        char *token; // tokenized substring of copy
        char *clean; // cleaned-up version of token

        copy = bdup(request);

        /* 
         * Search tokens are truncated before being placed in the struct.
         * See field() in textutils.h for details.
         */
        for (token  = strtok(copy, "**"); 
             token != NULL; 
             token  = strtok(NULL, "**")) 
        {
                if (clean = field(token, "GET "), clean != NULL)
                        pumpf(&session->resource, "%s", clean);

                if (clean = field(token, "Host: "), clean != NULL)
                        pumpf(&session->host, "%s", clean);

                if (clean = field(token, "User-Agent: "), clean != NULL)
                        pumpf(&session->agent, "%s", clean);
        }
}


/**
 * sesinfo_addr -- Insert remote host address and port into the session struct
 * @session: the uninitialized session struct
 * @remote : a copy of the remote sockaddr_in 
 * 
 * PROVIDES: remote_addr, remote_port
 */
inline void sesinfo_addr(struct ses_t *session, struct sockaddr_in *remote) 
{
        session->remote_addr = inet_ntoa(remote->sin_addr);
        session->remote_port = ntohs(remote->sin_port);
}


/**
 * sesinfo_time -- Insert the formatted time into the session struct
 * @session: the uninitialized session struct
 * @time   : current time
 */
inline void sesinfo_time(struct ses_t *session, time_t time)
{
        strftime(session->time, ISO_LEN, ISO_TIME, gmtime(&time));
}


/**
 * sesprep -- Write a formatted string containing session information
 * @session: previously-initialized session struct
 * @status : the status code
 */
inline void sesprep(struct ses_t *session, struct http_status *status)
{
        pumpf(&session->buffer, "%s: %s %s %s %s:%hu (%s)",
              status->tag,
              session->resource,
              session->host,
              status->figure,
              session->remote_addr,
              session->remote_port,
              session->time);
}


/**
 * sesinfo -- fill out a session structure
 * @session: the uninitialized session struct
 * @socket : file descriptor of active socket
 * @remote : sockaddr of remote client
 * @request: HTTP request
 */
void sesinfo(struct ses_t *session, int socket, struct sockaddr_in *remote, char *request)
{
        sesinfo_http(session, request);    // get resource, host, agent
        sesinfo_addr(session, remote);     // get remote_addr, remote_port
        sesinfo_time(session, time(NULL)); // get formatted time
        session->socket = socket;          // get socket descriptor
}


/******************************************************************************
 * WRITE
 * Functions to write to the log and to write over the open socket.
 ******************************************************************************/
/**
 * write_log -- Write a char buffer to the designated LOG_PATH
 * @buffer: string to be written to log file
 */
void write_log(const char *path, const char *buffer)
{
	int fd;
	if ((fd = open(path, O_CREAT| O_WRONLY | O_APPEND, 0644)) >= 0) {
		write(fd, buffer, strlen(buffer)); 
		write(fd, "\n", 1);      
		close(fd);
	}
}


/**
 * write_socket -- Write a buffer, including an HTTP error code, over a socket
 * @socket: socket file descriptor
 * @code  : HTTP status code
 * @buffer: message to be printed over socket
 */
void write_socket(int socket, int http_code, const char *message)
{
        char *buffer;
	pumpf(&buffer, "cloth says: %hd %s\r", http_code, message);

        /* Check that socket is a valid file descriptor */
        if (fcntl(socket, F_GETFD) == EBADF)
                log(WARN, NULL, "Socket write failure");
        else
	        write(socket, buffer, strlen(buffer));

        free(buffer);
}


/******************************************************************************
 * LOG 
 * Entry point for outside callers seeking to write to the log.
 ******************************************************************************/
/**
 * log -- write a message to the log file and/or over a socket
 * @code: the cloth status code
 * @session: an initialized session struct
 * @message: an additional explanatory message
 */
void log(int code, struct ses_t *session, char *message)
{
	char *buffer;

        if (session) {
                sesprep(session, &STATUS[code]);
                pumpf(&buffer, "%s", session->buffer);
        } else
                pumpf(&buffer, "%s: %s (%d)", STATUS[code].tag, message, errno);

        write_log(LOG_PATH, buffer); // All codes get written to the log

	switch (STATUS[code].code) 
        {
	case INFO: 
                free(buffer);
                break;
	case OUCH: 
                exit(3);
                break;
        case WARN:
                write_socket(session->socket, STATUS[code].code, message);
                exit(3);
		break;
	}	
}


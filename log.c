#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/param.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "textutils.h"
#include "http_status.h"

/* Default path of the log file (relative to -d) */
#define LOG_PATH "cloth.log"

/* strf format strings */
#define COMMON_LOG_TIME "%d/%b/%Y:%H:%M:%S %z"
#define ISO_TIME        "%Y-%m-%d %H:%M:%S"
#define ISO_LEN         24

/****************************************************************************** 
 * LOGS
 *
 * { code, file, hostname, verb, remote_addr, remote_port, time, message } 
 *
 * code 
 * ````
 *      START - the server is starting up
 *      STOP  - the server is shutting down
 *      INFO  - standard entry which indicates an action
 *      WARN  - an action has failed
 *      OUCH  - an unrecoverable error has occured
 *
 * file 
 * ````
 *      The filename of the source being requested
 *
 * hostname 
 * ````````
 *      myserver.mydomain.org 
 *      42.112.5.25.some.isp.net. 
 *
 *      The address or domain name specified by the remote host. Because 
 *      different names may resolve to the same address, this field will 
 *      specifically reflect the label that the *remote host* is using to 
 *      contact your server.
 *
 * verb
 * ````
 *      The nature of the interaction between the local and remote hosts,
 *      being one of the 9 HTTP methods ("verbs"). The 9 methods are HEAD, 
 *      GET, POST, PUT, DELETE, TRACE, OPTIONS, CONNECT, and PATCH. 
 *
 *      cloth only supports the GET routine, for simplicity. 
 *
 * remote_addr 
 * ```````````
 *      IPv4 address of the remote client. 
 *
 * remote_port
 * ```````````
 *      Port number of the remote client.
 * time
 * ````
 *      The time at which the request was processed by the server. 
 *      Time is formatted in ISO format: yyyy-mm-dd HH:mm:ss
 *
 * message
 * ```````
 *      An optional plaintext message, e.g. explaining an error context or
 *      specifying the user agent of the remote host.
 *
 ******************************************************************************/


/******************************************************************************
 * SESSION INFORMATION
 * 
 * A session is identified in the log by a set of 6 values:
 *
 *      0. socket      - the outbound socket being communicated over
 *      1. host        - hostname given by remote client (resolves to local ip)
 *      2. agent       - the user-agent id of the remote client
 *      3. resource    - the file requested by the remote client
 *      4. remote_addr - the address of the remote client
 *      5. remote_port - the port of the remote client
 *
 ******************************************************************************/
/*
 * Session structure
 */
struct ses_t {
        int  socket;                 // File descriptor of the socket
        char *host;                  // Hostname submitted by remote end
        char *agent;                 // Remote user-agent id
        char *resource;              // Resource (file) being requested
        char *remote_addr;           // Address of the remote host
        char time[ISO_LEN];          // Formatted time of processing 
        unsigned short remote_port;  // Port of the remote host
};


/** 
 * sesinfo_http -- Insert parsed HTTP request into the session struct 
 * @session: the uninitialized session struct 
 * @request: the raw text of the HTTP request 
 *
 * PROVIDES: resource, host, agent 
 */
void sesinfo_http(struct ses_t *session, char *request)
{
        char *copy;     // copy of HTTP request
        char *token;    // tokenized substring of copy
        char *clean;    // cleaned-up version of token

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
        strftime(session->time, LOGBUF, ISO_TIME, gmtime(&time));
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
        sesinfo_http(session, request); // get resource, host, agent
        sesinfo_addr(session, remote);  // get remote_addr, remote_port
        session->socket = socket;       // get socket descriptor
}






char *new_entry(struct http_status *status, struct session_t *session)
{
        char *timebuf;
        char *buffer;

        timebuf = malloc(LOGBUF * sizeof(char));
        time_info(timebuf, time(NULL));

        pumpf(&buffer, "%s %s %s %s:%hu (%s)",
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
        char *entry;
	char *buf;
	int fd;

        if (session != NULL)
                entry = new_entry(&STATUS[code], session);

	switch (STATUS[code].code) 
        {
	case INFO: 
                pumpf(&buf, "INFO: %s %s", entry, msg);
                break;
	case OUCH: 
                pumpf(&buf, "OUCH: %s (%d)", msg, errno); 
                break;
        case WARN:
		pumpf(&buf, "cloth says: %hd %s\r", STATUS[code].code, msg);
		write(session->socket, buf, strlen(buf));
		pumpf(&buf, "WARN: %s (%hd)", entry, STATUS[code].http); 
		break;
	}	
        
        /* Write the log buffer to the log file */
	if ((fd = open(LOG_PATH, O_CREAT| O_WRONLY | O_APPEND, 0644)) >= 0) {
		write(fd, buf, strlen(buf)); 
		write(fd, "\n", 1);      
		close(fd);
	}

        if (session != NULL)
                free(entry);

        free(buf);
        
	if (STATUS[code].code == OUCH || STATUS[code].code == WARN) 
                exit(3);
}


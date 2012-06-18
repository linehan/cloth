#ifndef __HTTP_LOG_H
#define __HTTP_LOG_H


/* Collects multiple representations of a status. */
struct http_status {
        char *tag;
        short code;
        short http;
        const char *figure;
};


/* cloth internal status code classes */
#define OUCH 42 // unrecoverable error
#define WARN 43 // failure
#define INFO 46 // normal operation


/* HTTP status codes */
#define HTTP_OK                 200
#define HTTP_ACCEPTED           202
#define HTTP_BAD_REQUEST        400
#define HTTP_NOT_FOUND          404
#define HTTP_METHOD_FORBIDDEN   405
#define HTTP_HEADER_OVERFLOW    431
#define HTTP_SERVER_ERROR       500
#define HTTP_NOT_IMPLEMENTED    501
#define HTTP_FATAL_ERROR        555 


/* cloth status codes */
enum codes { RESPONSE, ACCEPT, BAD_REQUEST, NOT_FOUND, BAD_METHOD, OVERFLOW,
             ERROR, NO_METHOD, FATAL };


/* status codes are indices into the global STATUS vector */
static struct http_status STATUS[]={
        { "INFO", INFO, HTTP_OK,               "--->" }, // RESPONSE
        { "INFO", INFO, HTTP_ACCEPTED,         "<---" }, // ACCEPT
        { "WARN", WARN, HTTP_BAD_REQUEST,      "x---" }, // BAD_REQUEST
        { "WARN", WARN, HTTP_NOT_FOUND,        "?---" }, // NOT_FOUND
        { "WARN", WARN, HTTP_METHOD_FORBIDDEN, "x---" }, // BAD_METHOD
        { "WARN", WARN, HTTP_HEADER_OVERFLOW,  "+---" }, // OVERFLOW
        { "WARN", WARN, HTTP_SERVER_ERROR,     "---x" }, // ERROR
        { "WARN", WARN, HTTP_NOT_IMPLEMENTED,  "---?" }, // NO_METHOD
        { "OUCH", OUCH, HTTP_FATAL_ERROR,      "xxxx" }, // FATAL
};


#define ISO_LEN 24


/* Session structure */
struct ses_t {
        int  socket;                 // File descriptor of the socket
        char time[ISO_LEN];          // Formatted time of processing 
        char *host;                  // Hostname submitted by remote end
        char *agent;                 // Remote user-agent id
        char *resource;              // Resource (file) being requested
        char *remote_addr;           // Address of the remote host
        unsigned short remote_port;  // Port of the remote host
        char *buffer;                // The formatted output string
};


/* Function prototypes */
void log(int code, struct ses_t *session, char *message);
void sesinfo(struct ses_t *, int, struct sockaddr_in *, char *);


#endif


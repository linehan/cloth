#ifndef __HTTP_STATUS_H
#define __HTTP_STATUS_H


struct http_status {
        short code;
        short http;
        const char *figure;
};


#define OUCH 42
#define WARN 43
#define INFO 46


#define   HTTP_OK                 200
#define   HTTP_ACCEPTED           202

#define   HTTP_BAD_REQUEST        400
#define   HTTP_NOT_FOUND          404
#define   HTTP_METHOD_FORBIDDEN   405
#define   HTTP_HEADER_OVERFLOW    431
#define   HTTP_SERVER_ERROR       500
#define   HTTP_NOT_IMPLEMENTED    501

#define   HTTP_FATAL_ERROR        555 

enum codes {
        RESPONSE,
        ACCEPT,
        BAD_REQUEST,
        NOT_FOUND,
        BAD_METHOD,
        OVERFLOW,
        ERROR,
        NO_METHOD,
        FATAL
};

static struct http_status STATUS[]={
        { INFO, HTTP_OK,               "--->" },      /* RESPONSE */
        { INFO, HTTP_ACCEPTED,         "<---" },      /* ACCEPT */
        { WARN, HTTP_BAD_REQUEST,      "x---" },      /* BAD_REQUEST */
        { WARN, HTTP_NOT_FOUND,        "?---" },      /* NOT_FOUND */
        { WARN, HTTP_METHOD_FORBIDDEN, "x---" },      /* BAD_METHOD */
        { WARN, HTTP_HEADER_OVERFLOW,  "+---" },      /* OVERFLOW */
        { WARN, HTTP_SERVER_ERROR,     "---x" },      /* ERROR */
        { WARN, HTTP_NOT_IMPLEMENTED,  "---?" },      /* NO_METHOD */
        { OUCH, HTTP_FATAL_ERROR,      "xxxx" },      /* FATAL */
};

#endif

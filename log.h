#ifndef __HTTP_LOG_H
#define __HTTP_LOG_H

struct ses_t _session_fref;


void log(int code, struct ses_t *session, char *msg);

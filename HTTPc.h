#ifndef __HTTPC_H__
#define __HTTPC_H__


char *HTTP_request(char *url, int con, int ver, char *header);
int ParseWanIP(char *html, char *wip, int type);

#endif


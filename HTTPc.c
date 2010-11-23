#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "HTTPc.h"
//#include "CameraManage.h"

#define PORT 80


int build_get_query(char *host, char *page, int con, int ver, char *get)
{
	char *conn_str[] = {"close", "Keep-Alive"};

	if(ver == 1) //http 1.1
	{
		sprintf(get, "\
GET /%s HTTP/1.1\r\n\
User-Agent: httpc-lite 0.1\r\n\
Host: %s\r\n\
Accept: text/html\r\n\
Connection: %s\r\n\r\n"
		, page, host, conn_str[con]);
	}
	else
	{
		sprintf(get, "\
GET /%s HTTP/1.0\r\n\
User-Agent: httpc-lite 0.1\r\n\
Host: %s\r\n\
Accept: text/html\r\n\r\n"
		, page, host);
	}

/*
Accept-Language: zh-TW,zh;q=0.9,en;q=0.8\r\n\
Accept-Charset: iso-8859-1, utf-8, utf-16, *;q=0.1\r\n\
Accept-Encoding: deflate, gzip, x-gzip, identity, *;q=0\r\n\
If-Modified-Since: 0\r\n\
*/

	return 0;
}


int create_tcp_socket()
{
	int sock;

	errno = 0;
	if((sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
	{
		fprintf(stderr, "Can't create TCP socket, becaust '%s'\n", strerror(errno) );
		sock = -1;
	}

	return sock;
}

//url => http://www.abc.def.com/first.html
//con => 1:Keep-Alive,  0:colse
//ver => 1: 1.1, 0: 1.0
// if(header != NULL) put 'HTTP header'
// return HTTP content


char *HTTP_request(char *url, int con, int ver, char *header)
{
	char *ip, *page, *ptr, *host, ip_buf[16], url_tmp[128];
	struct hostent *hent;
	struct sockaddr_in remote;
	int sock, tmpres;
	int rsize, n;
	size_t sent;

	memset(url_tmp, 0, sizeof(url_tmp));
    memcpy(url_tmp, url, strlen(url)); //to resolve SIG11
	//------------- parse URL ---------------------------
	ptr = host = url_tmp;
	if(strstr(ptr, "http://") != NULL)
		host += 7;

	if( (page=strstr(host, "/")) != NULL)
	{
		*page++ = '\0';
		//DDPRINTF("\033[1;33m[%s]\033[m url='%s'\nurl_tmp='%s'\npage = '%s'\n", __func__, url, url_tmp, page);
	}
	else
	{
		page = host +strlen(host);
		*(page+1) = '/';
		*(page+2) = '\0';
	}

	if( (host[0] >= '0') && (host[0] <= '9'))
		ip = host;
	else
	{
		if((hent = gethostbyname(host)) == NULL)
		{
			fprintf(stderr, "Can't get ip from '%s'\n", host);
			return NULL;
		}
		if(inet_ntop(AF_INET, (void *)hent->h_addr_list[0], ip_buf, 16) == NULL)
		{
			fprintf(stderr, "Can't resolve ip from '%s'\n", (char *)hent->h_addr_list[0]);
			return NULL;
		}
		ip = ip_buf;
	}

	//---------------- prepare socket structure ---------------------------------------
	remote.sin_family = AF_INET;
	tmpres = inet_pton(AF_INET, ip, (void *)(&(remote.sin_addr.s_addr)));
	if( tmpres < 0)
	{
		fprintf(stderr, "Can't set remote.sin_addr.s_addr");
		return NULL;
	}
	else if(tmpres == 0)
	{
		fprintf(stderr, "%s is not a valid IP address\n", ip);
		return NULL;
	}
	remote.sin_port = htons(PORT);

	//---------------- Create TCP socket & connect ------------------------------------
	sock = create_tcp_socket();
	if(sock < 0)
		return NULL;

	ptr = NULL;
	errno = 0;
	if(connect(sock, (struct sockaddr *)&remote, sizeof(struct sockaddr)) < 0)
	{
		fprintf(stderr, "Could not connect because '%s'\n", strerror(errno));
		goto http_req_over;
	}

	//---------------- start request ------------------------------------------------
	char get[512];
	build_get_query(host, page, con, ver, get);

//DDPRINTF("get_query -----------------------\n%s\n---------------------------------\n", get);

	sent = 0;
	while(sent < strlen(get))
	{
		errno = 0;
		tmpres = send(sock, get+sent, strlen(get)-sent, 0);
		if(tmpres == -1)
		{
			fprintf(stderr, "Can't send query because '%s'\n", strerror(errno) );
			goto http_req_over;
		}
		sent += tmpres;
	}


	rsize=0;
    n=1;
	char *h = header;

	while((tmpres = recv(sock, h+rsize, BUFSIZ, 0)) > 0)
	{
		rsize += tmpres;
	}

	if( (ptr=strstr(h, "\r\n\r\n")) != NULL)
	{
		*(ptr+2) = '\0';
		*(ptr+3) = '\0';
		ptr += 4;
	}
	else if( (ptr=strstr(h, "\n\n")) != NULL)
	{
		*(ptr+1) = '\0';
		ptr += 2;
	}
	else
		ptr = NULL;

http_req_over:

	close(sock);
	return ptr;

}



//return 0 - success
//        -1 - fail
int ParseWanIP(char *html, char *wip, int type)
{
    switch(type)
    {
        case 0: //http://www.allsystemsgo.es/myip.php
       	{
			char *c1 = "Your WAN IP address: ";
			char *p1, *p2;
            if( (p1=strstr(html, c1)) != NULL)
            {
                p1 += strlen(c1);
                p2 = strstr(p1, "</h1>");
                memcpy(wip, p1, p2-p1);
            }
       	}
            break;

		case 1: //http://wanip.info
		{
			char *p1, *p2, *p3;
			if( (p1=strstr(html, "ipinfo")) != NULL)
			{
				int i, len=0;
				for(i=0; i<4; i++)
				{
					p2 = strstr(p1, "<span>");
					p2 += 6;
					p3 = strstr(p1, "</span>");
					len = p3 -p2;
					memcpy(wip, p2, len);
					wip += len;
					*wip++ = '.';
					p1 = p3 +6;
				}
				*(--wip) = '\0';
			}
		}
			break;

		case 2: // http://www.whatismyip.com
		{
			int ch;
			char *p1, *p2, *pend;
			char *c1 = "Your IP Address Is:";
            if( (p1=strstr(html, c1)) != NULL)
           	{
           		pend = strstr(p1, "<br />");
				p1 += (strlen(c1));
				while(p1 <= pend)
				{
					switch(*p1)
					{
						case ' ':
							p1++;
							break;

						case '<':
							p2 = strstr(p1, ">");
							p1 = p2 +1;
							break;

						case '&':
							p1 += 2;
							ch = atoi(p1);
							*wip++ = ch;
							while(*p1++ != ';') {};
							break;

						case '0':
						case '1':
						case '2':
						case '3':
						case '4':
						case '5':
						case '6':
						case '7':
						case '8':
						case '9':
						case '.':
							*wip++ = *p1++;
							break;

						default:
							break;
					}
				}
           	}
		}
			break;

        default:
            return -1;
    }

    return 0;
}


//#define STANDALONE_TEST
#if defined(STANDALONE_TEST)
int main(int argc, char *argv[])
{
	char *content, *header;
	int wsite;
	char *wanip_site[3] = { "http://www.allsystemsgo.es/myip.php", 
							"http://wanip.info",
							"http://www.whatismyip.com"};
	header = (char *)malloc(1024*1024);
	if(header != NULL)
	{
		for(wsite=0; wsite++; wsite<3)
		{
			content = HTTP_request(wanip_site[wsite], 0, 1, header);
			if(content != NULL)
			{
				char wanip[17];
				memset(wanip, 0, 17);
				ParseWanIP(content, wanip, wsite);
				DDPRINTF("wan ip(allsystemsgo.es) = \033[1;33m%s\033[m\n", wanip);
			}
			else
			{
				fprintf(stderr, "http request '%s' fail\n", wanip_site[wsite]);
			}
		}
	}
	free(header);

	return 0;
}

#endif


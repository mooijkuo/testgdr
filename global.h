#ifndef __GLOBAL_H__
#define __GLOBAL_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(IVAN_DEBUG)
	#define DDPRINTF(msg, args...)	{ fprintf(stderr, msg, ## args); fflush(stderr); }
	//#define DDPRINTF(msg, args...)	{ fprintf(stderr, msg, __VA_ARGS__); fflush(stderr); }
#else
	#define DDPRINTF(msg, ...)
#endif


#define MAKE_SOCKADDR_IN(var,adr,prt) /*adr,prt must be in network order*/\
    /*struct sockaddr_in var;*/\
    var.sin_family = AF_INET;\
    var.sin_addr.s_addr = (adr);\
    var.sin_port = (prt);



#endif



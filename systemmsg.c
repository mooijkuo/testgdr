
#include "gdr.h"
#include <sys/sem.h>
#include <sys/ipc.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/msg.h>

int SystemMsgControlInit(void)
{
	int 		MsgKey;
	int 		MsgID;

	MsgKey = KEY_SYSTEM_MSG_CONTROL;
	MsgID = msgget(MsgKey,IPC_CREAT | 0666);
	return MsgID;
}

int	API_System_Reboot(void)
{
	int 		MsgControlID;
	MSGBUF	MsgControlBuf;
		
	MsgControlID 	= SystemMsgControlInit();
	
	memset(&MsgControlBuf,0,sizeof(MSGBUF));
	
	MsgControlBuf.mtype		= MSG_CONTROL;
	MsgControlBuf.Command	=	MSG_SYSTEM_REBOOT;
	//MsgControlBuf.mtext[0]	=	channel;
	//MsgControlBuf.mtext[1]	=	compression;
	
	if (msgsnd(MsgControlID,&MsgControlBuf,sizeof(MSGBUF)-sizeof(long),0)== -1)
	{
		printf("send msg error !! reboot ??\n");
		return 0;
	}
	
	return 1;
}

int	API_System_Date(char *DateString)
{
	int 		MsgControlID;
	MSGBUF	MsgControlBuf;
		
	MsgControlID 	= SystemMsgControlInit();
	
	memset(&MsgControlBuf,0,sizeof(MSGBUF));
	
	MsgControlBuf.mtype		= MSG_CONTROL;
	MsgControlBuf.Command	=	MSG_SYSTEM_DATE;
	memcpy(&MsgControlBuf.mtext[0],DateString,strlen(DateString));
	
	if (msgsnd(MsgControlID,&MsgControlBuf,sizeof(MSGBUF)-sizeof(long),0)== -1)
	{
		printf("send msg error !! reboot ??\n");
		return 0;
	}
	
	return 1;
}


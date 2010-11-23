#ifndef __RTSPCLIENT_H__
#define __RTSPCLIENT_H__

#include "global.h"
#include "gdr.h"

#define	CLIENTS		2

#define   METHOD_OPTIONS    0x0001
#define   METHOD_DESCRIBE   0x0002
#define   METHOD_SETUP      0x0004
#define   METHOD_PLAY       0x0008
#define   METHOD_TEARDOWN   0x0010
#define   METHOD_GET        0x0020
#define   METHOD_SETPARAM   0x0040
#define   METHOD_PAUSE      0x0080
#define   METHOD_REDIRCT    0x0100


#define RTP_VER(c)	(((c)&0xC0) >> 6)
#define RTP_PAD(c)	(((c)&0x20) >> 5)
#define RTP_EXT(c)	(((c)&0x10) >> 4)
#define RTP_CCNT(c)	(((c)&0x0F) >> 0)
#define RTP_MARK(c)	(((c)&0x80) >> 7)
#define RTP_TYPE(c)	(((c)&0x7F) >> 0)
//#define RTP_SEQNUM(c)	((c)&0xFFFF)

/*
struct RTPpacket
{
    unsigned char version: 2;
    unsigned char padding: 1;
    unsigned char extension: 1;
    unsigned char csrc_cnt: 4;
    unsigned char mark: 1;
    unsigned char payload_type: 7;
    unsigned short seq_num: 16;
    unsigned int timestamp: 32;
    unsigned int ssrc: 32;
    unsigned char csrc[0];
} __attribute__((__packed__));
*/

struct RTPpacket_tcp
{
	unsigned char sign[2];
	unsigned short packet_len;

	unsigned char version;//: 2;
	unsigned char padding;//: 1;
	unsigned char extension;//: 1;
	unsigned char csrc_cnt;//: 4;

	unsigned char mark;//: 1;
	unsigned char payload_type;//: 7;

	unsigned short seq_num;//: 16;

	unsigned int timestamp;//: 32;

	unsigned int ssrc;//: 32;
	unsigned char csrc[0];
};

struct RTPpacket_udp
{
	unsigned char version;//: 2;
	unsigned char padding;//: 1;
	unsigned char extension;//: 1;
	unsigned char csrc_cnt;//: 4;

	unsigned char mark;//: 1;
	unsigned char payload_type;//: 7;

	unsigned short seq_num;//: 16;

	unsigned int timestamp;//: 32;

	unsigned int ssrc;//: 32;
	unsigned char csrc[0];
};


#define NALU_FZ(c)		(((c)&0x80) >> 7)
#define NALU_NRI(c)		(((c)&0x60) >> 5)
#define NALU_TYPE(c)	(((c)&0x1F) >> 0)

#define	NALU_FUA	28
#define NALU_STAPA	24

struct NALU
{
	unsigned char fz_bit; //:1
	unsigned char nri; //:2
	unsigned char type; //:5
};

#define	QUEUE_SIZE	32*1024
#define D_SIZE		2*1024
struct Queue
{
	unsigned char head[5];
	unsigned char buf[QUEUE_SIZE];
	unsigned char tail[59];
	unsigned char *front; //for write
	unsigned char *rear; //for read
	unsigned char *rpoint;// return point
};


struct BufLink
{
	int num;
	int raw_len, len, skip;
	int ptype; // 96=video, 97=audio
	unsigned char buf[1500];	// current max packet size is 1472
};

#define BL_NUM	1024
//#define B_SIZE	(BL_NUM *1500)

struct EBuf
{
	int data_len;
	//int curr_pos;
	// processA/V() set wptr
	// GetDataBlockFromReadBuffer() set rear & data_len
	// processData() set front & data_len
	//struct BufLink *front, *rear, *wptr, *wb;
	// rear <= front <= wptr
	int haveData;
	int front, rear, wptr;
	struct BufLink BUF[BL_NUM];
};


struct RTSPClient
{
	int num;
	unsigned int sockfd;
	unsigned int RTPfd[4];
	unsigned int savefd;
	unsigned int method;
	unsigned int cseq;
	unsigned int session;
	unsigned int RTSPport, clientPort[2], serverPort[2];
	unsigned int playing;
	int track, track2;
	unsigned int SenderSSRC[2];
	unsigned int mySSRC[2];
	unsigned int videoNum, audioNum;
	unsigned char ptypeVideo, ptypeAudio;
	unsigned short rtp_seq;
	unsigned int jitter;
	unsigned int first_time;
	//ssize_t recv_size;
	//pthread_t data_thread;
	pthread_t video_thread, audio_thread, data_thread;
	pthread_t save_thread;
	pthread_mutex_t r_mutex, data_mutex, read_mutex, av_mutex;
	pthread_cond_t noData, avSync;
	char *s_ip, *c_ip;
	char *tail;
	//struct Queue *q;
	//struct Queue *qa;
	struct EBuf *b;
};



struct RTCP_SR_packet
{
	unsigned char first_byte; // ver:2 + padding:1 + re_cnt:5
	unsigned char packet_type;
	unsigned short len;
	unsigned int senderSSRC;
	unsigned int timestampMSW, timestampLSW;
	unsigned int RTPtimestamp;
	unsigned int sp_cnt; // sender's packet count
	unsigned int so_cnt; // sender's octet count
};

struct RTCP_SDES_packet
{
	unsigned char first_byte; // ver:2 + padding:1 + src_cnt:5
	unsigned char packet_type;
	unsigned char len; // len*4 = bytes
	unsigned int SSRC;
	unsigned char type; // 1 = CNAME, ignore others now
	unsigned char sdes_len;
	unsigned char cname[12];
	unsigned char end;
};

struct RTCP_RR_packet
{
	unsigned char first_byte; // ver:2 + padding:1 + re_cnt:5
	unsigned char packet_type;
	unsigned short len;
	unsigned int senderSSRC;
	unsigned int SSRC;
	unsigned char fraction, packet_lost[3];
	unsigned short seq_num;
	unsigned short highest_seq_num;
	unsigned int jitter;
	unsigned int last_sr;
	unsigned int delay_timestamp;
	struct RTCP_SDES_packet sdes;
};






int get_self_ip(char *buff);
int create_client(struct RTSPClient *cli);
void exit_client(struct RTSPClient *cli);


#endif


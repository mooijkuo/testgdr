/***************************************************************
 *
 * 簡單的 RTSP client，以最多同時開啟 16 clients (8 ch *2) 為設計目標
 *
 ***************************************************************/

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/netlink.h> 

#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>
#include <time.h>

#include "RTSPc.h"

const char myCNAME[] = "SQrtspclient";

struct EBuf ebuf[2];

ENCODER_BUFFER_T EncoderBuffer[2];

pthread_mutex_t remain_mutex;
pthread_cond_t noBuffer = PTHREAD_COND_INITIALIZER;
pthread_mutex_t clients_mutex;
int rtsp_client_num = 0;
char self_ip[INET_ADDRSTRLEN];

struct RTSPClient rc[CLIENTS];

int noVideo=0, noAudio=0;
int catCam0=1, catCam1=1;
int recordPause=0;

int send_OPTIONS(struct RTSPClient *cli)
{
	char buffer[1024];

	bzero(buffer, sizeof(buffer));
	sprintf(buffer,
"OPTIONS rtsp://%s/%s RTSP/1.0\r\n\
CSeq: %d\r\n\
User-Agent: rtsp-client lite [%s]\r\n\r\n",
	cli->s_ip, cli->tail, cli->cseq, cli->c_ip);


//DDPRINTF("\033[1;32m[%s]\033[m\n%s \n", __func__, buffer);

	errno = 0;
	if(send(cli->sockfd, buffer, strlen(buffer), 0) < 0)
	{
		DDPRINTF("send CSeq %d failed, err is '%s'\n", cli->cseq, strerror(errno));
		return -1;
	}

	bzero(buffer, sizeof(buffer));
	recv(cli->sockfd, buffer, sizeof(buffer), 0);
//	DDPRINTF("\033[1;32mOPTIONS\033[m got --\n'%s'\n", buffer);

//Public: DESCRIBE, SETUP, PLAY, PAUSE, TEARDOWN
	if(strstr(buffer, "Public") != NULL)
	{
		if(strstr(buffer, "DESCRIBE") != NULL)
			cli->method |= METHOD_DESCRIBE;
		if(strstr(buffer, "SETUP") != NULL)
			cli->method |= METHOD_SETUP;
		if(strstr(buffer, "PLAY") != NULL)
			cli->method |= METHOD_PLAY;
		if(strstr(buffer, "PAUSE") != NULL)
			cli->method |= METHOD_PAUSE;
		if(strstr(buffer, "TEARDOWN") != NULL)
			cli->method |= METHOD_TEARDOWN;
	}

	cli->cseq++;

	return 0;
}

int send_DESCRIBE(struct RTSPClient *cli)
{
	char buffer[1024];

	bzero(buffer, sizeof(buffer));
	sprintf(buffer,
"DESCRIBE rtsp://%s/%s RTSP/1.0\r\n\
CSeq: %d\r\n\
Accept: application/sdp\r\n\
User-Agent: rtsp-client lite\r\n\r\n",
	cli->s_ip, cli->tail, cli->cseq);

		errno = 0;
		if(send(cli->sockfd, buffer, strlen(buffer), 0) < 0)
		{
			DDPRINTF("send CSeq %d failed, err is '%s'\n", cli->cseq, strerror(errno));
			return -1;
		}

	bzero(buffer, sizeof(buffer));
	recv(cli->sockfd, buffer, sizeof(buffer), 0);

//DDPRINTF("\033[1;32mDESCRIBE\033[m got --\n'%s'\n", buffer);

	//---- parse SDP ------------------------------------
#if 0
	cli->track = -1;
	char *ptr = strstr(buffer, "control:trackID");
	if(ptr != NULL)
	{
		cli->track = atoi(ptr+16);
	}
#else
	cli->track = cli->track2 = -1;
	char *p1, *p2;
	char ct[] = "a=control:trackID=";
	char rm[] = "a=rtpmap:";
	if( (p1=strstr(buffer, "m=video")) != NULL) // have video stream
	{
		p2 = strstr(p1, ct);
		cli->track = atoi(p2 +strlen(ct));
		p2 = strstr(p1, rm);
		cli->ptypeVideo = (unsigned char)atoi(p2 +strlen(rm));
	}

	if( (p1=strstr(buffer, "m=audio")) != NULL) // have video stream
	{
		p2 = strstr(p1, ct);
		cli->track2 = atoi(p2 +strlen(ct));
		p2 = strstr(p1, rm);
		cli->ptypeAudio = (unsigned char)atoi(p2 +strlen(rm));
	}

DDPRINTF("\033[1;36m [%s] cli->num=%d, track=%d, %d, ptyp=%d, %d\033[m\n",
	__func__, cli->num, cli->track, cli->track2, cli->ptypeVideo, cli->ptypeAudio);

	if(cli->track == -1 || cli->track2 == -1)
		return -1;

#endif
	cli->cseq++;

	return 0;
}

int send_SETUP(struct RTSPClient *cli)
{
	char buffer[1024];
	char *ptr;

	//----------- 1st SETUP -----------------------------
	bzero(buffer, sizeof(buffer));

	sprintf(buffer,
"SETUP rtsp://%s/%s/trackID=%d RTSP/1.0\r\n\
CSeq: %d\r\n\
Transport: RTP/AVP;unicast;client_port=%d-%d\r\n\
User-Agent: rtsp-client lite\r\n\r\n",
	cli->s_ip, cli->tail, cli->track, cli->cseq, cli->clientPort[0], cli->clientPort[0]+1);

//DDPRINTF("\n\n\033[36m%s\033[m\n\n", buffer);

	errno = 0;
	if(send(cli->sockfd, buffer, strlen(buffer), 0) < 0)
	{
		DDPRINTF("send CSeq %d failed, err is '%s'\n", cli->cseq, strerror(errno));
		return -1;
	}

	bzero(buffer, sizeof(buffer));
	recv(cli->sockfd, buffer, sizeof(buffer), 0);

//DDPRINTF("\033[1;32mSETUP\033[m got --\n'%s'\n", buffer);

	ptr = strstr(buffer, "server_port=");
	if(ptr != NULL)
	{
		cli->serverPort[0] = atoi(ptr+12);
		DDPRINTF("\033[1;35m server_port = %d \033[m \n", cli->serverPort[0]);
	}

	ptr = strstr(buffer, "Session");
	if(ptr != NULL)
	{
		cli->session = atoi(ptr+8);
		DDPRINTF("\033[1;35m session = %d \033[m \n", cli->session);
	}

	cli->cseq++;


	//----------- 2nd SETUP -----------------------------
	bzero(buffer, sizeof(buffer));

	sprintf(buffer,
"SETUP rtsp://%s/%s/trackID=%d RTSP/1.0\r\n\
CSeq: %d\r\n\
Transport: RTP/AVP;unicast;client_port=%d-%d\r\n\
Session: %d\r\n\
User-Agent: rtsp-client lite\r\n\r\n",
	cli->s_ip, cli->tail, cli->track2, cli->cseq, cli->clientPort[1], cli->clientPort[1]+1, cli->session);

	errno = 0;
	if(send(cli->sockfd, buffer, strlen(buffer), 0) < 0)
	{
		DDPRINTF("send CSeq %d failed, err is '%s'\n", cli->cseq, strerror(errno));
		return -1;
	}

	bzero(buffer, sizeof(buffer));
	recv(cli->sockfd, buffer, sizeof(buffer), 0);

	ptr = strstr(buffer, "server_port=");
	if(ptr != NULL)
	{
		cli->serverPort[1] = atoi(ptr+12);
		DDPRINTF("\033[1;35m server_port = %d \033[m \n", cli->serverPort[1]);
	}

	cli->cseq++;


	return 0;
}


#if 0
void *deQueueThread(void *param)
{
	struct RTSPClient *cli = (struct RTSPClient *)param;

	ssize_t save_size=0, total_size=0;
	struct RTPpacket rtpp;
	int i=0;
	unsigned char tmp[4+12];

	struct Queue *q = &cli->q;

	DDPRINTF("\033[1;%dm [%s] num = %d, client=%p \033[m \n", 32+cli->num, __func__, cli->num, cli);

	pthread_mutex_lock(&cli->r_mutex);
	int dq_loop = (cli->playing == 1);
	pthread_mutex_unlock(&cli->r_mutex);
//	while(cli->playing == 1)
	while(dq_loop)
	{
		if(cli->data_ready != 1)
		{
			usleep(10*1000);
			continue;
		}

if(i<100 && cli->num==0)
{
	DDPRINTF("\033[32m [%s] q->front=%p, q->rear=%p, q->rear[]=%02x %02x %02x %02x %02x %02x \033[m\n",
		__func__, q->front, q->rear, q->rear[0], q->rear[1], q->rear[2], q->rear[3], q->rear[4], q->rear[5]);
}

		if(q->rpoint != NULL)
		{
			if((q->rear +4 +12) > q->rpoint) // left bytes less than RTP header
			{
				int i=0;
				for(i=0; i<4+12; i++)
				{
					if(q->rear < q->rpoint)
						tmp[i] = *q->rear++;
					else
					{
						q->rear = q->buf;
						q->rpoint = NULL;
						int j;
						for(j=i; j<4+12; j++)
							tmp[i] = *q->rear++;
						break;
					}
				}
				//parseRTP(tmp, &rtp);
				{
					unsigned char *p = tmp;
					struct RTPpacket *rtp = & rtpp;
					rtp->sign[0] = p[0];
					rtp->sign[1] = p[1];
					rtp->packet_len = (unsigned short)((p[2]<<8) + p[3]);
				
					p += 4;
					rtp->version = RTP_VER(p[0]);
					rtp->padding = RTP_PAD(p[0]);
					rtp->extension = RTP_EXT(p[0]);
					rtp->csrc_cnt = RTP_CCNT(p[0]);
					rtp->mark = RTP_MARK(p[1]);
					rtp->payload_type = RTP_TYPE(p[1]);
					rtp->seq_num = (unsigned short)((p[2]<<8) + (p[3]<<0));
					rtp->timestamp = (unsigned int)((p[4]<<24) + (p[5]<<16) + (p[6]<<8) + (p[7]<<0));
					rtp->ssrc =  (unsigned int)((p[8]<<24) + (p[9]<<16) + (p[10]<<8) + (p[11]<<0) );
				}



				q->rear += (rtpp.csrc_cnt *4);
				rtpp.packet_len -= (rtpp.csrc_cnt *4 +12);
				//q->rear point to MPEG4 data, rtp.packet_len is length of MPEG4 data
				if(rtpp.payload_type == 96)
					save_size = write(cli->savefd, q->rear, rtpp.packet_len);
				q->rear += rtpp.packet_len; //q->rear point to next RTP header
			}
			else
			{
				//parseRTP(q->rear, &rtp);
				{
					unsigned char *p = tmp;
					struct RTPpacket *rtp = & rtpp;
					rtp->sign[0] = p[0];
					rtp->sign[1] = p[1];
					rtp->packet_len = (unsigned short)((p[2]<<8) + p[3]);
				
					p += 4;
					rtp->version = RTP_VER(p[0]);
					rtp->padding = RTP_PAD(p[0]);
					rtp->extension = RTP_EXT(p[0]);
					rtp->csrc_cnt = RTP_CCNT(p[0]);
					rtp->mark = RTP_MARK(p[1]);
					rtp->payload_type = RTP_TYPE(p[1]);
					rtp->seq_num = (unsigned short)((p[2]<<8) + (p[3]<<0));
					rtp->timestamp = (unsigned int)((p[4]<<24) + (p[5]<<16) + (p[6]<<8) + (p[7]<<0));
					rtp->ssrc =  (unsigned int)((p[8]<<24) + (p[9]<<16) + (p[10]<<8) + (p[11]<<0) );
				}
				q->rear += 4; // skip sign[] & packet_len
				if( (q->rear +rtpp.packet_len) > q->rpoint)
				{
					q->rear += (rtpp.csrc_cnt *4);
					rtpp.packet_len -= (rtpp.csrc_cnt *4 +12);
					int len = q->rpoint -q->rear;
					if(rtpp.payload_type == 96)
						save_size = write(cli->savefd, q->rear, len);
					q->rear = q->buf;
					if(rtpp.payload_type == 96)
						save_size += write(cli->savefd, q->rear, rtpp.packet_len -len);
					q->rear += (rtpp.packet_len -len);
					q->rpoint = NULL;
				}
				else
				{
					q->rear += (rtpp.csrc_cnt *4);
					rtpp.packet_len -= (rtpp.csrc_cnt *4 +12);
					if(rtpp.payload_type == 96)
						save_size = write(cli->savefd, q->rear, rtpp.packet_len);
					q->rear += rtpp.packet_len;
				}
			}
		}
		else
		{
			//parseRTP(q->rear, &rtp);
			{
				unsigned char *p = tmp;
				struct RTPpacket *rtp = & rtpp;
				rtp->sign[0] = p[0];
				rtp->sign[1] = p[1];
				rtp->packet_len = (unsigned short)((p[2]<<8) + p[3]);
			
				p += 4;
				rtp->version = RTP_VER(p[0]);
				rtp->padding = RTP_PAD(p[0]);
				rtp->extension = RTP_EXT(p[0]);
				rtp->csrc_cnt = RTP_CCNT(p[0]);
				rtp->mark = RTP_MARK(p[1]);
				rtp->payload_type = RTP_TYPE(p[1]);
				rtp->seq_num = (unsigned short)((p[2]<<8) + (p[3]<<0));
				rtp->timestamp = (unsigned int)((p[4]<<24) + (p[5]<<16) + (p[6]<<8) + (p[7]<<0));
				rtp->ssrc =  (unsigned int)((p[8]<<24) + (p[9]<<16) + (p[10]<<8) + (p[11]<<0) );
			}

DDPRINTF(" xxx 1. type=%d, packet_len=%d, ccnt=%d\n", rtpp.payload_type, rtpp.packet_len, rtpp.csrc_cnt);

			q->rear += (rtpp.csrc_cnt *4);
			rtpp.packet_len -= (rtpp.csrc_cnt *4 +12);

DDPRINTF(" xxx 2. type=%d, packet_len=%d, ccnt=%d\n", rtpp.payload_type, rtpp.packet_len, rtpp.csrc_cnt);

			if(rtpp.payload_type == 96)
				save_size = write(cli->savefd, q->rear, rtpp.packet_len);
			q->rear += rtpp.packet_len;

			total_size += save_size;
			if(i++<100 && cli->num==0)
			{
				DDPRINTF("\033[%dm %d. pack_len=%d, save %d bytes, total %d bytes \033[m\n",
					32+cli->num, i, rtpp.packet_len, save_size, total_size);
			}

			while(q->rear >= q->front)
			{
				usleep(10*1000);
			}
		}

		pthread_mutex_lock(&cli->r_mutex);
		dq_loop = (cli->playing == 1);
		pthread_mutex_unlock(&cli->r_mutex);
	}
	close(cli->savefd);
	return (void *)0;
}
#endif


#if 0
int deRTPpacket_tcp(struct RTSPClient *cli, int len)
{
	struct RTPpacket rtp;
	ssize_t _size;
	struct Queue *q = cli->q;
	unsigned char *p = q->buf;
	unsigned char *f;
	static int i=0;
	int left = len;

	while(1)
	{
		if(left < 16)
		{
			int r = recv(cli->udpfd, q->front, (16-left), 0);
			left = 16;
			q->front += (16-left);

DDPRINTF("when (left -16)=%d, r=%d, p[]=%02x %02x %02x %02x %02x %02x %02x %02x\n",
	(left-16), r, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
		}

		//parseRTP(p, &rtp);
		{
			struct RTPpacket *rtpp = &rtp;
			rtpp->sign[0] = p[0];
			rtpp->sign[1] = p[1];
			rtpp->packet_len = (unsigned short)((p[2]<<8) + p[3]);
		
			//p += 4;
			rtpp->version = RTP_VER(p[0+4]);
			rtpp->padding = RTP_PAD(p[0+4]);
			rtpp->extension = RTP_EXT(p[0+4]);
			rtpp->csrc_cnt = RTP_CCNT(p[0+4]);
			rtpp->mark = RTP_MARK(p[1+4]);
			rtpp->payload_type = RTP_TYPE(p[1+4]);
			rtpp->seq_num = (unsigned short)((p[2+4]<<8) + (p[3+4]<<0));
			rtpp->timestamp = (unsigned int)((p[4+4]<<24) + (p[5+4]<<16) + (p[6+4]<<8) + (p[7+4]<<0));
			rtpp->ssrc =  (unsigned int)((p[8+4]<<24) + (p[9+4]<<16) + (p[10+4]<<8) + (p[11+4]<<0) );
		}


//DDPRINTF("xxxxxxxxxx left=%d, rtp.packet_len=%d\n", left, (unsigned int)rtp.packet_len);

		int _len = left -4 -(unsigned int)rtp.packet_len;
		if(_len < 0)
		{
			_len = -_len;
			recv(cli->udpfd, q->front, _len, 0);
			q->front += _len;
			left += _len;
		}

		if(rtp.payload_type != 96) //not RTP MPEG4/H264 packet
		{
			p += (4 +rtp.packet_len);

//DDPRINTF("............. not MPEG4 packet, packet_len=%d, left=%d\n", rtp.packet_len, left);
//sleep(1);

			left -= (4 +rtp.packet_len);
			if(left == 0)
				break;
			continue;
		}

		p += (4 +12 +rtp.csrc_cnt *4); //point to MPEG4 data
		_len = rtp.packet_len -(12 +rtp.csrc_cnt *4);
		errno = 0;
		_size = write(cli->savefd, p, _len);
		if(_size != _len)
		{
			DDPRINTF("\033[1;31m [%s] write failed, err is '%s' \033[m \n", __func__, strerror(errno));
exit(1);
		}
		else
		{
			//DDPRINTF("[%s] %d bytes saved", __func__, save_size);
		}
		p += _len;
		left -= (4 +rtp.packet_len);
		if(left == 0)
			break;
	}
	return 0;
}
#endif

int processRTCP(struct RTSPClient *cli, int len, char *buf, int stream)
{
//	struct Queue *q = cli->q;
//	unsigned char *p = q->buf;
	unsigned char *p = (unsigned char *)buf;

	if(p[1] != 0xC8)
		return -1;

	//--- RTCP -------------------- Sender's Report
	struct RTCP_SR_packet *sr = (struct RTCP_SR_packet *)p;

	if(cli->SenderSSRC[stream>>1] != ntohl(sr->senderSSRC ) )
		return -2;

	struct RTCP_RR_packet rr;
	memset((void *)&rr, 0, sizeof(rr));
	rr.first_byte = 0x81;
	rr.packet_type = 0xC9;
	rr.len = htons(7);
	rr.senderSSRC = htonl(cli->SenderSSRC[stream>>1]);
	rr.SSRC = htonl(cli->mySSRC[stream>>1]);
	memset(&rr.fraction, 0, 4);
	rr.seq_num = 0;
	rr.highest_seq_num = htons(cli->rtp_seq);
	rr.jitter = cli->jitter;
	rr.last_sr = *(unsigned int *)((char *)(&sr->timestampMSW) +2);
	rr.delay_timestamp = 0;
	rr.sdes.first_byte = 0x81;
	rr.sdes.packet_type = 0xCA;
	rr.sdes.len = htons(5);
	rr.sdes.SSRC = htonl(cli->SenderSSRC[stream>>1]);
	rr.sdes.type = 1;
	rr.sdes.sdes_len = 12;
	memcpy(&rr.sdes.cname, myCNAME, 12);
	rr.sdes.end = 0;
	write(cli->RTPfd[stream], (char *)&rr, sizeof(rr));
	return 0;

}




#define DEBUG_TIMER
//#define DEBUG_WRITE

//debug
#if defined(DEBUG_WRITE)
int d_fda=0, d_fdv=0;
#endif


void exit_client(struct RTSPClient *cli)
{
	static int ren = 0;

	if(ren == 1 || !cli || cli->num==-1)
		return;

	ren = 1;

	cli->playing = 0;
	usleep(300*1000);

	shutdown(cli->sockfd, SHUT_RDWR);
	close(cli->sockfd);
	cli->sockfd = -1;

	int i;
	for(i=0; i<4; i++)
	{
		shutdown(cli->RTPfd[i], SHUT_RDWR);
		close(cli->RTPfd[i]);
		cli->RTPfd[i] = -1;
	}

	if(cli->data_thread)
	{
		pthread_cancel(cli->data_thread);
		cli->data_thread = 0;
	}

	DDPRINTF("\033[35m [%s] thread %d exit.... \033[m \n", __func__, cli->num);

	cli->num = -1;

	pthread_mutex_lock(&clients_mutex);
	rtsp_client_num--;
	pthread_mutex_unlock(&clients_mutex);

	ren = 0;
}


static int v_ready[2] = {0, 0};

void *processData(void *param)
{
	struct RTPpacket_udp rtp;
	struct RTSPClient *cli = (struct RTSPClient *)param;
	static unsigned int last_rtp_timestamp[2]={0,0};
	static int video_frame_start[2] = {1, 1};
	static int audio_frame_start[2] = {1, 1};
	static unsigned int last_seq_num[2] = {0xFFFFFFFF, 0xFFFFFFFF};
	static time_t last_time[2];

#if defined(DEBUG_WRITE)
static int d_len[2] = {0, 0};
#endif

	while(cli->playing == 1)
	{
		if(cli->b->haveData > BL_NUM/2)
		{
			DDPRINTF("\033[1;31m[%s]\033[m System busy ?!!  %d\n", __func__, cli->b->haveData);
		}

		pthread_mutex_lock(&cli->data_mutex);

		do
		{
			if(cli->b->haveData)
			{
				struct BufLink *bl = &cli->b->BUF[cli->b->front];
				unsigned char *p = bl->buf;
				unsigned char *bp = p +bl->raw_len;

				rtp.version = RTP_VER(p[0]);
				rtp.padding = RTP_PAD(p[0]);
				rtp.extension = RTP_EXT(p[0]);
				rtp.csrc_cnt = RTP_CCNT(p[0]);
				rtp.mark = RTP_MARK(p[1]);
				rtp.payload_type = RTP_TYPE(p[1]);
				rtp.seq_num = (unsigned short)((p[2]<<8) + (p[3]<<0));
				rtp.timestamp = (unsigned int)((p[4]<<24) + (p[5]<<16) + (p[6]<<8) + (p[7]<<0));
				rtp.ssrc = (unsigned int)((p[8]<<24) + (p[9]<<16) + (p[10]<<8) + (p[11]<<0) );

				if(rtp.padding == 1)
				{
					int pd_len = *(bp-1);
					bp -= (pd_len +1);
				}
				p += (12 + (rtp.csrc_cnt<<2) ); //point to payload data

				if(rtp.payload_type == cli->ptypeAudio)
				{
					if(audio_frame_start[cli->num] == 1)
					{
						int _alen = bp -p;
						int alen = (_alen >> 6) + ((_alen & 0x3F) > 0);
						if(cli->num==1 && !(catCam0==0 && catCam1==1))
							memset(p, 0xFF, _alen); //audio data of channel 1 set to 0xFF

						p -= 5;
						*(p+0) = 0x00;
						*(p+1) = 0x00;
						*(p+2) = 0x01;
						*(p+3) = 0xC4;
						*(p+4) = (unsigned char)alen;
						audio_frame_start[cli->num] = 0;
					}

					if(rtp.mark == 1)
					{
						*bp++ = 0x00;
						*bp++ = 0x00;
						*bp++ = 0x01;
						*bp++ = 0xC5;
						memset(bp, 0xFF, 55);
						bp += 55;

						cli->audioNum++;
						audio_frame_start[cli->num] = 1;
					}
				}
				else if(rtp.payload_type == cli->ptypeVideo) // video
				{
					/*
					if(noVideo==1 || recordPause==1 || (catCam0==0 && cli->num==0) || (catCam1==0 && cli->num==1))
					{
						cli->b->haveData--;
						break;
					}
					*/

					bl->ptype = 96;
					cli->rtp_seq = ntohs((unsigned short)((p[2]<<8) + (p[3]<<0)));
					cli->jitter = rtp.timestamp - last_rtp_timestamp[cli->num];
					last_rtp_timestamp[cli->num] = rtp.timestamp;

				/*
					int i_frame = 0;
					if(p[0]==0x00 && p[1]==0x00 && p[2]==0x01 && p[3]==0xB0)
					{
						i_frame = 1;
						//DDPRINTF("\033[1;32m[%s]\033[m got video I-Frame @ channel %d\n", __func__, cli->num);
					}
					else
						i_frame = 0;
				*/

					if(video_frame_start[cli->num] == 1)
					{
						pthread_mutex_lock(&cli->av_mutex);
						video_frame_start[cli->num] = 0;
					}

					if(rtp.seq_num && (rtp.seq_num != (last_seq_num[cli->num]+1)) )
					{
						time_t t = time(0);
						DDPRINTF("\033[31m[%s] client %d lost packet ?? last seq=%d, current seq=%d (diff %d sec)\033[m\n", __func__, cli->num, last_seq_num[cli->num], rtp.seq_num, (int)(t-last_time[cli->num]) );
					}
					last_seq_num[cli->num] = rtp.seq_num;
					last_time[cli->num] = time(0);

					if(rtp.mark == 1)
					{
						pthread_cond_signal(&cli->avSync);
						pthread_mutex_unlock(&cli->av_mutex);
						cli->videoNum++;
						video_frame_start[cli->num] = 1;
					}
					else
					{
						if( (bp -p) != 1460)
						{
							DDPRINTF("[%s] video frame maybe lost data ?? expect 1460 bytes but only %d bytes\n", __func__, bp -p);
						}
					}

				/*
				//debug
				if(tmp->num && tmp == cli->b->rear)
				{
					DDPRINTF("\033[1;31m[%s] WARRING : client %d EBUF overflow !!!!\033[m\n", __func__, cli->num);
				}
				*/
				}
				else
				{
					DDPRINTF("\033[1;31m[%s]\033[m STRANGE DATA !!!!!!\n", __func__);
					cli->playing = 0;
					break;
				}

//debug
#if defined(DEBUG_WRITE)
		if(cli->num == 0)
		{
			d_len[0] += write(d_fdv, p, bp-p);
			/*
			if(rtp.payload_type == cli->ptypeVideo) // video
			{
				d_len[0] += write(d_fdv, p, bp-p);
			}
			else if(rtp.payload_type == cli->ptypeAudio) // audio
			{
				d_len[1] += write(d_fda, p, bp-p);
			}
			*/
		}
#endif

				int wsize = bp -p;
				bl->len = wsize;
				bl->skip = p -bl->buf;
				pthread_mutex_lock(&remain_mutex);
				cli->b->data_len += wsize;
				if(bl->ptype == 97)
					cli->b->data_len += (5 +4 +55);

				cli->b->front++;
				if(cli->b->front == BL_NUM)
					cli->b->front = 0;

				cli->b->haveData--;
				if(cli->b->data_len >= (DATA_BLOCK_SIZE-512))
				{
					pthread_cond_signal(&noBuffer);

#if defined(DEBUG_WRITE)
		if(cli->num == 0)
		{
			DDPRINTF("\033[1;32m[%s]\033[m debug write audio %d bytes, video %d bytes, total %d bytes [%d]\n",
				__func__, d_len[1], d_len[0], d_len[0]+d_len[1], DATA_BLOCK_SIZE-512);
		}
#endif

				}
				pthread_mutex_unlock(&remain_mutex);
			}
			else
			{
				pthread_cond_wait(&cli->noData, &cli->data_mutex);
			}
		}while(0);

		pthread_cond_signal(&cli->noData);
		pthread_mutex_unlock(&cli->data_mutex);
	}

	return NULL;
}


void *ReceivingVideo(void *param)
{
	char rtcp_buf[D_SIZE];
	int pack_len;
	struct RTSPClient *cli = (struct RTSPClient *)param;
	int ret, i;

//debug
#if defined(DEBUG_TIMER)
time_t ltime=time(0);
time_t total_secs=0;
#endif

//	struct Queue *q = cli->q;
	struct in_addr saddr;
	inet_aton(cli->s_ip, &saddr);
	struct sockaddr_in sa_server;

	for(i=0; i<1; i++)
	{
		MAKE_SOCKADDR_IN(sa_server, saddr.s_addr, htons(cli->serverPort[i]));
		if (connect(cli->RTPfd[i*2+0], (struct sockaddr*)&sa_server, sizeof(sa_server) ) != 0)
		{
			DDPRINTF("\033[1;31m [%s] client %d connect %d failed\033[m\n", __func__, cli->num, i*2+0);
			exit(-1);
		}

		MAKE_SOCKADDR_IN(sa_server, saddr.s_addr, htons(cli->serverPort[i]+1));
		if (connect(cli->RTPfd[i*2+1], (struct sockaddr*)&sa_server, sizeof(sa_server) ) != 0)
		{
			DDPRINTF("\033[1;31m [%s] client %d connect %d failed\033[m\n", __func__, cli->num, i*2+1);
			exit(-1);
		}
	}


//debug
#if defined(DEBUG_WRITE)
if(cli->num == 0)
	d_fdv = open("source.mp4v", O_TRUNC | O_CREAT | O_RDWR, 0666);
#endif

	struct timeval TimeoutVal;
	fd_set read_fds;
	unsigned int maxfd;
	unsigned char *p;


//debug
#if defined(DEBUG_TIMER)
static time_t st;
static int ic = 0;
st = time(0);
#endif

	int pcnt[2]= {0, 0};
	while(cli->playing == 1)
	{

		maxfd = 0;
		FD_ZERO(&read_fds);

		if(cli->track != -1)
		{
			for(i=0; i<2; i++)
			{
				FD_SET(cli->RTPfd[i], &read_fds);
				if(cli->RTPfd[i] > maxfd)
					maxfd = cli->RTPfd[i];
			}
		}
		else
			break;

		errno = 0;
		TimeoutVal.tv_sec  = 100;
		TimeoutVal.tv_usec = 0;
		ret = select(maxfd +1, &read_fds, NULL, NULL, &TimeoutVal);

		if(ret < 0)
		{
			DDPRINTF("\033[31m [%s] client %d select() for video fail, err is '%s' \033[m\n", __func__, cli->num, strerror(errno));
			break;
		}
		else if(ret == 0)
		{
			DDPRINTF("\033[31m [%s] client %d select() for video timeout.\033[m\n", __func__, cli->num);
			break;
		}

		for(i=0; i<2; i++)
		{
			if(FD_ISSET(cli->RTPfd[i], &read_fds))
			{
				if(i == 0)
				{
					pthread_mutex_lock(&cli->data_mutex);
					p = cli->b->BUF[cli->b->wptr].buf;
					pack_len = read(cli->RTPfd[i], p, D_SIZE);

	if(noVideo==1 || recordPause==1 || (catCam0==0 && cli->num==0) || (catCam1==0 && cli->num==1))
	{
		if(++pcnt[cli->num] >= 1000)
		{
			pcnt[cli->num] = 0;
			DDPRINTF("[%s] skip channel %d video...\n", __func__, cli->num);
		}
		pthread_mutex_unlock(&cli->data_mutex);
		//usleep(300*1000);

		v_ready[cli->num] = 0;
		continue;
	}

	if(v_ready[cli->num] == 0)
	{
		unsigned char *pp = p;
		unsigned char *bp = pp +pack_len;
		unsigned char padding = RTP_PAD(pp[0]);
		unsigned char csrc_cnt = RTP_CCNT(pp[0]);
		if(padding == 1)
		{
			int pd_len = *(bp-1);
			bp -= (pd_len +1);
		}
		pp += (12 + (csrc_cnt<<2) ); //point to payload data

//DDPRINTF("[%s] p=0x%02x, pp=0x%02x %02x %02x %02x %02x %02x %02x\n", __func__, p[0], pp[0], pp[1], pp[2], pp[3], pp[4], pp[5], pp[6]);

		if(pp[0]==0x00 && pp[1]==0x00 && pp[2]==0x01 && pp[3]==0xB0)
			v_ready[cli->num] = 1;
		else
		{
			pthread_mutex_unlock(&cli->data_mutex);
			continue;
		}
	}


//debug
#if defined(DEBUG_TIMER)
	if(cli->num == 0)
		if(++ic >= 1000)
		{
			time_t stt = time(0);
			DDPRINTF("[%s] receive %d packets span %d sec\n", __func__, ic, (int)(stt -st) );
			st = stt;
			ic = 0;
		}
#endif


					if(pack_len > 0)
					{
						struct BufLink *bl = &cli->b->BUF[cli->b->wptr];
					
						bl->raw_len = pack_len;
						cli->b->wptr++;
						if(cli->b->wptr == BL_NUM)
							cli->b->wptr = 0;
					
						cli->b->haveData++;
					
						pthread_cond_signal(&cli->noData);
					}
					else if(pack_len == 0)
					{
						DDPRINTF("\033[1;31m socket %d be closed ?? \033[m\n", cli->num);
						pthread_mutex_lock(&cli->r_mutex);
						cli->playing = 0;
						pthread_mutex_unlock(&cli->r_mutex);
					}
					else
					{
						DDPRINTF("\033[1;31m socket %d error, err is '%s' \033[m\n", cli->num, strerror(errno));
						pthread_mutex_lock(&cli->r_mutex);
						cli->playing = 0;
						pthread_mutex_unlock(&cli->r_mutex);
					}
					pthread_mutex_unlock(&cli->data_mutex);

#if defined(DEBUG_TIMER)
	time_t now = time(0);
	if(now -ltime >= 10)
	{
		total_secs += (now -ltime);
		DDPRINTF("\033[%dm client %d got %d video frames & %d audio frames in %d seconds (total %d seconds now...)\033[m\n", 32+cli->num, cli->num, cli->videoNum, cli->audioNum, (int)(now -ltime), (int)total_secs);
		ltime = now;
		cli->audioNum = cli->videoNum = 0;
	}
#endif
				}
				else
				{
					pack_len = read(cli->RTPfd[i], rtcp_buf, D_SIZE);
					if(pack_len > 0)
						ret = processRTCP(cli, pack_len, rtcp_buf, i);
				}
			}
		}
	}

//debug
#if defined(DEBUG_WRITE)
if(cli->num == 0)
	close(d_fdv);
#endif

	//DDPRINTF("\033[35m [%s] thread %d for video exit.... \033[m \n", __func__, cli->num);
	exit_client(cli);

	return (void *)0;
}



void *ReceivingAudio(void *param)
{
	char rtcp_buf[D_SIZE];
	ssize_t pack_len;
	struct RTSPClient *cli = (struct RTSPClient *)param;
	int ret, i;

	struct in_addr saddr;
	inet_aton(cli->s_ip, &saddr);
	struct sockaddr_in sa_server;

	for(i=1; i<2; i++)
	{
		MAKE_SOCKADDR_IN(sa_server, saddr.s_addr, htons(cli->serverPort[i]));
		if (connect(cli->RTPfd[i*2+0], (struct sockaddr*)&sa_server, sizeof(sa_server) ) != 0)
		{
			DDPRINTF("\033[1;31m [%s] client %d connect %d failed\033[m\n", __func__, cli->num, i*2+0);
			exit(-1);
		}

		MAKE_SOCKADDR_IN(sa_server, saddr.s_addr, htons(cli->serverPort[i]+1));
		if (connect(cli->RTPfd[i*2+1], (struct sockaddr*)&sa_server, sizeof(sa_server) ) != 0)
		{
			DDPRINTF("\033[1;31m [%s] client %d connect %d failed\033[m\n", __func__, cli->num, i*2+1);
			exit(-1);
		}
	}


//debug
#if defined(DEBUG_WRITE)
if(cli->num == 0)
	d_fda = open("source.g726", O_TRUNC | O_CREAT | O_RDWR, 0666);
#endif

	struct timeval TimeoutVal;
	fd_set read_fds;
	unsigned int maxfd;
	unsigned char *p;


//debug
#if defined(DEBUG_TIMER)
static time_t st;
static int ic = 0;
st = time(0);
#endif

	int pcnt[2] = {0, 0};
	while(cli->playing == 1)
	{

		if(!cli || cli->num == -1)
			break;

		maxfd = 0;
		FD_ZERO(&read_fds);

		if(cli->track2 != -1)
		{
			for(i=2; i<4; i++)
			{
				FD_SET(cli->RTPfd[i], &read_fds);
				if(cli->RTPfd[i] > maxfd)
					maxfd = cli->RTPfd[i];
			}
		}
		else
			break;

		errno = 0;
		TimeoutVal.tv_sec  = 100;
		TimeoutVal.tv_usec = 0;
		ret = select(maxfd +1, &read_fds, NULL, NULL, &TimeoutVal);

		if(ret < 0)
		{
			DDPRINTF("\033[31m [%s] client %d select() for audio fail, err is '%s' \033[m\n", __func__, cli->num, strerror(errno));
			break;
		}
		else if(ret == 0)
		{
			DDPRINTF("\033[31m [%s] client %d select() for audio timeout.\033[m\n", __func__, cli->num);
			break;
		}

	pthread_mutex_lock(&cli->av_mutex);
	pthread_cond_wait(&cli->avSync, &cli->av_mutex);

		for(i=2; i<4; i++)
		{
			if(FD_ISSET(cli->RTPfd[i], &read_fds))
			{
				//pthread_mutex_lock(&remain_mutex[cli->num]);
				if(i == 2)
				{
					pthread_mutex_lock(&cli->data_mutex);
					p = cli->b->BUF[cli->b->wptr].buf;
					pack_len = read(cli->RTPfd[i], p, D_SIZE);

if(noAudio==1 || recordPause==1)
{
	if(++pcnt[cli->num] >= 300)
	{
		pcnt[cli->num] = 0;
		DDPRINTF("[%s] skip channel %d audio...\n", __func__, cli->num);
	}
	pthread_mutex_unlock(&cli->data_mutex);
	pthread_mutex_unlock(&cli->av_mutex);
	continue;
}

if(v_ready[cli->num] == 0)
{
	pthread_mutex_unlock(&cli->data_mutex);
	pthread_mutex_unlock(&cli->av_mutex);
	continue;
}

//debug
#if defined(DEBUG_TIMER)
	if(cli->num == 0)
		if(++ic >= 300)
		{
			time_t stt = time(0);
			DDPRINTF("[%s] receive %d packets span %d sec\n", __func__, ic, (int)(stt -st) );
			st = stt;
			ic = 0;
		}
#endif
	




					if(pack_len > 0)
					{
						struct BufLink *bl = &cli->b->BUF[cli->b->wptr];
					
						bl->raw_len = pack_len;
						cli->b->wptr++;
						if(cli->b->wptr == BL_NUM)
							cli->b->wptr = 0;
					
						cli->b->haveData++;
					
						pthread_cond_signal(&cli->noData);
					}
					else if(pack_len == 0)
					{
						DDPRINTF("\033[1;31m client %d socket %d be closed ?? \033[m\n", cli->num, i);
						break;
					}
					else
					{
						DDPRINTF("\033[1;31m client %d socket %d error, err is '%s' \033[m\n", cli->num, i, strerror(errno));
						break;
					}
					pthread_mutex_unlock(&cli->data_mutex);
				}
				else
				{
					pack_len = read(cli->RTPfd[i], rtcp_buf, D_SIZE);
					if(pack_len > 0)
						ret = processRTCP(cli, pack_len, rtcp_buf, i);
				}

			}
		}
	pthread_mutex_unlock(&cli->av_mutex);

	}

//debug
#if defined(DEBUG_WRITE)
if(cli->num == 0)
	close(d_fda);
#endif

	//DDPRINTF("\033[31m [%s] thread %d for audio exit.... \033[m \n", __func__, cli->num);
	exit_client(cli);

	return (void *)0;
}


int send_PLAY(struct RTSPClient *cli)
{
	char buffer[1024];

	bzero(buffer, sizeof(buffer));

	sprintf(buffer,
"PLAY rtsp://%s/%s/ RTSP/1.0\r\n\
CSeq: %d\r\n\
Session: %d\r\n\
Range: npt=0.000-\r\n\
User-Agent: rtsp-client lite\r\n\r\n",
	cli->s_ip, cli->tail, cli->cseq, cli->session);

	errno = 0;
	if(send(cli->sockfd, buffer, strlen(buffer), 0) < 0)
	{
		DDPRINTF("send CSeq %d failed, err is '%s'\n", cli->cseq, strerror(errno));
		return -1;
	}

	bzero(buffer, sizeof(buffer));
	recv(cli->sockfd, buffer, sizeof(buffer), 0);

//DDPRINTF("\033[1;32mPLAY\033[m got --\n'%s'\n", buffer);

	if(strstr(buffer, "200 OK") == NULL)
	{
		DDPRINTF("\033[1;31m server not ready.\033[m\n");
		return -2;
	}

	char *ptr = strstr(buffer, "ssrc=");
	if(ptr != NULL)
	{
		cli->SenderSSRC[0] = atoi(ptr+5);
		ptr = strstr(ptr+5, "ssrc=");
		cli->SenderSSRC[1] = atoi(ptr+5);
	}

	cli->cseq++;
	pthread_mutex_lock(&cli->r_mutex);
	cli->playing = 1;
	pthread_mutex_unlock(&cli->r_mutex);

	cli->b = &ebuf[cli->num];

//create a thread for receive data
{
    int res;

	errno = 0;
	res = pthread_create(&cli->video_thread, NULL/*&thread_attr*/, ReceivingVideo, (void *)cli);
	if (res != 0)
	{
		printf("ReceivingVideo thread for client %d creation failed, err is '%s'\n", cli->num, strerror(errno));
		return -2;
	}
	usleep(1);

	errno = 0;
	res = pthread_create(&cli->audio_thread, NULL/*&thread_attr*/, ReceivingAudio, (void *)cli);
	if (res != 0)
	{
		printf("ReceivingAudio thread for client %d creation failed, err is '%s'\n", cli->num, strerror(errno));
		return -2;
	}
	usleep(1);

	errno = 0;
	res = pthread_create(&cli->data_thread, NULL/*&thread_attr*/, processData, (void *)cli);
	if (res != 0)
	{
		printf("processData thread for client %d creation failed, err is '%s'\n", cli->num, strerror(errno));
		return -2;
	}
	usleep(1);

}


	return 0;
}

int send_TEARDOWN(struct RTSPClient *cli)
{
	char buffer[1024];

	pthread_mutex_lock(&cli->r_mutex);
	cli->playing = 0;
	pthread_mutex_unlock(&cli->r_mutex);
	usleep(10*1000);

	bzero(buffer, sizeof(buffer));

	sprintf(buffer,
"TEARDOWN rtsp://%s/%s/ RTSP/1.0\r\n\
CSeq: %d\r\n\
Session: %d\r\n\
User-Agent: rtsp-client lite\r\n\r\n",
cli->s_ip, cli->tail, cli->cseq, cli->session);


//DDPRINTF("\033[1;31m %s \033[m \n", buffer);

	errno = 0;
	if(send(cli->sockfd, buffer, strlen(buffer), 0) < 0)
	{
		DDPRINTF("send CSeq %d failed, err is '%s'\n", cli->sockfd, strerror(errno));
		return -1;
	}

	bzero(buffer, sizeof(buffer));
	recv(cli->sockfd, buffer, sizeof(buffer), 0);
	DDPRINTF("\033[1;32mTEARDOWN\033[m got --\n'%s'\n", buffer);

	cli->cseq = 1;

	return 0;
}

/*
int send_GET()
{
}

int send_SET_PARAMETER()
{
}

int send_PAUSE()
{
}

int send_REDIRECT()
{
}
*/

int create_client(struct RTSPClient *cli)
{
	struct in_addr saddr;
	int ret, i;

	//---------- create RTsP socket ---------------
	errno = 0;
	cli->sockfd = socket(AF_INET, SOCK_STREAM/*SOCK_DGRAM*/, 0);
	if (cli->sockfd < 0)
	{
		DDPRINTF("[%s] unable to create socket for client %d: ret=%d, err is '%s'\n", __func__, cli->num, cli->sockfd, strerror(errno));
		return cli->sockfd;
	}

	inet_aton(cli->s_ip, &saddr);
	struct sockaddr_in sa_server;
	MAKE_SOCKADDR_IN(sa_server, saddr.s_addr, htons(cli->RTSPport));

	errno = 0;
	if (connect(cli->sockfd, (struct sockaddr*)&sa_server, sizeof(sa_server) ) != 0)
	{
    	DDPRINTF("[%s] connect failed, err is '%s'\n", __func__, strerror(errno));
		DDPRINTF(" ... ip='%s', port=%d\n", cli->s_ip, cli->RTSPport);
		return -1;
	}

	do
	{
		ret = send_OPTIONS(cli);
		if(ret != 0)
			break;

		if(cli->method & METHOD_DESCRIBE)
		{
			ret = send_DESCRIBE(cli);
			if(ret != 0)
			{
				//break;
				cli->num = -1;
				return -1;
			}
		}
		else
			break;

		//------------------ prepare UTP socket --------------------
		int reuseaddr = 1;
		socklen_t reuseaddr_len = sizeof(reuseaddr);
		if(cli->track != -1)
		{
			i = 0;
			errno = 0;
			cli->RTPfd[i] = socket(AF_INET, SOCK_DGRAM, 0);
			if(cli->RTPfd[i] < 0)
			{
				DDPRINTF("[%s] unable to create RTP socket %d for client %d: ret=%d, err is '%s'\n", __func__, i, cli->num, cli->RTPfd[i], strerror(errno));
				return cli->RTPfd[i];
			}

			struct in_addr caddr;
			inet_aton(cli->c_ip, &caddr);
			struct sockaddr_in sa_client;
			cli->clientPort[0] = 30001 +cli->num*10; // hard code
			MAKE_SOCKADDR_IN(sa_client, caddr.s_addr, htons(cli->clientPort[0]));
			/*
			sa_client.sin_family = AF_INET;
			sa_client.sin_addr.s_addr = htonl(INADDR_ANY);
			sa_client.sin_port = htons(cli->clientPort[0]);
			*/
			setsockopt(cli->RTPfd[i], SOL_SOCKET, SO_REUSEADDR, &reuseaddr, reuseaddr_len);

			errno = 0;
			ret = bind(cli->RTPfd[i], (struct sockaddr *)&sa_client, sizeof(struct sockaddr));
			//if (ret==Err_SOCKET) {
			if(ret != 0)
			{
				fprintf(stderr,"[%s] bind port %d fail because '%s'\n", __func__, cli->clientPort[0], strerror(errno));
				close(cli->RTPfd[i]);
				return -1;
			}

			i = 1;
			errno = 0;
			cli->RTPfd[i] = socket(AF_INET, SOCK_DGRAM, 0);
			if(cli->RTPfd[i] < 0)
			{
				DDPRINTF("[%s] unable to create RTP socket %d for client %d: ret=%d, err is '%s'\n", __func__, i, cli->num, cli->RTPfd[i], strerror(errno));
				return cli->RTPfd[i];
			}
			
			//struct in_addr caddr;
			inet_aton(cli->c_ip, &caddr);
			//struct sockaddr_in sa_client;
			//cli->clientPort[0] = 30001; // hard code
			MAKE_SOCKADDR_IN(sa_client, caddr.s_addr, htons(cli->clientPort[0]+1));
			/*
			sa_client.sin_family = AF_INET;
			sa_client.sin_addr.s_addr = htonl(INADDR_ANY);
			sa_client.sin_port = htons(cli->clientPort[0]);
			*/
			setsockopt(cli->RTPfd[i], SOL_SOCKET, SO_REUSEADDR, &reuseaddr, reuseaddr_len);
			errno = 0;
			ret = bind(cli->RTPfd[i], (struct sockaddr *)&sa_client, sizeof(struct sockaddr));
			if(ret != 0)
			{
				fprintf(stderr,"[%s] bind port %d fail because '%s'\n", __func__, cli->clientPort[0]+1, strerror(errno));
				close(cli->RTPfd[i]);
				return -1;
			}
		}

		if(cli->track2 != -1)
		{
			i = 2;
			errno = 0;
			cli->RTPfd[i] = socket(AF_INET, SOCK_DGRAM, 0);
			if(cli->RTPfd[i] < 0)
			{
				DDPRINTF("[%s] unable to create RTP socket %d for client %d: ret=%d, err is '%s'\n", __func__, i, cli->num, cli->RTPfd[i], strerror(errno));
				return cli->RTPfd[i];
			}

			struct in_addr caddr;
			inet_aton(cli->c_ip, &caddr);
			struct sockaddr_in sa_client;
			cli->clientPort[1] = 30005 +cli->num*10; // hard code
			MAKE_SOCKADDR_IN(sa_client, caddr.s_addr, htons(cli->clientPort[1]));
			setsockopt(cli->RTPfd[i], SOL_SOCKET, SO_REUSEADDR, &reuseaddr, reuseaddr_len);
			errno = 0;
			ret = bind(cli->RTPfd[i], (struct sockaddr *)&sa_client, sizeof(struct sockaddr));
			if(ret != 0)
			{
				fprintf(stderr,"[%s] bind port %d fail because '%s'\n", __func__, cli->clientPort[1], strerror(errno));
				close(cli->RTPfd[i]);
				return -1;
			}

			i = 3;
			errno = 0;
			cli->RTPfd[i] = socket(AF_INET, SOCK_DGRAM, 0);
			if(cli->RTPfd[i] < 0)
			{
				DDPRINTF("[%s] unable to create RTP socket %d for client %d: ret=%d, err is '%s'\n", __func__, i, cli->num, cli->RTPfd[i], strerror(errno));
				return cli->RTPfd[i];
			}
			
			inet_aton(cli->c_ip, &caddr);
			MAKE_SOCKADDR_IN(sa_client, caddr.s_addr, htons(cli->clientPort[1]+1));
			setsockopt(cli->RTPfd[i], SOL_SOCKET, SO_REUSEADDR, &reuseaddr, reuseaddr_len);
			errno = 0;
			ret = bind(cli->RTPfd[i], (struct sockaddr *)&sa_client, sizeof(struct sockaddr));
			if(ret != 0)
			{
				fprintf(stderr,"[%s] bind port %d fail because '%s'\n", __func__, cli->clientPort[1]+1, strerror(errno));
				close(cli->RTPfd[i]);
				return -1;
			}
		}


		if(cli->method & METHOD_SETUP)
		{
			ret = send_SETUP(cli);
			if(ret != 0)
				break;
		}
		else
			break;

		if(cli->method & METHOD_PLAY)
		{
			ret = send_PLAY(cli);
			if(ret != 0)
				break;
			pthread_mutex_lock(&clients_mutex);
			rtsp_client_num++;
			pthread_mutex_unlock(&clients_mutex);
		}
		else
			break;

	} while(0);

	return 0;
}


int get_self_ip(char *buff)
{
    int sockfd;
    int n, ret;
    struct sockaddr_in *addr;
    struct ifreq if_info;

    for(n=0; n<9; n++)
    {
        sockfd =socket(AF_INET, SOCK_DGRAM, 0);
        //strcpy(if_info.ifr_name, "eth0");
        sprintf(if_info.ifr_name, "eth%d", n);
		ret = ioctl(sockfd, SIOCGIFADDR, &if_info);
        shutdown(sockfd, SHUT_RDWR);
        close(sockfd);
        if(ret == 0)
       	{
	        addr = (struct sockaddr_in *)&if_info.ifr_addr;
    	    inet_ntop(AF_INET, &addr->sin_addr, buff, INET_ADDRSTRLEN) ;
			//printf("ret=%d, ip=%s\n", ret, buff);
			return 0;
       	}
    }

    return -1;
}


#if 0 // stand along code
int main(int argc, char *argv[])
{
	int i, j;

	char *ip, *tail;
	//char _ip[]="192.168.5.97";
	//char _tail[CLIENTS][64]={"stream1"};
	char _ip[]="192.168.5.216";
	char _tail[CLIENTS][64]={"live0-1.sdp"};

	pthread_mutex_init(&remain_mutex, NULL);

	if(argc > 2)
	{
		ip = argv[1];
		tail = argv[2];
	}
	else if(argc > 1)
	{
		ip = argv[1];
		tail = _tail[0];
	}
	else
	{
		ip = _ip;
		tail = _tail[0];
	}

	bzero(self_ip, INET_ADDRSTRLEN);
	int ret = get_self_ip(self_ip);
	DDPRINTF("get_self_ip return %d, self_ip=%s\n", ret, self_ip);

	bzero((char *)rc, sizeof(rc));
	for(i=0; i<CLIENTS; i++) // 2 clients for now
	{
		rc[i].cseq = 1;
		rc[i].s_ip = ip;
		rc[i].c_ip = self_ip;
		rc[i].RTSPport = 554;
		sprintf(_tail[i], "live%d-1.sdp", i);
		rc[i].tail = _tail[i];
		rc[i].num = i;
		pthread_mutex_init(&rc[i].r_mutex, NULL);
	}
	
	for(i=0; i<CLIENTS; i++)
	{
		create_client(&rc[i]);
	}

	sleep(10);

	for(i=0; i<CLIENTS; i++)
	{
		send_TEARDOWN(&rc[i]);
	}	

/*
	for(j=0; j<CLIENTS; j++)
	{
		if(rc[j].data_thread > 0)
			pthread_join(rc[j].data_thread, NULL);
	}
*/
	fflush(stderr);

	for(i=0; i<CLIENTS; i++)
	{
		shutdown(rc[i].sockfd, SHUT_RDWR);
		close(rc[i].sockfd);
		pthread_mutex_destroy(&rc[i].r_mutex);
		shutdown(rc[i].udpfd, SHUT_RDWR);
		close(rc[i].udpfd);
	}

	return 0;
}
#endif



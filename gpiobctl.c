
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/timeb.h>
#include <sys/time.h>   
#include <ctype.h>
#include <signal.h>
#include <poll.h>
#include <math.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h> 
#include <dirent.h>
#include <termios.h>
#include <pthread.h>
#include <errno.h>

#include "gdr.h"
#include "gpioctl.h"
#include "led_btn.h"
#include "systemmsg.h"

#define	PARSE_HEAD			1
#define	PARSE_DATE			2
#define	PARSE_TIME			3
#define	PARSE_VALID			4
#define	PARSE_N0				5
#define	PARSE_N1				6
#define	PARSE_E0				7
#define	PARSE_E1				8
#define	PARSE_SPEED			9
#define	PARSE_DEGREE		10
#define	PARSE_GSX				11
#define	PARSE_GSY				12
#define	PARSE_GSZ				13

#define	PARSE_SV				14
#define	PARSE_SU				15
#define	PARSE_MAX_CN		16
#define	PARSE_MIN_CN		17
#define	PARSE_IR_LED		18

#define	PARSE_CHECKSUM	19
#define	PARSE_END				20



extern pthread_mutex_t	gps_mutex;
char gps_buffer[200];
int gps_init = GPS_INIT_NULL;
int CarSpeed=1;
int CarGSensorX=1;
int CarGSensorY=1;
int CarGSensorZ=1;

extern int GPSStatus;

struct datetime
{
	char MM[2], DD[2];
	char hh[2], mm[2];
	char _20[2], YY[2];
	char _d[1], ss[2];
} __attribute__((packed));

void *thread_gps(void *gpio_pointer)
{
	int fd_gpio, fd232;
	struct termios options;
	char buffer[200];
	int i, len, offset;
	int Speed,GSensorX, GSensorY, GSensorZ;
	int	SpeedFixed, State;
	int Valid=0, TimeInit=0;
	struct datetime ct;
	memset((char *)&ct, 0, sizeof(ct));

//DDPRINTF("[%s] sizeof struct datetime is %d\n", __func__, sizeof(ct));

	ct._20[0] = '2';
	ct._20[1] = '0';
	ct._d[0] = '.';
	int DateIndex=0, TimeIndex=0;

	//memset(CHANGE_DATE, 0, sizeof(CHANGE_DATE));

	fd_gpio = (int)gpio_pointer;

	int cmd;
	cmd = LED_RED; LED_ON(cmd);
	printf("low-a3\n"); sleep(1);
	cmd = LED_RED; LED_OFF(cmd);
	printf("high-a3\n"); sleep(1);
	const char gps_tty[] = "/dev/ttyS1";
	errno = 0;
	if( (fd232 = open(gps_tty,	O_RDWR | O_NOCTTY | O_NDELAY)) < 0)
	{
		DDPRINTF("\033[1;31m[%s]\033[m %s open fail because '%s'\nGPS thread exit.\n", __func__, gps_tty, strerror(errno));
		pthread_exit(0);
	}
	tcgetattr(fd232, &options);
	cfsetispeed(&options, B9600);
	cfsetospeed(&options, B9600);
	options.c_cflag |= (CLOCAL | CREAD);
	tcsetattr(fd232, TCSANOW, &options);

	int bs = sizeof(buffer);
 	while (1)
	{
	  	offset = 0;
	    memset(buffer, 0, bs);

	    while (offset < bs)
	    {
			if( (len = read(fd232, &buffer[offset], bs-offset)) <= 0)
	    		break;

//DDPRINTF("\n[%s] read from %s got %d bytes(offset=%d)\n'%s'\n", __func__, gps_tty, len, offset, &buffer[offset]);

	    	offset += len;
	    	usleep(100*1000);
	    }

	    if (offset<70)
	    	continue;

	    if ((buffer[0] != 0x24)||(buffer[offset-2]!=0x0a)||(buffer[offset-1]!=0x0a))
	    	continue;

	    State = PARSE_HEAD;
	    Speed = 0;
		GSensorX= 0;
		GSensorY= 0;
		GSensorZ= 0;
		SpeedFixed = 0;

	    for(i=0; i<offset; i++)
	    {
	    	switch (State)
			{
				case PARSE_HEAD:
					if (buffer[i] == ',')
						State = PARSE_DATE;
					break;
				case PARSE_DATE:
					if (buffer[i] == ',')
						State = PARSE_TIME;
					else if (DateIndex==0)
						ct.MM[0] = buffer[i];
					else if (DateIndex==1)
						ct.MM[1] = buffer[i];
					else if (DateIndex==2)
						ct.DD[0] = buffer[i];
					else if (DateIndex==3)
						ct.DD[1] = buffer[i];
					else if (DateIndex==4)
						ct.YY[0] = buffer[i];
					else if (DateIndex==5)
						ct.YY[1] = buffer[i];
					DateIndex++;
					break;
				case PARSE_TIME:
					if (buffer[i] == ',')
						State = PARSE_VALID;
					else if (TimeIndex==0)
						ct.hh[0] = buffer[i];
					else if (TimeIndex==1)
						ct.hh[1] = buffer[i];
					else if (TimeIndex==2)
						ct.mm[0] = buffer[i];
					else if (TimeIndex==3)
						ct.mm[1] = buffer[i];
					else if (TimeIndex==4)
						ct.ss[0] = buffer[i];
					else if (TimeIndex==5)
						ct.ss[1] = buffer[i];
					TimeIndex++;
					break;
				case PARSE_VALID:
					if (buffer[i] == 'A')
					{
						Valid = 1;
						if (TimeInit==0)
						{
							cmd = LED_GREEN; LED_ON(cmd);
							TimeInit = 1;
							API_System_Date((char *)&ct);
						}
						GPSStatus = LED_STATUS_GPS_READY;
						printf("GPS ON\n");
					}
					else if (buffer[i] == 'V')
					{
						Valid = 0;
						GPSStatus = LED_STATUS_GPS_NOT_READY;
						//ivan printf("GPS OFF\n");
					}
					else if (buffer[i] == ',')
						State = PARSE_N0;
					break;
				case PARSE_N0:
					if (buffer[i] == ',')
						State = PARSE_N1;
					break;
				case PARSE_N1:
					if (buffer[i] == ',')
						State = PARSE_E0;
					break;
				case PARSE_E0:
					if (buffer[i] == ',')
						State = PARSE_E1;
					break;
				case PARSE_E1:
					if (buffer[i] == ',')
						State = PARSE_SPEED;
					break;
				case PARSE_SPEED:
					if (buffer[i] == ',')
						State = PARSE_DEGREE;
					else if (buffer[i] == '.')
						SpeedFixed = 1;
					else if (!SpeedFixed)
						Speed = Speed * 10 + buffer[i] - '0';
					break;
				case PARSE_DEGREE:
					if (buffer[i] == ',')
						State = PARSE_GSX;
					break;
				case PARSE_GSX:
					if (buffer[i] == ',')
						State = PARSE_GSY;
					else if ((buffer[i] != '-')&&(buffer[i] != '.'))
						GSensorX = GSensorX * 10 + buffer[i] - '0';
					break;
				case PARSE_GSY:
					if (buffer[i] == ',')
						State = PARSE_GSZ;
					else if ((buffer[i] != '-')&&(buffer[i] != '.'))
						GSensorY = GSensorY * 10 + buffer[i] - '0';
					break;
				case PARSE_GSZ:
					if (buffer[i] == ',')
						State = PARSE_SV;
					else if ((buffer[i] != '-')&&(buffer[i] != '.'))
						GSensorZ = GSensorZ * 10 + buffer[i] - '0';
					break;
				case PARSE_SV:
					if (buffer[i] == ',')
						State = PARSE_SU;
					break;
				case PARSE_SU:
					if(buffer[i] == ',')
						State = PARSE_MAX_CN;
					break;
				case PARSE_MAX_CN:
					if(buffer[i] == ',')
						State = PARSE_MIN_CN;
					break;
				case PARSE_MIN_CN:
					if(buffer[i] == ',')
						State = PARSE_IR_LED;
					break;
				case PARSE_IR_LED:
					if (buffer[i] == '1')
					{
						//ivan, 20100902
						//IR_LED_ON;
						//printf("IR ON\n");
					}
					else if (buffer[i] == '0')
					{
						//ivan, 20100902
						//IR_LED_OFF;
						//printf("IR OFF\n");
					}
					if(buffer[i] == ',')
						State = PARSE_CHECKSUM;
					break;
				default:
					break;
			}
	    }

    	pthread_mutex_lock(&gps_mutex);
    	memcpy(gps_buffer,buffer,200);
    	CarSpeed = Speed;
		CarGSensorX	= GSensorX;
		CarGSensorY	= GSensorY;
		CarGSensorZ	= GSensorZ;

		//gps_init只表示有資料可讀. 並不表示定位到了
		if (Valid==1)
			gps_init = GPS_INIT_GPS;
		else
			gps_init = GPS_INIT_GSENSOR;
    	pthread_mutex_unlock(&gps_mutex);

    	usleep(100*1000);
	}

	return 0;
}


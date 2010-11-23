
#ifndef _LARGEFILE64_SOURCE
#define _LARGEFILE64_SOURCE
#endif

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <linux/rtc.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/ip_icmp.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

#include "gdr.h"

#include "gpioctl.h"
#include "led_btn.h"
#include "systemmsg.h"
#include "RTSPc.h"
#include "HTTPc.h"



#define	CONFIG_FLASH	"/home/gdr_config"
#define	MAGIC_FLASH		"/home/gdr_magic"

#if defined(USE_FAKE_USB)
//	#define USB_STORAGE_PATH_0	"./sda"
//	#define USB_STORAGE_PATH_1	"./sdb"
#define USB_STORAGE_PATH_0	"/dev/sdb"
#define USB_STORAGE_PATH_1	"/dev/sdc"
	#define USB_SETTING_AREA	"./mtdblock4"
	#define USB_MAGIC_AREA		"./mtdblock5"
#else
	#define USB_STORAGE_PATH_0	"/dev/usb_3rd_port"
	#define USB_STORAGE_PATH_1	"/dev/usb_4th_port"
	//#define USB_SETTING_AREA	"/dev/mtdblock4"
	//#define USB_MAGIC_AREA		"/dev/mtdblock5"
	#define USB_SETTING_AREA	CONFIG_FLASH
	#define USB_MAGIC_AREA		MAGIC_FLASH
#endif


#define	PATH_RTC	"/dev/rtc0"
#define TIME_FILE	"/home/gdr_time"



#define	FAKE_DISK_SIZE	(1024*1024*256)

#define	RET_SOMETHING_WRONG			0
#define	RET_NO_EVENT_HAPPEN			1
#define	RET_MASK_EVENT_HAPPEN		2


extern struct EBuf ebuf[];

extern char gps_buffer[];
extern int gps_init;
extern int CarSpeed;
extern int CarGSensorX;
extern int CarGSensorY;
extern int CarGSensorZ;
extern int CarSpeed;

extern int noVideo, noAudio;
extern int catCam0, catCam1;
int canPauseRecord, canForceRecord;
extern int recordPause;


#if 0
typedef struct
{
	INDEX_SECTOR_T buf[8];
	INDEX_SECTOR_T *ptr, *prev;
	int pos;
	off64_t off;
} IST_CHAIN_T;
#else
typedef struct
{
	INDEX_SECTOR_T *ptr, *prev;
	off64_t off, prev_off;
} IST_CHAIN_T;
#endif

IST_CHAIN_T istc;
//INDEX_SECTOR_T *currIndex, *prevIndex; //ivan+


DUAL_USB_T GlobalDualUSB;

volatile int StopRecording = 0;
volatile int LEDStatus = LED_STATUS_INIT;
volatile int BuzzerStatus = BUZZER_STATUS_INIT;
volatile int GPSStatus = LED_STATUS_GPS_NOT_READY;
volatile int KeyStroke = 0;
volatile time_t OverGSensor = 0;
volatile int backMarkList = 0;
volatile int forceRecord=0;

int ThresholdNotice = 0;
//extern struct MPG_PARAM *BuzzerDevice;

int fd_rtc = -1;
int fd_gpio = -1;


#if defined(AUTO_SPACE) //ivan modified
int DiskDataBlocks[2] = {0, 0};
off64_t LenDataBlock[2] = {0, 0};
INDEX_SECTOR_T *IndexBuffer[2] = {NULL, NULL};
//int *IndexBufferDirtyFlag[2] = {NULL, NULL};
#else
INDEX_SECTOR_T IndexBuffer[2][DISK_DATA_BLOCKS];
int IndexBufferDirtyFlag[2][DISK_DATA_BLOCKS];
#endif

EVENT_INFO_T EventInfo[16][8];	//16M = 16*(8*128K)=16*8*(128k)
EVENT_INFO_T *EventInfoPtr;
char EventMask[16][8];
char *EventPtr;

pthread_mutex_t	gps_mutex;
extern pthread_mutex_t remain_mutex;
extern pthread_cond_t noBuffer;
extern pthread_mutex_t clients_mutex;
extern struct RTSPClient rc[];
extern char self_ip[];
extern int rtsp_client_num;

extern ENCODER_BUFFER_T EncoderBuffer[];

unsigned char currRes[2], currFrr[2];
unsigned short markTimes;



unsigned long calc_checksum(unsigned long *data, int len)
{
	unsigned long checksum = 0;
	int x;

	for(x=0; x<len; x++)
	{
		checksum ^= *data;
		data++;
	}
	return checksum;
}






int GetUSBStoragePath(DUAL_USB_T *DualUSB)
{
	memset(DualUSB->CarRecorder[0].StoragePath, 0, MAX_STORAGE_PATH);
	strcpy(DualUSB->CarRecorder[0].StoragePath, USB_STORAGE_PATH_0);
	memset(DualUSB->CarRecorder[1].StoragePath, 0, MAX_STORAGE_PATH);
	strcpy(DualUSB->CarRecorder[1].StoragePath, USB_STORAGE_PATH_1);
	return 0;
}

int VBUS_CHECK(void)
{
	//check device connect
#define CONNECT_FILE	"/sys/module/g_file_storage/parameters/connect"
	errno = 0;
	int fd = open(CONNECT_FILE, O_RDONLY);
	if(fd > 0)
	{
		char buf;
		errno = 0;
		size_t rs = read(fd, &buf, 1);
		if(rs > 0)
		{
			if(buf == 'Y' || buf == 'y' || buf == '1' || buf == 1)
			{
				StopRecording = 1;
				DDPRINTF("\033[1;36m[%s] USB slave connected.\033[m\n", __func__);
			}
		}
		else
		{
			DDPRINTF("[%s] '%s' read fail because '%s'.\n", __func__, CONNECT_FILE, strerror(errno));
		}
		close(fd);
	}
	else
	{
		DDPRINTF("[%s] '%s' open fail because '%s'.\n", __func__, CONNECT_FILE, strerror(errno));
	}

	if (StopRecording==1)
	{
		LEDStatus = LED_STATUS_STOP_RECORD;
		while (1)
			sleep(10);
	}
	return 0;
}


//�P�B USB �P Flash �W�� setting.
//return value = number of USB, 0=fail
/*
 * CarMagic
 * Flash �]�N�Ʀr���b���} 0
 * USB�W �]�N�Ʀr���b 0xe000000
 * Magic number is 0xff,0x11,0x22,0x33,magic1,magic2,magic3,magic4
*/

void config_default(USB_SETTING_T *cfg)
{
	memset(cfg, 0, 512);
	cfg->Tag = SETTING_TAG;

	strncpy(cfg->Fw, "V01.00.00", 9);
	strncpy(cfg->XAxis, "5.100", 5);
	strncpy(cfg->YAxis, "5.100", 5);
	strncpy(cfg->ZAxis, "5.100", 5);

	cfg->Limit = 1;
	cfg->Unit  = 0;
	cfg->Value = 110;
	cfg->Record = 3;
	cfg->Alert = 1;
	cfg->AlertItems = 15;
	cfg->DPI = 640;
	cfg->Frame = 30;
	cfg->Auto = 1;
	cfg->ManRec = 3;
	cfg->Scene = 'A';
	cfg->Cover = 1;
	cfg->Reserve = 3;
	cfg->DPI2 = 640;
	cfg->Frame2 = 30;
	cfg->Audio = 1;
	cfg->MarkTimes = 30;
	cfg->Checksum = calc_checksum( (unsigned long *)cfg, (512-4)/4);
}


int usb_slave()
{
#define	CMD_GADGET	"insmod /lib/modules/g_file_storage.ko file="
	
	char cmd[128];
	int flag_check;

	sleep(3);

	memset(cmd, 0, sizeof(cmd));
	flag_check=0;

	sprintf(cmd, CMD_GADGET);
	DDPRINTF("cmd=%s\n",cmd);

	if(0 ==access(USB_STORAGE_PATH_0, R_OK|W_OK)){
		strcat(cmd, USB_STORAGE_PATH_0);
		flag_check=1;
		DDPRINTF("cmd=%s\n", cmd);
	}

	if(0 == access(USB_STORAGE_PATH_1, R_OK|W_OK)){
		if(1 == flag_check) {
			strcat(cmd, ",");
		}else{
			flag_check = 1;
		}
		strcat(cmd, USB_STORAGE_PATH_1);
		DDPRINTF("cmd=%s\n", cmd);
	}


	if(0 == access(CONFIG_FLASH, R_OK|W_OK)){
		if(1 == flag_check) {
			strcat(cmd, ",");
		}else{
			flag_check = 1;
		}
		strcat(cmd, CONFIG_FLASH);
		DDPRINTF("cmd=%s\n", cmd);
	}

	if(0 == access(MAGIC_FLASH, R_OK|W_OK)){
		if(1 == flag_check) {
			strcat(cmd, ",");
		}else{
			flag_check = 1;
		}
		strcat(cmd, MAGIC_FLASH);
		DDPRINTF("cmd=%s\n", cmd);
	}

	if(1 == flag_check) {
		strcat(cmd, " removable=1");
		DDPRINTF("cmd=%s\n", cmd);
		system(cmd);
	}
	return 0;
}


#if defined(AUTO_SPACE)
int SpaceCheck(off64_t *sspace)
{
	const int _absspace[] = {4000, 8000, 16000, 32000, 64000, 128000};
	const int _minspace[] = { 512, 1024,  1536,  2048,	2560,	3072};
	const int _baseline[] = {3300, 6600, 13720, 28470, 58475, 119000};
	const int _abslimit = 3000;

	int i;
	off64_t space = *sspace;
	/*
		�b���n�w�d�a�Ϫ��Ŷ�
		4G --> 512M ==> 3302M ->        3300
		8G --> 1G ==> 6605M -->          6600
		16G --> 1.5G ==> 13722M -->   13720
		32G --> 2G ==> 28469M -->      28470
		64G --> 2.5G ==> 58475M -->   58475
		128G --> 3G ==> 118998M --> 119000
		�Y�u���i���v�Ŷ��p���W�C�Ʀr�h���Xĵ�i
		�Y�w�g�p�� 3G �h���X�Y��ĵ�i�]�L�׭��l�Ŷ��h�j�^
	*/
	space = space /(1024*1024); //bytes -> MB
	for(i=0; i<6; i++)
	{
		if(space > _absspace[i])
			continue;
		space -= _minspace[i];
		break;
	}
	if(i==6)
		space -= 4096;
	if(space < _abslimit)
	{
		DDPRINTF("\033[1;31m[%s]\033[m Fatal warning !! Record space less than 2GB\n", __func__);
		//fatal
	}
	else if(space < _baseline[i])
	{
		DDPRINTF("\033[31m[%s]\033[m Warning : Usable space less than 85%%, suggest install a new pen driver\n", __func__);
		//warning
	}
	*sspace = space*1024*1024;
	return 0;
}
#endif

int GenerateUSBDiskFile(DUAL_USB_T *DualUSB)
{
	int fd;
	USB_SETTING_T USBSetting, FlashSetting;
	int FdFlash, MagicFlash;
	unsigned char Magic[8];
	time_t	MagicTime;
	unsigned long checksum;
	int usb_exist = 0;

	/* ivan note
	 *  �Ѧ��q�i�H�ݥX�A���t�Φ��N flash �e�X mtdblock4 ���x�s USB setting ����
	*/
	// read & repair FlashSetting -------------------------------------

	errno = 0;
#if defined(USE_FAKE_USB)
	FdFlash = open(USB_SETTING_AREA, O_CREAT | O_RDWR | O_SYNC, 0666);
#else
	FdFlash = open(USB_SETTING_AREA, O_RDWR | O_SYNC, 0666);
#endif

	if (FdFlash <0 )
	{
		printf("[%s] Open FlashSetting file fail because '%s'\n", __func__, strerror(errno));
		return -1;
	}

	lseek64(FdFlash, 0x00, SEEK_SET);
	read(FdFlash, &FlashSetting, 512);
	checksum = calc_checksum((unsigned long *)&FlashSetting, (512-4)/4);
	if ( (checksum != FlashSetting.Checksum) || (FlashSetting.Tag != SETTING_TAG) )
	{
		printf("[%s] Flash setting fail, fix now\n", __func__);
		config_default(&USBSetting);
		memcpy(&FlashSetting, &USBSetting, 512);
		lseek64(FdFlash, 0, SEEK_SET);
		write(FdFlash, &FlashSetting, 512);
		sync();
	}

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

	/* ivan note
	 *  �Ѧ��q�i�H�ݥX�A���t�Φ��N flash �e�X mtdblock5 �������m Magic ����
	*/
	// Magic ----------------------------------------------------------

	errno = 0;
#if defined(USE_FAKE_USB)
	MagicFlash = open(USB_MAGIC_AREA, O_CREAT | O_RDWR | O_SYNC, 0666);
#else
	MagicFlash = open(USB_MAGIC_AREA, O_RDWR | O_SYNC, 0666);
#endif

	if (MagicFlash < 0 )
	{
		printf("[%s] Open MagicNumber file fail because '%s'\n", __func__, strerror(errno));
		return -1;
	}
	MagicTime = time(NULL);
	Magic[0]=0xff;
	Magic[1]=0x11;
	Magic[2]=0x22;
	Magic[3]=0x33;
	memcpy(&Magic[4], &MagicTime, 4);
	lseek64(MagicFlash, 0, SEEK_SET);
	write(MagicFlash, Magic, 8);
	close(MagicFlash);
	sync();
	// end of Magic - - - - - - - - - - - - - - - - - - - - - - - - - -


	DualUSB->USBDiskCount = 0;
	// check USB's checksum, if checksum fail, copy FlashSetting to USB and password change to 0x00*12
	errno = 0;
#if defined(USE_FAKE_USB)
//	fd = open(DualUSB->CarRecorder[0].StoragePath, O_CREAT | O_RDWR | O_SYNC, 0666);
	fd = open(DualUSB->CarRecorder[0].StoragePath, O_RDWR | O_SYNC, 0666);
#else
	fd = open(DualUSB->CarRecorder[0].StoragePath, O_RDWR | O_SYNC, 0666);
#endif

	if (fd < 0)
	{
		printf("[%s] USB storage 1 open fail because '%s'\n", __func__, strerror(errno));
		IndexBuffer[0] = (INDEX_SECTOR_T *)malloc(sizeof(INDEX_SECTOR_T));
		//IndexBufferDirtyFlag[0] = (int *)malloc(sizeof(int));
		//return 0;
	}
	else
	{
		usb_exist = 1;
		DualUSB->USBDiskCount++;

#if defined(AUTO_SPACE)
		LenDataBlock[0] = lseek64(fd, 0, SEEK_END);
		lseek64(fd, 0, SEEK_SET);
		SpaceCheck(&LenDataBlock[0]);
		DiskDataBlocks[0] = (LenDataBlock[0] -OFFSET_DATA_BLOCK) /DATA_MACRO_BLOCK;
		//for debug only
		//DiskDataBlocks[0] = 33;

DDPRINTF("\033[1;36m[%s]\033[m DiskSpace of USB 1 : %lld MB, %d blocks\n", __func__, LenDataBlock[0]/1024/1024, DiskDataBlocks[0]);

		IndexBuffer[0] = (INDEX_SECTOR_T *)malloc(DiskDataBlocks[0] *sizeof(INDEX_SECTOR_T));
		//IndexBufferDirtyFlag[0] = (int *)malloc(DiskDataBlocks[0] *sizeof(int));
#endif

		DualUSB->CarRecorder[0].StorageDescriptor = fd;

		//0xC000000 = 192MB, QfCfgTable
		lseek64(fd, QFCFG_TABLE_OFFSET, SEEK_SET);
		errno = 0;
		if(read(fd, &USBSetting, 512) == 512)
		{

//ivan debug
{
	USB_SETTING_T *usb = &USBSetting;
	DDPRINTF("\n\nUSB settings....\n");
	DDPRINTF("Camera 1 -- resolution = %d, frame rate = %d\n", usb->DPI, usb->Frame);
	DDPRINTF("Camera 2 -- resolution = %d, frame rate = %d\n", usb->DPI2, usb->Frame2);
	DDPRINTF("X/Y/Z Axis = (%s, %s, %s)\n", usb->XAxis, usb->YAxis, usb->ZAxis);
	DDPRINTF("Speed limitation enable = %d, unit = %d, speed = %d\n", usb->Limit, usb->Unit, usb->Value);
	DDPRINTF("Video record = %d, Audio record = %d\n", usb->Record, usb->Audio);
	DDPRINTF("Alarm = %d, AlarmType = 0x%02X\n", usb->Alert, usb->AlertItems);
	DDPRINTF("ManRec = 0x%02X, MarkTimes = %d secs\n", usb->ManRec, usb->MarkTimes);
	DDPRINTF("Checksum = 0x%08lX, calc = 0x%08lX\n\n", usb->Checksum, checksum);

#if 0 //demo only, 20101007
	usb->DPI = 640;
	usb->Frame = 30;
	usb->DPI2 = 640;
	usb->Frame2 = 30;
	memcpy(usb->XAxis, "5.100", 5);
	memcpy(usb->YAxis, "5.100", 5);
	memcpy(usb->ZAxis, "5.100", 5);
	usb->Limit = 1;
	usb->Unit = 0;
	usb->Value = 120;
	usb->Record = 3;
	usb->Audio = 1;
	usb->Alert = 1;
	usb->AlertItems = 0x0F;
	usb->ManRec = 3;
#endif

}

			checksum = calc_checksum((unsigned long *)&USBSetting, (512-4)/4);
			if ((checksum != USBSetting.Checksum) || (USBSetting.Tag != SETTING_TAG))
			{
				printf("[%s] USB setting fail, fix now\n", __func__);
				memcpy(&USBSetting, &FlashSetting, 512-4);
				memset(USBSetting.Password, 0, 12);
				USBSetting.Checksum = calc_checksum((unsigned long *)&USBSetting, (512-4)/4);
				lseek64(fd, QFCFG_TABLE_OFFSET, SEEK_SET);
				write(fd, &USBSetting, 512);
				sync();
			}
			//if password OK, copy USBsetting to Flash, then change USB's password to 0x00*12
			//else if (memcmp(USBSetting.Password, FlashSetting.Password, 12) == 0)
			else if(1)
			{
				printf("[%s] Update FlashSetting from USBSetting\n", __func__);
				memcpy(&FlashSetting, &USBSetting, 512);
				lseek64(FdFlash, 0, SEEK_SET);
				write(FdFlash, &FlashSetting, 512);
	
				memset(USBSetting.Password, 0, 12);
				USBSetting.Checksum = calc_checksum((unsigned long *)&USBSetting, (512-4)/4);
				lseek64(fd, QFCFG_TABLE_OFFSET, SEEK_SET);
				write(fd, &USBSetting, 512);
				sync();
			}
			//if password FAIL, copy FlashSetting to USB(password change to 0x00*12)
			else
			{
				memcpy(&USBSetting,&FlashSetting,512);
				memset(USBSetting.Password, 0, 12);
				USBSetting.Checksum = calc_checksum((unsigned long *)&USBSetting, (512-4)/4);
				lseek64(fd, QFCFG_TABLE_OFFSET, SEEK_SET);
				write(fd, &USBSetting, 512);
				printf("[%s] Flash setting update to USB 1\n", __func__);
				sync();
			}
		}
		else
		{
			printf("[%s] read USBSetting fail because '%s'\n", __func__, strerror(errno));
		}
	
		lseek64(fd, MAGIC_OFFSET, SEEK_SET);
		write(fd ,Magic, 8);
		sync();

		catCam0 = ((USBSetting.Record & 0x01) != 0);
		catCam1 = ((USBSetting.Record & 0x02) != 0);
		canForceRecord = ((USBSetting.ManRec & 0x01) != 0);
		canPauseRecord = ((USBSetting.ManRec & 0x02) != 0);

		currRes[0] = (USBSetting.DPI == 320)?RES_N_QVGA:RES_N_VGA;
		currRes[1] = (USBSetting.DPI2 == 320)?RES_N_QVGA:RES_N_VGA;
	
		currFrr[0] = USBSetting.Frame;
		if(currFrr[0] > 30)
			currFrr[0] = 30;
		currFrr[1] = USBSetting.Frame2;
		if(currFrr[1] > 30)
			currFrr[1] = 30;

		markTimes = USBSetting.MarkTimes;

//ivan debug
#if defined(USE_FAKE_USB)
		currRes[0] = RES_N_VGA;
		currFrr[0] = 30;
		currRes[1] = RES_N_VGA;
		currFrr[1] = 30;
#endif


		DualUSB->CarRecorder[0].CarSetting.Limit = USBSetting.Limit;
		DualUSB->CarRecorder[0].CarSetting.Value = USBSetting.Value;
		DualUSB->CarRecorder[0].CarSetting.Resolution = USBSetting.DPI;
		DualUSB->CarRecorder[0].CarSetting.FrameRate = USBSetting.Frame;
		DualUSB->CarRecorder[0].CarSetting.Scene = USBSetting.Scene;
		//DualUSB->CarRecorder[0].CarSetting.Resolution2 = USBSetting.DPI2;
		//DualUSB->CarRecorder[0].CarSetting.FrameRate2 = USBSetting.Frame2;
	
		DualUSB->CarRecorder[0].CarSetting.XAxis = (USBSetting.XAxis[0]-'0')*1000+(USBSetting.XAxis[2]-'0')*100+(USBSetting.XAxis[3]-'0')*10+USBSetting.XAxis[4]-'0';
		DualUSB->CarRecorder[0].CarSetting.YAxis = (USBSetting.YAxis[0]-'0')*1000+(USBSetting.YAxis[2]-'0')*100+(USBSetting.YAxis[3]-'0')*10+USBSetting.YAxis[4]-'0';
		DualUSB->CarRecorder[0].CarSetting.ZAxis = (USBSetting.ZAxis[0]-'0')*1000+(USBSetting.ZAxis[2]-'0')*100+(USBSetting.ZAxis[3]-'0')*10+USBSetting.ZAxis[4]-'0';

#if 0
		DDPRINTF("CarSetting.Limit= %d\n", DualUSB->CarRecorder[0].CarSetting.Limit);
		DDPRINTF("CarSetting.Value= %d\n", DualUSB->CarRecorder[0].CarSetting.Value);
		DDPRINTF("\033[31mCarSetting.Resolution= %d\033[m\n", DualUSB->CarRecorder[0].CarSetting.Resolution);
		DDPRINTF("\033[31mCarSetting.FrameRate= %d\033[m\n", DualUSB->CarRecorder[0].CarSetting.FrameRate);
		DDPRINTF("USBSetting.Resolution2= %d\033[m\n", USBSetting.DPI2);
		DDPRINTF("USBSetting.FrameRate2= %d\033[m\n", USBSetting.Frame2);
		DDPRINTF("CarSetting.XAxis= %d\n", DualUSB->CarRecorder[0].CarSetting.XAxis);
		DDPRINTF("CarSetting.YAxis= %d\n", DualUSB->CarRecorder[0].CarSetting.YAxis);
		DDPRINTF("CarSetting.ZAxis= %d\n", DualUSB->CarRecorder[0].CarSetting.ZAxis);
		DDPRINTF("Camera enable (%d, %d)\n", catCam0, catCam1);
		DDPRINTF("FroceRecord = %d, recordPause = %d\n", canForceRecord, canPauseRecord);
#endif
	}


	errno = 0;
#if defined(USE_FAKE_USB)
//	fd = open(DualUSB->CarRecorder[1].StoragePath, O_CREAT | O_RDWR | O_SYNC, 0666);
	fd = open(DualUSB->CarRecorder[1].StoragePath, O_RDWR | O_SYNC, 0666);
#else
	fd = open(DualUSB->CarRecorder[1].StoragePath, O_RDWR | O_SYNC, 0666);
#endif
	if (fd < 0)
	{
		printf("[%s] USB storage 2 open fail because '%s'\n", __func__, strerror(errno));
		IndexBuffer[1] = (INDEX_SECTOR_T *)malloc(sizeof(INDEX_SECTOR_T));
		//IndexBufferDirtyFlag[1] = (int *)malloc(sizeof(int));
		//return 1;
	}
	else
	{
		usb_exist |= 2;
		DualUSB->USBDiskCount++;

#if defined(AUTO_SPACE)
		LenDataBlock[1] = lseek64(fd, 0, SEEK_END);
		lseek64(fd, 0, SEEK_SET);
		SpaceCheck(&LenDataBlock[1]);
		DDPRINTF("\033[1;36m[%s]\033[m DiskSpace of USB 2 : %lld MB\n", __func__, LenDataBlock[1]/1024/1024);
		DiskDataBlocks[1] = (LenDataBlock[1] -OFFSET_DATA_BLOCK) /DATA_MACRO_BLOCK;
		IndexBuffer[1] = (INDEX_SECTOR_T *)malloc(DiskDataBlocks[1] *sizeof(INDEX_SECTOR_T));
		//IndexBufferDirtyFlag[1] = (int *)malloc(DiskDataBlocks[1] *sizeof(int));
#endif

		DualUSB->CarRecorder[1].StorageDescriptor = fd;
		//printf("USB storage 2 open success\n");
		lseek64(fd, QFCFG_TABLE_OFFSET, SEEK_SET);
		write(fd, &USBSetting, 512);
		printf("[%s] Flash setting update to USB 2\n", __func__);
		sync();

		lseek64(fd, MAGIC_OFFSET, SEEK_SET);
		write(fd, Magic, 8);
		sync();
	}

	usb_slave();

	return usb_exist;
}



//copy from app/led_btn.c
/*
power On �ɭY�������U�C���p�A�ХΫ��򭵴���(�T�@���A���@��)�� Halt �t�ΡC
1. ���� USB �H���г����`�C

���t�ζ}�l���v�ɡA���T�T�u���A���ܨt�ζ}����C
���t�Φb���v�ɡA�Y�������O�����i�ΪŶ��p���ε��� 10%�A���T�T
����(�T�@���A���@�� �T��)�����ϥΪ̰O�����ֺ��F�C���ɨt�Φ۰��ন�������л\�Ҧ��C
�Y�ϥΪ̫������v�ɭP�O�����Ŷ��w���A�h�A�Ϋ��򭵴���(�T�@���A���@��)�� Halt �t�ΡC

����]�w�u�ѤU�W�t�P�ƥ��ⶵ (�i�ƿ�)�C
���ܶW�t�h�t�εo�{�W�t�ɴN�o����C
���ܨƥ��h�t�εo�{�ƥ��ɴN�o����C
���̳����N�O�t�εo�{�W�t�Ψƥ��ɳ��|�o����C
������T�@���A���@���C
*/


int ThreadBuzzerControl(void *gpio_pointer)
{
/*
	int fd;
	fd = (int)gpio_pointer;
	while (BuzzerDevice == (struct MPG_PARAM *)(-1))
		usleep(300000);
*/

	int fd_pwm;

	errno = 0;
	fd_pwm=open(SQ_PWM, O_RDWR);
	if(fd_pwm <= 0)
	{
		printf("\033[1;31m[%s]\033[m pwm_node open fail because '%s'\n", __func__, strerror(errno));
		pthread_exit(NULL);
	}
	else
	{
		errno = 0;
		if(ioctl(fd_pwm, SQ_PWM_RESET) < 0)
		{
			printf("\033[1;31m[%s]\033[m pwm io error because '%s\n", __func__, strerror(errno));
			pthread_exit(NULL);
		}
	}



	while (1)
	{
		switch (BuzzerStatus)
		{
			case BUZZER_STATUS_INIT:
				sleep(1);
				break;

			case BUZZER_STATUS_START_RECORDING:
DDPRINTF("[%s]  BUZZER_STATUS_START_RECORDING\n", __func__);
				BUZZER_ON;
				usleep(200*1000);
				BUZZER_OFF;
				//usleep(3*1000*1000);
				BuzzerStatus = BUZZER_STATUS_INIT;
				break;

			case BUZZER_STATUS_SPACE_THRESHOLD:
DDPRINTF("[%s]  BUZZER_STATUS_SPACE_THRESHOLD, ThresholdNotice=%d\n", __func__, ThresholdNotice);
				if (!ThresholdNotice)
				{
					BUZZER_ON;
					sleep(1);
					BUZZER_OFF;
					sleep(1);
					BUZZER_ON;
					sleep(1);
					BUZZER_OFF;
					sleep(1);
					BUZZER_ON;
					sleep(1);
					BUZZER_OFF;
					sleep(1);
					BuzzerStatus = BUZZER_STATUS_INIT;
					ThresholdNotice = 1;
				}
				break;

			case BUZZER_STATUS_OVER_SPEED:
DDPRINTF("[%s] BUZZER_STATUS_OVER_SPEED\n", __func__);
				BUZZER_ON;
				usleep(300*1000);
				BUZZER_OFF;
				usleep(300*1000);
				BUZZER_ON;
				usleep(300*1000);
				BUZZER_OFF;
				BuzzerStatus = BUZZER_STATUS_INIT;
				break;

			case BUZZER_STATUS_GSENSOR:
DDPRINTF("[%s] BUZZER_STATUS_GSENSOR\n", __func__);
				BUZZER_ON;
				sleep(1);
				BUZZER_OFF;
				BuzzerStatus = BUZZER_STATUS_INIT;
				break;

			case BUZZER_STATUS_NO_USBDISK:
DDPRINTF("[%s] BUZZER_STATUS_NO_USBDISK\n", __func__);
				BUZZER_ON;
				sleep(1);
				BUZZER_OFF;
				sleep(1);
				break;

			case BUZZER_STATUS_SPACE_FULL:
DDPRINTF("[%s] BUZZER_STATUS_SPACE_FULL\n", __func__);
				BUZZER_ON;
				usleep(500*1000);
				BUZZER_OFF;
				usleep(500*1000);
				break;

			case BUZZER_STATUS_STREAM_FAIL:
DDPRINTF("[%s] BUZZER_STATUS_STREAM_FAIL\n", __func__);
				BUZZER_ON;
				usleep(500*1000);
				BUZZER_OFF;
				usleep(100*1000);
				BUZZER_ON;
				usleep(100*1000);
				BUZZER_OFF;
				usleep(100*1000);
				BUZZER_ON;
				usleep(100*1000);
				BUZZER_OFF;
				BuzzerStatus = BUZZER_STATUS_INIT;
				break;

			case BUZZER_STATUS_OFF:
DDPRINTF("[%s] BUZZER_STATUS_OFF\n", __func__);
				BUZZER_OFF;
				sleep(1);
				break;

			default:
				//BUZZER_OFF;
				sleep(1);
				break;
		}
	}
}



//LED copy from app/led_btn.c

int ThreadLEDControl(void *gpio_pointer)
{
	int fd_gpio = (int)gpio_pointer;
	int cmd;

	while (1)
	{
		switch (LEDStatus)
		{
			case LED_STATUS_INIT:	//Power On �ɡA�Ҧ�LED�O���|�I�G500ms�A�A���ܥثe���A
				cmd = LED_RED; LED_ON(cmd);
				cmd = LED_GREEN; LED_ON(cmd);
				cmd = LED_BLUE; LED_ON(cmd);
				usleep(500*1000);
				cmd = LED_RED; LED_OFF(cmd);
				cmd = LED_GREEN; LED_OFF(cmd);
				cmd = LED_BLUE; LED_OFF(cmd);
				usleep(500*1000);
				LEDStatus = LED_STATUS_STOP_RECORD;
				break;
			case LED_STATUS_LIVE_VIEW:	//���t�ζi�JLive View�ɡA���LED�O�n�ܦ��{�{���ܡC�G�@���A���@���C
				cmd = LED_BLUE; LED_ON(cmd);
				sleep(1);
				cmd = LED_BLUE; LED_OFF(cmd);
				sleep(1);
				break;
			case LED_STATUS_PASSWORD_RESTORE:	 //���t�ζi�J�^�_ Password �ɡA���LED�O�n�ܦ��ֳt�{�{���ܡC �G300ms�A��300ms
				cmd = LED_BLUE; LED_ON(cmd);
				usleep(300*1000);
				cmd = LED_BLUE; LED_OFF(cmd);
				usleep(300*1000);
				break;
			case LED_STATUS_PASSWORD_RESTORE_COMPLETE:
				cmd = LED_BLUE; LED_OFF(cmd);
				sleep(1);
				break;
			case LED_STATUS_START_RECORD:
				cmd = LED_BLUE; LED_ON(cmd);
				if (GPSStatus==LED_STATUS_GPS_READY)
				{
					cmd = LED_RED; LED_OFF(cmd);
					cmd = LED_GREEN; LED_ON(cmd);
				}
				else
				{
					cmd = LED_RED; LED_ON(cmd);
					cmd = LED_GREEN; LED_OFF(cmd);
				}
				sleep(1);
				break;
			case LED_STATUS_STOP_RECORD:
				cmd = LED_BLUE; LED_OFF(cmd);
				if (GPSStatus==LED_STATUS_GPS_READY)
				{
					cmd = LED_RED; LED_OFF(cmd);
					cmd = LED_GREEN; LED_ON(cmd);
				}
				else
				{
					cmd = LED_RED; LED_ON(cmd);
					cmd = LED_GREEN; LED_OFF(cmd);
				}
				sleep(1);
				break;

			default:
				break;
		}
	}
}

int ButtonCounter=0;


int GetPIPKey(int fd)
{
	int counter=0;

	while (PUSH_BUTTON == BUTTON_PRESSED)
	{
		counter++;
		usleep(500000);
#if 0
		if (counter > 4)	// 2 sec
			LEDStatus = LED_STATUS_LIVE_VIEW;
#else
		if (counter > 4)	// 2 sec
		{
			LEDStatus = LED_STATUS_LIVE_VIEW;
			break;
		}
#endif
		if (counter > 14) //ivan note: �ѩ� > 4 �N break �F�A�ҥH�o�� if �ڥ��]����
		{
			LEDStatus = LED_STATUS_PASSWORD_RESTORE;
			break;
		}
	}

	return 0;
}



//�����~.�Ϊ�,�LIndex��.
int CheckIndexTable(CAR_RECORDER_T *CarRecorder, int TableSelect, int *MaxIndexPosition)
{
	int i, MaxSerial=0;
	INDEX_SECTOR_T is;

#if defined(AUTO_SPACE)
	for(i=0; i<DiskDataBlocks[CarRecorder->num]; i++)
#else
	for(i=0; i<DISK_DATA_BLOCKS; i++)
#endif
	{
		off64_t off = CarRecorder->IndexSectorWriteBase[TableSelect]+ i*INDEX_SECTOR_SIZE;

		if (lseek64(CarRecorder->StorageDescriptor, off, SEEK_SET) < 0)
		{
DDPRINTF("[%s] seek position %llx fail\n", __func__, off);
			return 0;
		}
		if (read(CarRecorder->StorageDescriptor, (char *)&is, INDEX_SECTOR_SIZE)<0)
		{
DDPRINTF("[%s] read position %llx fail\n", __func__, off);
			return 0;
		}

		if (is.Tag != ENDIAN_INT(TAG_SECTOR_TABLE))
		{
DDPRINTF("\033[31m[%s]\033[m block %d : Tag=0x%08lX fail\n", __func__, i, is.Tag);
			return 0;
		}

		unsigned long checksum = calc_checksum((unsigned long *)&is, (INDEX_SECTOR_SIZE-4)/4);
		if(checksum != is.Checksum)
		{
DDPRINTF("\033[31m[%s]\033[m block %d : Checksum=0x%08lX is error, must be 0x%08lX\n", __func__, i, is.Checksum, checksum);
			return 0;
		}

		if(ENDIAN_INT(is.IndexTable.IndexTableSerial) > MaxSerial)
		{
			MaxSerial = ENDIAN_INT(is.IndexTable.IndexTableSerial);
			*MaxIndexPosition = i;
		}
		//DDPRINTF("check i=%d ok\n", i);
		VBUS_CHECK();
	}
	return (MaxSerial);
}


int InitIndexTable(CAR_RECORDER_T *CarRecorder, int TableSelect)
{
	int i;
	INDEX_SECTOR_T is;

	off64_t off = CarRecorder->IndexSectorWriteBase[TableSelect];
	errno = 0;
	if (lseek64(CarRecorder->StorageDescriptor, off, SEEK_SET) < 0)
	{
		DDPRINTF("[%s] seek position %llx fail because '%s'\n", __func__, off, strerror(errno));
		return -1;
	}

	memset((char *)&is, 0, INDEX_SECTOR_SIZE);
	is.Tag = ENDIAN_INT(TAG_SECTOR_TABLE);

	DDPRINTF("[%s] initial USB %d IndexTable area %d now...\n", __func__, CarRecorder->num+1, TableSelect);

#if defined(AUTO_SPACE)
	for(i=0; i<DiskDataBlocks[CarRecorder->num]; i++)
#else
	for (i=0; i<DISK_DATA_BLOCKS; i++)
#endif
	{
		is.Checksum = calc_checksum( (unsigned long *)&is, (INDEX_SECTOR_SIZE-4)/4);
		errno = 0;
		if(write(CarRecorder->StorageDescriptor, (char *)&is, INDEX_SECTOR_SIZE) != INDEX_SECTOR_SIZE)
		{
			DDPRINTF("[%s] IndexTable record %d initial fail because '%s'\n", __func__, i, strerror(errno));
			return -1;
		}
		VBUS_CHECK();
	}
	return 0;
}

int CopyIndexTable(CAR_RECORDER_T *CarRecorder, int TableSrc, int TableDst)
{
	int i;
	INDEX_SECTOR_T is;

#if defined(AUTO_SPACE)
	for(i=0; i<DiskDataBlocks[CarRecorder->num]; i++)
#else
	for (i=0; i<DISK_DATA_BLOCKS; i++)
#endif
	{
		off64_t off = CarRecorder->IndexSectorWriteBase[TableSrc]+ i*INDEX_SECTOR_SIZE;
		if(lseek64(CarRecorder->StorageDescriptor, off, SEEK_SET) < 0)
		{
			DDPRINTF("[%s] seek position %llx fail\n", __func__, off);
			return (0);
		}
		if(read(CarRecorder->StorageDescriptor, (char *)&is, INDEX_SECTOR_SIZE) < 0)
			return 0;

		off = CarRecorder->IndexSectorWriteBase[TableDst]+ i*INDEX_SECTOR_SIZE;
		if(lseek64(CarRecorder->StorageDescriptor, off,SEEK_SET)<0)
		{
			DDPRINTF("[%s] seek position %llx fail\n", __func__, off);
			return (0);
		}
		if(write(CarRecorder->StorageDescriptor, (char *)&is, INDEX_SECTOR_SIZE) < 0)
			return 0;
		VBUS_CHECK();
	}
	return (1);
}


//Serial=0. ���ܭn�״_�Ĥ@��DATA_BLOCK. �åBserialIndex��ӭn�q1�}�l.�o�쪺.�P�ɦ�start/stop flag
int FixFirstDataBlockAndIndex(CAR_RECORDER_T *CarRecorder)
{
	int index;
	DATA_TABLE_T DataTable;
	int FailIndex= -1;
	off64_t off;

	unsigned int DataBlockSerial=0, DataBlockEndSerial=0;
	unsigned char DataBlockTimeStamp[6], DataBlockEndTimeStamp[6];
	unsigned char FlagMask = 0;
#ifdef EVENT_FIX
	EVENT_INFO_T	*Info;
	int	MyEventCounter = 0;
#endif

	//�ײĤ@��DATA Block
	//�����o.DataBlockSerial.DataBlockTimeStamp[6].DataBlockEndSerial.DataBlockEndTimeStamp[6]
	//FailIndex
	for(index=0; index<(DATA_MACRO_BLOCK/DATA_BLOCK_SIZE); index++)
	{
		off = CarRecorder->DataBlockWriteBase +(index+1)*DATA_BLOCK_SIZE -DATA_TABLE_SIZE;
		if(lseek64(CarRecorder->StorageDescriptor, off, SEEK_SET) < 0)
		{
			DDPRINTF("  [%s] seek position %llx fail\n", __func__, off);
			return (0);
		}
		if(read(CarRecorder->StorageDescriptor, &DataTable, DATA_TABLE_SIZE) < 0)
		{
			DDPRINTF("  [%s] read position %llx fail\n", __func__, off);
			return (0);
		}

		if(DataTable.Tag != ENDIAN_INT(TAG_DATA_TABLE))
		{
			FailIndex = index;
			DDPRINTF("  [%s] Tag of SubDataBlock %d not correct\n", __func__, index);
			break;
		}
		if(DataTable.DataBlockSerial != ENDIAN_INT(index+1))
		{
			FailIndex = index;
			DDPRINTF("  [%s] DataBlockSerial of SubDataBlock %d not correct\n", __func__, index);
			break;
		}

		if(DataTable.CheckSum != calc_checksum((unsigned long *)&DataTable, (DATA_TABLE_SIZE-4)/4))
		{
			FailIndex = index;
			DDPRINTF("  [%s] CheckSum of SubDataBlock %d not correct\n", __func__, index);
			break;
		}
		if(index==0)
		{
			DataBlockSerial = DataTable.DataBlockSerial;
			memcpy(DataBlockTimeStamp, DataTable.DataBlockTimeStamp, 6);
		}
		DataBlockEndSerial = DataTable.DataBlockSerial;
		memcpy(DataBlockEndTimeStamp, DataTable.DataBlockTimeStamp, 6);

#ifdef EVENT_FIX
			EventInfoPtr = &EventInfo[0][0];
			Info = EventInfoPtr +MyEventCounter; //event counter increased and looping every 16*8
			memset((char *)Info, 0, sizeof(EVENT_INFO_T));
			EventPtr = &EventMask[0][0];

			if ( (DataTable.FlagMask==EVENT_TYPE_GSENSOR_TRIGGER) ||
				 (DataTable.FlagMask==EVENT_TYPE_OVER_SPEED) ||
				 (DataTable.FlagMask==EVENT_TYPE_HAND_TRIGGER) )
			{
				DDPRINTF("  [%s] SubDataBlock %d found event %d\n", __func__, index, DataTable.FlagMask);
				Info->EventType = ENDIAN_SHORT(DataTable.FlagMask);
				Info->DataBlockSerial = DataTable.DataBlockSerial;
				memcpy(Info->EventTime, DataTable.DataBlockTimeStamp,6);
				*(EventPtr+MyEventCounter) = DataTable.FlagMask;
			}

			MyEventCounter++;
			//FlagMask . �u���Ĥ@��.
			if (FlagMask==0)
				if ( (DataTable.FlagMask==EVENT_TYPE_GSENSOR_TRIGGER)||
					 (DataTable.FlagMask==EVENT_TYPE_OVER_SPEED) ||
					 (DataTable.FlagMask==EVENT_TYPE_HAND_TRIGGER) )
				{
					FlagMask = DataTable.FlagMask;
					//printf("must fix index event\n");
				}
#endif

		VBUS_CHECK();
	}

	DDPRINTF("  [%s] FailIndex = %d\n", __func__, FailIndex);
	//�p�GFailIndex=-1, ���ܾ���DATA Block���O�n��
	//FailIndex�аO. �b�ĴX��512Byte��Index�X��.
	VBUS_CHECK();

	//here is first sub_data_block of first DataBlock
	if (FailIndex == 0)//no need to fix index table,only init CarRecorder
	{
		CarRecorder->DataBlockWriteBase = OFFSET_DATA_BLOCK;
		CarRecorder->IndexSectorWriteBase[0] = OFFSET_MARK_TABLE_0;
		CarRecorder->IndexSectorWriteBase[1] = OFFSET_MARK_TABLE_1;

		//CarRecorder->DataBlockWriteOffset = 0;
		CarRecorder->xx = 0;
		CarRecorder->IndexSectorWriteOffset = 0;

		CarRecorder->DataBlockSerial = 0;	//@@ so, this is the last serial being written
		CarRecorder->IndexSector.IndexTable.IndexTableSerial = 0;
		CarRecorder->DataBlock = 0;
		CarRecorder->CurrentChannel = CHANNEL_IN;		//@@
		CarRecorder->FlagRecordStart[0] = CarRecorder->FlagRecordStart[1] = 1;
		CarRecorder->PingPong = 0;
	}
	else
	{
		//�״_Index,�A��CarRecorder.
		INDEX_SECTOR_T is;
		memset(&is, 0, sizeof(INDEX_SECTOR_T));
		is.Tag = ENDIAN_INT(TAG_SECTOR_TABLE);
		CarRecorder->itSerial = 1; //ivan+
		is.IndexTable.IndexTableSerial = ENDIAN_INT(CarRecorder->itSerial);
		memset(is.IndexTable.EventInfo, 0, sizeof(EVENT_INFO_T)*MAX_EVENT_EACH_16MB_DATA);
		is.IndexTable.DataBlockSerial = DataBlockSerial;
		memcpy(is.IndexTable.DataBlockTimeStamp, DataBlockTimeStamp, 6);
		is.IndexTable.FlagRecordStart = 1;
		is.IndexTable.FlagRecordEnd = 1;
		is.IndexTable.FlagMask = 0;
		is.IndexTable.EventInfoCounter = 0;
		memset(is.IndexTable.Reserved2,0,2);
		is.IndexTable.DataBlockEndSerial = DataBlockEndSerial;
		memcpy(is.IndexTable.DataBlockEndTimeStamp, DataBlockEndTimeStamp, 6);
		is.GSensorSensitivityVer = ENDIAN_SHORT(1); //ivan+
		is.GSensorSensitivityAlgorithm = ENDIAN_SHORT(1); //ivan+
		memset(is.Reserved, 0, INDEX_REVERSED_SIZE);
		is.Checksum = calc_checksum((unsigned long *)&is, (DATA_TABLE_SIZE-4)/4);;
		//
		CarRecorder->DataBlockWriteBase = OFFSET_DATA_BLOCK;
		CarRecorder->IndexSectorWriteBase[0] = OFFSET_MARK_TABLE_0;
		CarRecorder->IndexSectorWriteBase[1] = OFFSET_MARK_TABLE_1;

		//CarRecorder->DataBlockWriteOffset = DATA_MACRO_BLOCK;
		CarRecorder->IndexSectorWriteOffset = sizeof(INDEX_SECTOR_T);

		CarRecorder->DataBlockSerial = 1;	//@@ so, this is the last serial being written
		CarRecorder->itSerial = 1; //ivan+
		is.IndexTable.IndexTableSerial = ENDIAN_INT(CarRecorder->itSerial);
		CarRecorder->DataBlock = 0;
		CarRecorder->CurrentChannel = CHANNEL_IN;		//@@
		CarRecorder->FlagRecordStart[0] = CarRecorder->FlagRecordStart[1] = 1;
		CarRecorder->PingPong = 0;

		is.IndexTable.FlagMask = FlagMask;

		off = CarRecorder->IndexSectorWriteBase[0];
		if (lseek64(CarRecorder->StorageDescriptor, off, SEEK_SET)<0)
		{
			DDPRINTF("  [%s] seek position %llx fail\n", __func__, off);
			return (0);
		}
		if (write(CarRecorder->StorageDescriptor, (char *)&is, INDEX_SECTOR_SIZE) < 0)
		{
			DDPRINTF("  [%s] write position %llx fail\n", __func__, off);
			return 0;
		}

		if (FailIndex == -1)
			CarRecorder->itSerial = 8; //ivan+
		else
			CarRecorder->itSerial = FailIndex; //ivan+
		CarRecorder->IndexSector.IndexTable.IndexTableSerial = ENDIAN_INT(CarRecorder->itSerial);

		CarRecorder->PingPong	= 1;

		VBUS_CHECK();
	}

	return 1;
}




int CheckIndexTableStop(CAR_RECORDER_T *CarRecorder,int TableSelect,int MaxIndexPosition)
{
	INDEX_SECTOR_T is;

	off64_t off = CarRecorder->IndexSectorWriteBase[TableSelect]+MaxIndexPosition*INDEX_SECTOR_SIZE;
	if (lseek64(CarRecorder->StorageDescriptor, off, SEEK_SET) < 0)
	{
		DDPRINTF("[%s] seek %llx fail\n", __func__, off);
		return (0);
	}
	if (read(CarRecorder->StorageDescriptor, (char *)&is, INDEX_SECTOR_SIZE) < 0)
	{
		DDPRINTF("[%s] read %llx fail\n", __func__, off);
		return (0);
	}

	if (is.IndexTable.FlagRecordEnd)
		return 1;
	else
		return 0;

}


int FixIndexStop0(CAR_RECORDER_T *CarRecorder, int TableSelect, int MaxIndexPosition)
{
	int i, index;
	INDEX_SECTOR_T IndexSector;
	DATA_TABLE_T DataTable;
	unsigned int checksum;
	unsigned int *ptrInt;
	int FailIndex= -1;

	unsigned int DataBlockSerial=0;
	unsigned char DataBlockTimeStamp[6];
	unsigned int DataBlockEndSerial=0;
	unsigned char DataBlockEndTimeStamp[6];
	long long MaxIndexPositionLong=0;
	unsigned int MaxIndexDataBlockSerial=0;
	unsigned char FlagMask=0;
#ifdef EVENT_FIX
	EVENT_INFO_T *Info;
	int	MyEventCounter = 0;
#endif

//��Ū�X��.�H�ѭ״_.
	INDEX_SECTOR_T is2;

	off64_t off = CarRecorder->IndexSectorWriteBase[TableSelect]+MaxIndexPosition*INDEX_SECTOR_SIZE;
	if (lseek64(CarRecorder->StorageDescriptor, off, SEEK_SET) < 0)
	{
		DDPRINTF("[%s] seek %llx fail\n", __func__, off);
		return (-1);
	}
	if (read(CarRecorder->StorageDescriptor, (char *)&is2, INDEX_SECTOR_SIZE) < 0)
	{
		DDPRINTF("[%s] read %llx fail\n", __func__, off);
		return (-1);
	}

	MaxIndexDataBlockSerial = ENDIAN_INT(is2.IndexTable.DataBlockSerial);
	DDPRINTF("[%s] MaxIndexDataBlockSerial= %x\n", __func__, MaxIndexDataBlockSerial);

	IndexSector.Tag = ENDIAN_INT(TAG_SECTOR_TABLE);
	MaxIndexPositionLong = MaxIndexPosition;
	MaxIndexPositionLong <<= 24;
	DDPRINTF("[%s] max=%x,%llx\n", __func__, MaxIndexPosition, MaxIndexPositionLong);
	VBUS_CHECK();


	//�J�M�ثe�Sstop.���N��2�إi��. �@�جO���n�u���ۤv��DATA_BLOCK(16MB)���n���F.index�]�g�F.
	//�t�@�جO. �U�@��DATA_BLOCK�����@�b. �o�����n��serial���s���ʨӧP�_.
	//�ҥH�ˬd���ODataBlock.
	//1. �@�}�l��DataBlockSerial�nindex/data�۲�
	//2. �A�ӭn�dDataBlockSerial�s���������_��.
	//3. checksum���d��
	//4. �ҥH�p�G�_�b�ۤv�o��. �A�ۤv�o���[stop.
	//5. �p�G�_�b�U�@��DATA_BLOCK. ���N�[�@��INDEX.


	for (index=0; index<(2*(DATA_MACRO_BLOCK/DATA_BLOCK_SIZE)); index++)
	{
		off = CarRecorder->DataBlockWriteBase+MaxIndexPositionLong+(index+1)*DATA_BLOCK_SIZE-DATA_TABLE_SIZE;
		if (lseek64(CarRecorder->StorageDescriptor, off, SEEK_SET) < 0)
		{
			DDPRINTF("[%s].2 seek %llx fail\n", __func__, off);
			return (-1);
		}
		if (read(CarRecorder->StorageDescriptor, &DataTable, DATA_TABLE_SIZE) < 0)
		{
			DDPRINTF("[%s].2 read %llx fail\n", __func__, off);
			return (-1);
		}

		if (DataTable.Tag != ENDIAN_INT(TAG_DATA_TABLE))
		{
			FailIndex = index;
			DDPRINTF("[%s] Tag fail\n", __func__);
			break;
		}

		ptrInt = (unsigned int *)&DataTable;
		checksum = 0;
		for (i=0; i<(DATA_TABLE_SIZE-4)/4; i++)
		{
			checksum ^= *ptrInt;
			ptrInt++;
		}
		if (DataTable.CheckSum != checksum)
		{
			FailIndex = index;
			DDPRINTF("[%s] CheckSum fail\n", __func__);
			break;
		}
		//DDPRINTF("[%s] DataTable.DataBlockSerial=%x\n", __func__, DataTable.DataBlockSerial);

		if (DataTable.DataBlockSerial != ENDIAN_INT(MaxIndexDataBlockSerial+index))
		{
			FailIndex = index;
			DDPRINTF("[%s] DataBlockSerial fail\n", __func__);
			break;
		}

		if (index==(DATA_MACRO_BLOCK/DATA_BLOCK_SIZE))
		{
			DataBlockSerial = ENDIAN_INT(DataTable.DataBlockSerial);
			memcpy(DataBlockTimeStamp, DataTable.DataBlockTimeStamp, 6);
		}
		DataBlockEndSerial = ENDIAN_INT(DataTable.DataBlockSerial);
		memcpy(DataBlockEndTimeStamp, DataTable.DataBlockTimeStamp, 6);

		if (index >= (DATA_MACRO_BLOCK/DATA_BLOCK_SIZE))
		{
#ifdef EVENT_FIX
			EventInfoPtr = &EventInfo[0][0];
			Info = EventInfoPtr +MyEventCounter; //event counter increased and looping every 16*8
			Info->DataBlockSerial = 0;
			for(i=0; i<6; i++)
				Info->EventTime[i] = 0;
			Info->EventType = 0;
			EventPtr = &EventMask[0][0];

			if ( (DataTable.FlagMask==EVENT_TYPE_GSENSOR_TRIGGER) || (DataTable.FlagMask==EVENT_TYPE_OVER_SPEED) )
			{
				Info->EventType = ENDIAN_SHORT(DataTable.FlagMask);
				Info->DataBlockSerial = DataTable.DataBlockSerial;		//!!!attention
				memcpy(Info->EventTime, DataTable.DataBlockTimeStamp, 6);
				*(EventPtr +MyEventCounter) = DataTable.FlagMask;
			}

			MyEventCounter++;
			//FlagMask . �u���Ĥ@��.
			if (FlagMask == 0)
				if ( (DataTable.FlagMask==EVENT_TYPE_GSENSOR_TRIGGER) || (DataTable.FlagMask==EVENT_TYPE_OVER_SPEED) )
				{
					FlagMask = DataTable.FlagMask;
					printf("must fix index event\n");
				}
#endif
		}
		VBUS_CHECK();
	}
	DDPRINTF("[%s] FailIndex=%d, DataBlockSerial=%08X, DataBlockEndSerial=%08X\n", __func__, FailIndex, DataBlockSerial, DataBlockEndSerial);

	//�p�GFailIndex=-1, ���ܾ���DATA Block���O�n��
	//FailIndex�аO. �b�ĴX��512 bytes �� Index �X��.
	if (FailIndex == 0)
	{
		DDPRINTF("[%s] Fatal error!! index 0 fail.\n", __func__);
		return (-2);
	}
	//���~�b�Ĥ@�� block �̡A�u�n�[�W. stop . end time.
	else if ( (FailIndex<=(DATA_MACRO_BLOCK/DATA_BLOCK_SIZE)) && (FailIndex>0)) 
	{
		DDPRINTF("[%s] index %d is fixing...\n", __func__, FailIndex);
		//�״_Index,�A��CarRecorder.
		is2.IndexTable.FlagRecordEnd = 1;
		is2.IndexTable.DataBlockEndSerial = ENDIAN_INT(DataBlockEndSerial);
		memcpy(is2.IndexTable.DataBlockEndTimeStamp, DataBlockEndTimeStamp, 6);
		is2.Checksum = calc_checksum((unsigned long *)&is2,(DATA_TABLE_SIZE-4)/4);

		CarRecorder->DataBlockWriteBase = OFFSET_DATA_BLOCK;
		CarRecorder->IndexSectorWriteBase[0] = OFFSET_MARK_TABLE_0;
		CarRecorder->IndexSectorWriteBase[1] = OFFSET_MARK_TABLE_1;

		//CarRecorder->DataBlockWriteOffset = MaxIndexPositionLong+DATA_MACRO_BLOCK;	//@@�U�Ӷ}�l�g���a��
#if defined(AUTO_SPACE)
		//if (CarRecorder->DataBlockWriteOffset >= LenDataBlock[CarRecorder->num])
		//	CarRecorder->DataBlockWriteOffset -= LenDataBlock[CarRecorder->num];
		CarRecorder->IndexSectorWriteOffset = MaxIndexPosition*sizeof(INDEX_SECTOR_T);
		if(CarRecorder->IndexSectorWriteOffset >= (DiskDataBlocks[CarRecorder->num]*INDEX_SECTOR_SIZE))
			CarRecorder->IndexSectorWriteOffset -= (DiskDataBlocks[CarRecorder->num]*INDEX_SECTOR_SIZE);
#else
		if (CarRecorder->DataBlockWriteOffset >= LEN_DATA_BLOCK)
			CarRecorder->DataBlockWriteOffset -= LEN_DATA_BLOCK;
		CarRecorder->IndexSectorWriteOffset = MaxIndexPosition*sizeof(INDEX_SECTOR_T);
		if(CarRecorder->IndexSectorWriteOffset >= (DISK_DATA_BLOCKS*INDEX_SECTOR_SIZE))
			CarRecorder->IndexSectorWriteOffset -= (DISK_DATA_BLOCKS*INDEX_SECTOR_SIZE);
#endif
		CarRecorder->DataBlockSerial = DataBlockEndSerial;	//@@ so, this is the last serial being written
		//CarRecorder->IndexSector.IndexTable.IndexTableSerial = is2.IndexTable.IndexTableSerial;
		//CarRecorder->itSerial++; //ivan+
		CarRecorder->DataBlock = 0;
		CarRecorder->CurrentChannel = CHANNEL_IN;		//@@
		CarRecorder->FlagRecordStart[0] = CarRecorder->FlagRecordStart[1] = 1;
		CarRecorder->PingPong = 0;

		off = CarRecorder->IndexSectorWriteBase[0]+CarRecorder->IndexSectorWriteOffset;
		if (lseek64(CarRecorder->StorageDescriptor, off, SEEK_SET) < 0)
		{
			DDPRINTF("[%s].3 seek %llx fail\n", __func__, off);
			return (-1);
		}
		if (write(CarRecorder->StorageDescriptor, (char *)&is2, INDEX_SECTOR_SIZE)<0)
		{
			DDPRINTF("[%s].3 write %llx fail\n", __func__, off);
			return (-1);
		}
		DDPRINTF("\033[1;31m[%s]\033[m IndexTableSerial=0x%08X, DataBlockSerial=0x%08X\n",
			__func__, ENDIAN_INT(is2.IndexTable.IndexTableSerial),
			ENDIAN_INT(is2.IndexTable.DataBlockSerial) );

		off = CarRecorder->IndexSectorWriteBase[1]+CarRecorder->IndexSectorWriteOffset;
		if (lseek64(CarRecorder->StorageDescriptor, off, SEEK_SET) < 0)
		{
			DDPRINTF("[%s].4 seek %llx fail\n", __func__, off);
			return (-1);
		}
		if (write(CarRecorder->StorageDescriptor, (char *)&is2, INDEX_SECTOR_SIZE)<0)
		{
			DDPRINTF("[%s].4 write %llx fail\n", __func__, off);
			return (-1);
		}
		CarRecorder->IndexSectorWriteOffset += sizeof(INDEX_SECTOR_T);

#if defined(AUTO_SPACE)
		if (CarRecorder->IndexSectorWriteOffset >= (DiskDataBlocks[CarRecorder->num]*INDEX_SECTOR_SIZE))
			CarRecorder->IndexSectorWriteOffset -= (DiskDataBlocks[CarRecorder->num]*INDEX_SECTOR_SIZE);
#else
		if (CarRecorder->IndexSectorWriteOffset >= (DISK_DATA_BLOCKS*INDEX_SECTOR_SIZE))
			CarRecorder->IndexSectorWriteOffset -= (DISK_DATA_BLOCKS*INDEX_SECTOR_SIZE);
#endif
	}
	//�u�n�[�W. stop . end time. �åB�[���U�@��index
	else
	{
		DDPRINTF("[%s] next index fixing...\n", __func__);
		//�״_Index,�A��CarRecorder.

		memset((char *)&is2, 0, sizeof(INDEX_SECTOR_T));
		is2.Tag = ENDIAN_INT(TAG_SECTOR_TABLE);
		//is2.IndexTable.IndexTableSerial = ENDIAN_INT(ENDIAN_INT(is2.IndexTable.IndexTableSerial)+1);
		CarRecorder->itSerial++; //ivan+
		is2.IndexTable.IndexTableSerial = ENDIAN_INT(CarRecorder->itSerial);
		CarRecorder->IndexSectorBuffer.IndexTable.IndexTableSerial = is2.IndexTable.IndexTableSerial;
		memset((char *)is2.IndexTable.EventInfo, 0, sizeof(EVENT_INFO_T)*MAX_EVENT_EACH_16MB_DATA);
		memcpy(is2.IndexTable.DataBlockTimeStamp, DataBlockTimeStamp, 6);
		is2.IndexTable.DataBlockSerial = ENDIAN_INT(DataBlockSerial);

		is2.IndexTable.FlagRecordStart = 0;	//since this is the NEXT index
		is2.IndexTable.FlagRecordEnd = 1;
		is2.IndexTable.FlagMask = FlagMask;
		is2.IndexTable.EventInfoCounter = 0;
		is2.IndexTable.Reserved2[0] = is2.IndexTable.Reserved2[1] = 0;
		is2.IndexTable.DataBlockEndSerial = ENDIAN_INT(DataBlockEndSerial);
		memcpy(is2.IndexTable.DataBlockEndTimeStamp, DataBlockEndTimeStamp, 6);
		is2.GSensorSensitivityVer = ENDIAN_SHORT(1); //ivan+
		is2.GSensorSensitivityAlgorithm = ENDIAN_SHORT(1); //ivan+
		memset(is2.Reserved, 0, INDEX_REVERSED_SIZE); //ivan+

		int a,b;
		for (a=0; a<16; a++)
			for (b=0; b<8; b++)
			{
				if ( (EventMask[a][b]==EVENT_TYPE_GSENSOR_TRIGGER) || (EventMask[a][b]==EVENT_TYPE_OVER_SPEED) )
				{
					is2.IndexTable.EventInfo[a].DataBlockSerial = EventInfo[a][b].DataBlockSerial;
					is2.IndexTable.EventInfo[a].EventType = EventInfo[a][b].EventType;
					memcpy(is2.IndexTable.EventInfo[a].EventTime, EventInfo[a][b].EventTime, 6);
					is2.IndexTable.EventInfoCounter++;
					//should modify here!!
					break;	//���Ĥ@��event
				}
			}

		is2.Checksum = calc_checksum( (unsigned long *)&is2,(DATA_TABLE_SIZE-4)/4);

		CarRecorder->DataBlockWriteBase = OFFSET_DATA_BLOCK;
		CarRecorder->IndexSectorWriteBase[0] = OFFSET_MARK_TABLE_0;
		CarRecorder->IndexSectorWriteBase[1] = OFFSET_MARK_TABLE_1;

		//CarRecorder->DataBlockWriteOffset = MaxIndexPositionLong +DATA_MACRO_BLOCK+DATA_MACRO_BLOCK;	//@@
#if defined(AUTO_SPACE)
		//if (CarRecorder->DataBlockWriteOffset >= LenDataBlock[CarRecorder->num])
		//	CarRecorder->DataBlockWriteOffset -= LenDataBlock[CarRecorder->num];
		CarRecorder->IndexSectorWriteOffset 	= (MaxIndexPosition+1)*sizeof(INDEX_SECTOR_T);
		if (CarRecorder->IndexSectorWriteOffset >= (DiskDataBlocks[CarRecorder->num]*INDEX_SECTOR_SIZE))
			CarRecorder->IndexSectorWriteOffset -=(DiskDataBlocks[CarRecorder->num]*INDEX_SECTOR_SIZE);
#else
		if (CarRecorder->DataBlockWriteOffset >= LEN_DATA_BLOCK)
			CarRecorder->DataBlockWriteOffset -= LEN_DATA_BLOCK;
		CarRecorder->IndexSectorWriteOffset 	= (MaxIndexPosition+1)*sizeof(INDEX_SECTOR_T);	//@@
		if (CarRecorder->IndexSectorWriteOffset >= (DISK_DATA_BLOCKS*INDEX_SECTOR_SIZE))
			CarRecorder->IndexSectorWriteOffset -=(DISK_DATA_BLOCKS*INDEX_SECTOR_SIZE);
#endif
		CarRecorder->DataBlockSerial = DataBlockEndSerial;	//@@ so, this is the last serial being written
		CarRecorder->IndexSector.IndexTable.IndexTableSerial = is2.IndexTable.IndexTableSerial;
		CarRecorder->DataBlock = 0;
		CarRecorder->CurrentChannel = CHANNEL_IN;		//@@
		CarRecorder->FlagRecordStart[0] = CarRecorder->FlagRecordStart[1] = 1;
		CarRecorder->PingPong = 0;

		off = CarRecorder->IndexSectorWriteBase[0]+CarRecorder->IndexSectorWriteOffset;
		if (lseek64(CarRecorder->StorageDescriptor, off, SEEK_SET) < 0)
		{
			DDPRINTF("[%s].5 seek %llx fail\n", __func__, off);
			return (-1);
		}
		if (write(CarRecorder->StorageDescriptor, (char *)&is2, INDEX_SECTOR_SIZE)<0)
		{
			DDPRINTF("[%s].5 write %llx fail\n", __func__, off);
			return (-1);
		}

		DDPRINTF("\033[1;31m[%s].5\033[m IndexTableSerial=0x%08X, DataBlockSerial=0x%08X\n",
			__func__, ENDIAN_INT(is2.IndexTable.IndexTableSerial),
			ENDIAN_INT(is2.IndexTable.DataBlockSerial) );


		if (lseek64(CarRecorder->StorageDescriptor, CarRecorder->IndexSectorWriteBase[1]+CarRecorder->IndexSectorWriteOffset, SEEK_SET)<0)
		{
			DDPRINTF("[%s].6 seek %llx fail\n", __func__, off);
			return (-1);
		}
		if (write(CarRecorder->StorageDescriptor, (char *)&is2, INDEX_SECTOR_SIZE)<0)
		{
			DDPRINTF("[%s].6 write %llx fail\n", __func__, off);
			return (-1);
		}
		CarRecorder->IndexSectorWriteOffset += sizeof(INDEX_SECTOR_T);
#if defined(AUTO_SPACE)
		if (CarRecorder->IndexSectorWriteOffset >= (DiskDataBlocks[CarRecorder->num]*INDEX_SECTOR_SIZE))
			CarRecorder->IndexSectorWriteOffset -=(DiskDataBlocks[CarRecorder->num]*INDEX_SECTOR_SIZE);
#else
		if (CarRecorder->IndexSectorWriteOffset >= (DISK_DATA_BLOCKS*INDEX_SECTOR_SIZE))
			CarRecorder->IndexSectorWriteOffset -=(DISK_DATA_BLOCKS*INDEX_SECTOR_SIZE);
#endif
	}

	return 0;
}



int FixIndexStop1(CAR_RECORDER_T *CarRecorder,int TableSelect,int MaxIndexPosition)
{
	int i, index;
	INDEX_SECTOR_T IndexSector;
	DATA_TABLE_T DataTable;
	unsigned int checksum;
	unsigned int *ptrInt;
	int FailIndex= -1;

	//unsigned int DataBlockSerial;
	unsigned char DataBlockTimeStamp[6];
	unsigned int DataBlockEndSerial=0;
	unsigned char DataBlockEndTimeStamp[6];
	long long MaxIndexPositionLong;
	unsigned int MaxIndexDataBlockSerial;
	unsigned char FlagMask=0;
#ifdef EVENT_FIX
	EVENT_INFO_T *Info;
	int	MyEventCounter = 0;
#endif

//��Ū�X��.�H�ѭ״_.
	//char TableContext2[INDEX_SECTOR_SIZE];
	//INDEX_SECTOR_T *IndexSector2;
	INDEX_SECTOR_T is2;

	off64_t off = CarRecorder->IndexSectorWriteBase[TableSelect]+MaxIndexPosition*INDEX_SECTOR_SIZE;
	if (lseek64(CarRecorder->StorageDescriptor, off, SEEK_SET) < 0)
	{
		DDPRINTF("[%s] seek %llx fail\n", __func__, off);
		return (-1);
	}
	if (read(CarRecorder->StorageDescriptor, (char *)&is2, INDEX_SECTOR_SIZE) < 0)
	{
		DDPRINTF("[%s] read %llx fail\n", __func__, off);
		return (-1);
	}

	//IndexSector2 = (INDEX_SECTOR_T *)TableContext2;
	MaxIndexDataBlockSerial = ENDIAN_INT(is2.IndexTable.DataBlockSerial);
	DDPRINTF("[%s] MaxIndexDataBlockSerial= %x\n", __func__, MaxIndexDataBlockSerial);

	IndexSector.Tag = ENDIAN_INT(TAG_SECTOR_TABLE);	//?????????
	MaxIndexPositionLong = MaxIndexPosition;
	MaxIndexPositionLong <<= 24;
	DDPRINTF("[%s] max=%x,%llx\n", __func__, MaxIndexPosition, MaxIndexPositionLong);
	VBUS_CHECK();

	for (index=0; index<(DATA_MACRO_BLOCK/DATA_BLOCK_SIZE); index++)  //diff 1
	{
		off = CarRecorder->DataBlockWriteBase+MaxIndexPositionLong+(index+1)*DATA_BLOCK_SIZE-DATA_TABLE_SIZE;
		if (lseek64(CarRecorder->StorageDescriptor, off, SEEK_SET) < 0)
		{
			DDPRINTF("[%s].2 seek %llx fail\n", __func__, off);
			return (-1);
		}
		if (read(CarRecorder->StorageDescriptor, &DataTable, DATA_TABLE_SIZE) < 0)
		{
			DDPRINTF("[%s].2 read %llx fail\n", __func__, off);
			return (-1);
		}

		if (DataTable.Tag != ENDIAN_INT(TAG_DATA_TABLE))
		{
			FailIndex = index;
			DDPRINTF("[%s] Tag fail\n", __func__);
			break;
		}

		ptrInt = (unsigned int *)&DataTable;
		checksum = 0;
		for (i=0; i<(DATA_TABLE_SIZE-4)/4; i++)
		{
			checksum ^= *ptrInt;
			ptrInt++;
		}
		if (DataTable.CheckSum != checksum)
		{
			FailIndex = index;
			DDPRINTF("[%s] CheckSum fail\n", __func__);
			break;
		}
		//DDPRINTF("[%s] DataTable.DataBlockSerial=%x\n", __func__, DataTable.DataBlockSerial);

		if (DataTable.DataBlockSerial != ENDIAN_INT(MaxIndexDataBlockSerial+index))
		{
			FailIndex = index;
			DDPRINTF("[%s] DataBlockSerial fail\n", __func__);
			break;
		}

/*
		if (index==(DATA_MACRO_BLOCK/DATA_BLOCK_SIZE))
		{
			DataBlockSerial = ENDIAN_INT(DataTable.DataBlockSerial);
			memcpy(DataBlockTimeStamp,DataTable.DataBlockTimeStamp,6);
			printf("DATA_BLOCK_SERIAL=%x\n",DataBlockSerial);
		}
*/
		DataBlockEndSerial = ENDIAN_INT(DataTable.DataBlockSerial);
		memcpy(DataBlockEndTimeStamp,DataTable.DataBlockTimeStamp,6);

		VBUS_CHECK();
	}
	//DDPRINTF("[%s] FailIndex=%d, DataBlockSerial=%x, DataBlockEndSerial=%x\n", __func__, FailIndex, DataBlockSerial, DataBlockEndSerial);
	DDPRINTF("[%s] FailIndex=%d, DataBlockEndSerial=%x\n", __func__, FailIndex, DataBlockEndSerial);

	//check 2---------------------------------------------------------------------
	FailIndex = -1;	//prepare for next checking
	MaxIndexDataBlockSerial = DataBlockEndSerial +1;

	for (index=0; index<(DATA_MACRO_BLOCK/DATA_BLOCK_SIZE); index++)
	{
		off = CarRecorder->DataBlockWriteBase +MaxIndexPositionLong +DATA_MACRO_BLOCK +(index+1)*DATA_BLOCK_SIZE-DATA_TABLE_SIZE;
		if (lseek64(CarRecorder->StorageDescriptor, off, SEEK_SET) < 0)
		{
			DDPRINTF("[%s].3 seek %llx fail\n", __func__, off);
			return (-1);
		}
		if (read(CarRecorder->StorageDescriptor, &DataTable, DATA_TABLE_SIZE) < 0)
		{
			DDPRINTF("[%s].3 read %llx fail\n", __func__, off);
			return (-1);
		}

		if (DataTable.Tag != ENDIAN_INT(TAG_DATA_TABLE))
		{
			FailIndex = index;
			DDPRINTF("[%s] Tag fail\n", __func__);
			break;
		}

		ptrInt = (unsigned int *)&DataTable;
		checksum = 0;
		for (i=0; i<(DATA_TABLE_SIZE-4)/4; i++)
		{
			checksum ^= *ptrInt;
			ptrInt++;
		}
		if (DataTable.CheckSum != checksum)
		{
			FailIndex = index;
			DDPRINTF("[%s] CheckSum fail\n", __func__);
			break;
		}
		DDPRINTF("[%s] DataTable.DataBlockSerial=%x\n", __func__, DataTable.DataBlockSerial);

		if (DataTable.DataBlockSerial != ENDIAN_INT(MaxIndexDataBlockSerial+index))
		{
			FailIndex = index;
			DDPRINTF("[%s].2 DataBlockSerial fail\n", __func__);
			break;
		}

		if (index==0)
		{
			//DataBlockSerial = ENDIAN_INT(DataTable.DataBlockSerial);
			memcpy(DataBlockTimeStamp, DataTable.DataBlockTimeStamp, 6);
		}
		DataBlockEndSerial = ENDIAN_INT(DataTable.DataBlockSerial);
		memcpy(DataBlockEndTimeStamp,DataTable.DataBlockTimeStamp,6);

#ifdef EVENT_FIX
		EventInfoPtr = &EventInfo[0][0];
		Info = EventInfoPtr+MyEventCounter; //event counter increased and looping every 16*8
		Info->DataBlockSerial = 0;
		for(i=0; i<6; i++)
			Info->EventTime[i] = 0;
		Info->EventType = 0;
		EventPtr = &EventMask[0][0];

		if ((DataTable.FlagMask==EVENT_TYPE_GSENSOR_TRIGGER)||(DataTable.FlagMask==EVENT_TYPE_OVER_SPEED))
		{
			Info->EventType = ENDIAN_SHORT(DataTable.FlagMask);
			Info->DataBlockSerial = DataTable.DataBlockSerial;		//!!!attention
			memcpy(Info->EventTime, DataTable.DataBlockTimeStamp, 6);
			*(EventPtr+MyEventCounter) = DataTable.FlagMask;
		}

		MyEventCounter++;
		//FlagMask . �u���Ĥ@��.
		if (FlagMask==0)
			if ( (DataTable.FlagMask==EVENT_TYPE_GSENSOR_TRIGGER) || (DataTable.FlagMask==EVENT_TYPE_OVER_SPEED) )
			{
				FlagMask = DataTable.FlagMask;
				printf("must fix index event\n");
			}

#endif
		VBUS_CHECK();
	}
	//DDPRINTF("[%s] FailIndex=%d, DataBlockSerial=%x, DataBlockEndSerial=%x\n", __func__, FailIndex, DataBlockSerial, DataBlockEndSerial);
	DDPRINTF("[%s] FailIndex=%d, DataBlockEndSerial=%x\n", __func__, FailIndex, DataBlockEndSerial);

	//�p�GFailIndex=-1, ���ܾ���DATA Block���O�n��
	//FailIndex�аO. �b�ĴX��512Byte��Index�X��.

	//else if ((FailIndex<=(DATA_MACRO_BLOCK/DATA_BLOCK_SIZE)) && (FailIndex>0))
	if (FailIndex==0)
	//���έ״_
	{
		printf("just make~~~~~~fail index=%d\n",FailIndex);
		//��CarRecorder.

		CarRecorder->DataBlockWriteBase = OFFSET_DATA_BLOCK;
		CarRecorder->IndexSectorWriteBase[0] = OFFSET_MARK_TABLE_0;
		CarRecorder->IndexSectorWriteBase[1] = OFFSET_MARK_TABLE_1;

		//CarRecorder->DataBlockWriteOffset = MaxIndexPositionLong +DATA_MACRO_BLOCK;	//@@�U�Ӷ}�l�g���a��
#if defined(AUTO_SPACE)
		//if (CarRecorder->DataBlockWriteOffset >= LenDataBlock[CarRecorder->num])
		//	CarRecorder->DataBlockWriteOffset -= LenDataBlock[CarRecorder->num];
		CarRecorder->IndexSectorWriteOffset 	= (MaxIndexPosition+1)*sizeof(INDEX_SECTOR_T);	//@@
		if (CarRecorder->IndexSectorWriteOffset >= (DiskDataBlocks[CarRecorder->num]*INDEX_SECTOR_SIZE))
			CarRecorder->IndexSectorWriteOffset -=(DiskDataBlocks[CarRecorder->num]*INDEX_SECTOR_SIZE);
#else
		if (CarRecorder->DataBlockWriteOffset >= LEN_DATA_BLOCK)
			CarRecorder->DataBlockWriteOffset -= LEN_DATA_BLOCK;
		CarRecorder->IndexSectorWriteOffset 	= (MaxIndexPosition+1)*sizeof(INDEX_SECTOR_T);	//@@
		if (CarRecorder->IndexSectorWriteOffset >= (DISK_DATA_BLOCKS*INDEX_SECTOR_SIZE))
			CarRecorder->IndexSectorWriteOffset -=(DISK_DATA_BLOCKS*INDEX_SECTOR_SIZE);
#endif
		CarRecorder->DataBlockSerial = DataBlockEndSerial;	//@@ so, this is the last serial being written
		CarRecorder->IndexSector.IndexTable.IndexTableSerial = is2.IndexTable.IndexTableSerial;	//@@
		CarRecorder->DataBlock = 0;
		CarRecorder->CurrentChannel = CHANNEL_IN;		//@@
		CarRecorder->FlagRecordStart[0] = CarRecorder->FlagRecordStart[1] = 1;
		CarRecorder->PingPong = 0;
	}
	else
	//�u�n�[�W. stop . end time. �åB�[���U�@��index
	{
		printf("next index~~~~~~\n");
		//�״_Index,�A��CarRecorder.

		memset((char *)&is2, 0, sizeof(INDEX_SECTOR_T));// 512); //ivan+
		is2.Tag = ENDIAN_INT(TAG_SECTOR_TABLE);
		//is2.IndexTable.IndexTableSerial = ENDIAN_INT(ENDIAN_INT(is2.IndexTable.IndexTableSerial)+1);
		CarRecorder->itSerial++; //ivan+
		is2.IndexTable.IndexTableSerial = ENDIAN_INT(CarRecorder->itSerial);
		memset((char *)is2.IndexTable.EventInfo, 0, sizeof(EVENT_INFO_T)*MAX_EVENT_EACH_16MB_DATA);
		memcpy((char *)is2.IndexTable.DataBlockTimeStamp, (char *)DataBlockTimeStamp, 6);
		is2.IndexTable.DataBlockSerial = ENDIAN_INT(MaxIndexDataBlockSerial);

		is2.IndexTable.FlagRecordStart = 1;	//since this is the NEXT index
		is2.IndexTable.FlagRecordEnd = 1;
		is2.IndexTable.FlagMask = FlagMask;
		is2.IndexTable.EventInfoCounter = 0;
		is2.IndexTable.Reserved2[0] = is2.IndexTable.Reserved2[1] = 0;
		is2.IndexTable.DataBlockEndSerial = ENDIAN_INT(DataBlockEndSerial);
		memcpy((char *)is2.IndexTable.DataBlockEndTimeStamp,DataBlockEndTimeStamp,6);
		is2.GSensorSensitivityVer = ENDIAN_SHORT(1); //ivan+
		is2.GSensorSensitivityAlgorithm = ENDIAN_SHORT(1); //ivan+
		memset((char *)is2.Reserved, 0, INDEX_REVERSED_SIZE); //ivan+

		is2.Checksum = calc_checksum( (unsigned long *)&is2,(DATA_TABLE_SIZE-4)/4);
		//
		CarRecorder->DataBlockWriteBase = OFFSET_DATA_BLOCK;
		CarRecorder->IndexSectorWriteBase[0] = OFFSET_MARK_TABLE_0;
		CarRecorder->IndexSectorWriteBase[1] = OFFSET_MARK_TABLE_1;

		//CarRecorder->DataBlockWriteOffset = MaxIndexPositionLong+DATA_MACRO_BLOCK+DATA_MACRO_BLOCK;	//@@
#if defined(AUTO_SPACE)
		//if (CarRecorder->DataBlockWriteOffset >= LenDataBlock[CarRecorder->num])
		//	CarRecorder->DataBlockWriteOffset -= LenDataBlock[CarRecorder->num];
		CarRecorder->IndexSectorWriteOffset = (MaxIndexPosition+1)*sizeof(INDEX_SECTOR_T);
		if (CarRecorder->IndexSectorWriteOffset >= (DiskDataBlocks[CarRecorder->num]*INDEX_SECTOR_SIZE))
			CarRecorder->IndexSectorWriteOffset -=(DiskDataBlocks[CarRecorder->num]*INDEX_SECTOR_SIZE);
#else
		if (CarRecorder->DataBlockWriteOffset >= LEN_DATA_BLOCK)
			CarRecorder->DataBlockWriteOffset -= LEN_DATA_BLOCK;
		CarRecorder->IndexSectorWriteOffset = (MaxIndexPosition+1)*sizeof(INDEX_SECTOR_T);	//@@
		if (CarRecorder->IndexSectorWriteOffset >= (DISK_DATA_BLOCKS*INDEX_SECTOR_SIZE))
			CarRecorder->IndexSectorWriteOffset -=(DISK_DATA_BLOCKS*INDEX_SECTOR_SIZE);
#endif
		CarRecorder->DataBlockSerial = DataBlockEndSerial;	//@@ so, this is the last serial being written
		CarRecorder->IndexSector.IndexTable.IndexTableSerial = is2.IndexTable.IndexTableSerial;
		CarRecorder->DataBlock = 0;
		CarRecorder->CurrentChannel = CHANNEL_IN;		//@@
		CarRecorder->FlagRecordStart[0] = CarRecorder->FlagRecordStart[1] = 1;
		CarRecorder->PingPong = 0;

		off = CarRecorder->IndexSectorWriteBase[0]+CarRecorder->IndexSectorWriteOffset;
		if (lseek64(CarRecorder->StorageDescriptor, off, SEEK_SET) < 0)
		{
			DDPRINTF("[%s].4 seek %llx fail\n", __func__, off);
			return (-1);
		}
		if (write(CarRecorder->StorageDescriptor, (char *)&is2, INDEX_SECTOR_SIZE) < 0)
		{
			DDPRINTF("[%s].4 write %llx fail\n", __func__, off);
			return (-1);
		}

		DDPRINTF("\033[1;31m[%s]\033[m IndexTableSerial=0x%08X, DataBlockSerial=0x%08X\n",
			__func__, ENDIAN_INT(is2.IndexTable.IndexTableSerial),
			ENDIAN_INT(is2.IndexTable.DataBlockSerial) );

		off = CarRecorder->IndexSectorWriteBase[1]+CarRecorder->IndexSectorWriteOffset;
		if (lseek64(CarRecorder->StorageDescriptor, off, SEEK_SET) < 0)
		{
			DDPRINTF("[%s].5 seek %llx fail\n", __func__, off);
			return (-1);
		}
		if (write(CarRecorder->StorageDescriptor, (char *)&is2, INDEX_SECTOR_SIZE) < 0)
		{
			DDPRINTF("[%s].5 write %llx fail\n", __func__, off);
			return (-1);
		}
		CarRecorder->IndexSectorWriteOffset += sizeof(INDEX_SECTOR_T);
#if defined(AUTO_SPACE)
		if (CarRecorder->IndexSectorWriteOffset >= (DiskDataBlocks[CarRecorder->num]*INDEX_SECTOR_SIZE))
			CarRecorder->IndexSectorWriteOffset -= (DiskDataBlocks[CarRecorder->num]*INDEX_SECTOR_SIZE);
#else
		if (CarRecorder->IndexSectorWriteOffset >= (DISK_DATA_BLOCKS*INDEX_SECTOR_SIZE))
			CarRecorder->IndexSectorWriteOffset -= (DISK_DATA_BLOCKS*INDEX_SECTOR_SIZE);
#endif
	}

	return 0;
}



int FixIndexTable(CAR_RECORDER_T *CarRecorder)
{
	unsigned int MaxIndexTableSerial[2];
	int MaxIndexPosition[2]={0, 0};

	//FIX_CHECK_TABLE:
	CarRecorder->DataBlockWriteBase = OFFSET_DATA_BLOCK;
	//CarRecorder->DataBlockWriteOffset = 0;
	CarRecorder->IndexSectorWriteBase[0] = OFFSET_MARK_TABLE_0;
	CarRecorder->IndexSectorWriteBase[1] = OFFSET_MARK_TABLE_1;
	CarRecorder->IndexSectorWriteOffset = 0;

	//CheckIndexTable ������ IndexSectorTable ���@�M�A���X�s���̤j��
	MaxIndexTableSerial[0] = CheckIndexTable(CarRecorder, 0, &MaxIndexPosition[0]);
	MaxIndexTableSerial[1] = CheckIndexTable(CarRecorder, 1, &MaxIndexPosition[1]);


DDPRINTF("\033[1;35m[%s]\033[m usb %d MaxIndexTableSerial=(%08x, %08x), pos=(%d, %d)\n",
	__func__, CarRecorder->num, 
	MaxIndexTableSerial[0], MaxIndexTableSerial[1],
	MaxIndexPosition[0], MaxIndexPosition[1]);


	//���� table ���O 0 ���ܡA�i������ pen driver ���٥����L initial
	if( (MaxIndexTableSerial[0]==0) && (MaxIndexTableSerial[1]==0) )
	{
		printf("[%s] IndexSectorTable not initial yet.\n", __func__);
		if(InitIndexTable(CarRecorder, 0) < 0)
		{
			return -1;
		}
		if(InitIndexTable(CarRecorder, 1) < 0)
		{
			return -1;
		}
		MaxIndexPosition[0] = MaxIndexPosition[1] = -1;
	}
	else if (MaxIndexTableSerial[1] >= MaxIndexTableSerial[0])
	{
		printf("[%s] copy IndexSectorTable 1 to 0\n", __func__);
		CopyIndexTable(CarRecorder, 1, 0);
		MaxIndexPosition[0] = MaxIndexPosition[1];
		MaxIndexTableSerial[0] = MaxIndexTableSerial[1];
		CarRecorder->IndexSectorBuffer.IndexTable.IndexTableSerial = ENDIAN_INT(MaxIndexTableSerial[1]);
	}
	else
	{
		printf("[%s] copy IndexSectorTable 0 to 1\n", __func__);
		CopyIndexTable(CarRecorder, 0, 1);
		MaxIndexPosition[1] = MaxIndexPosition[0];
		MaxIndexTableSerial[1] = MaxIndexTableSerial[0];
		CarRecorder->IndexSectorBuffer.IndexTable.IndexTableSerial = ENDIAN_INT(MaxIndexTableSerial[0]);
	}
	DDPRINTF("\033[1;36m[%s]\033[m USB %d : MaxIndexTableSerial=(0x%08x, 0x%08x) --> 0x%08x\n",
		__func__, CarRecorder->num+1, 
		(MaxIndexTableSerial[0]), 
		(MaxIndexTableSerial[1]),
		ENDIAN_INT(CarRecorder->IndexSectorBuffer.IndexTable.IndexTableSerial) );

	CarRecorder->itSerial = MaxIndexTableSerial[0]; //ivan+

/*
�ҥH���j�����v��..�̫��@�q���״_�|���T�ӱ��p
1. �U�@�q�N�O�̫᣸��block..���ثe��SerialIndex�s��
2. �U�@�q�O���s�򪺱j�����v..�ҥH�n������..�����S�j�����v�����q���v...
3. �U�@�q�]�O�j�����v..���OMarkTable�٨S���аO..�u�bsubTable���аO
�o���������j�����v��!!
*/

	//Serial=0. ���ܭn�״_�Ĥ@��DATA_BLOCK. �åBserialIndex��ӭn�q1�}�l.�o�쪺.�P�ɦ�start/stop flag
	if (MaxIndexTableSerial[0]==0)
 	{
		DDPRINTF("\033[1;32m[%s]\033[m Fix first data block\n", __func__);
		FixFirstDataBlockAndIndex(CarRecorder);	//Mask->�����^��?
	}
	else //MaxIndexTableSerial0>0
	{
		int	StopFlag;

		StopFlag = CheckIndexTableStop(CarRecorder,0,MaxIndexPosition[0]);
		VBUS_CHECK();
		if (StopFlag)
		{
			printf("[%s] StopFlag==1\n", __func__);
			FixIndexStop1(CarRecorder,0,MaxIndexPosition[0]);
			VBUS_CHECK();
			printf("[%s] fix stop flag 1 complete\n", __func__);
		}
		else
		{
			printf("[%s] StopFlag==0\n", __func__);
			FixIndexStop0(CarRecorder,0,MaxIndexPosition[0]);
			VBUS_CHECK();
			printf("[%s] fix stop flag 0 complete\n", __func__);
		}
	}

	return 1;
}

int FillIndexBuffer(int disk, CAR_RECORDER_T *CarRecorder)
{
	//off64_t off = CarRecorder->IndexSectorWriteBase[disk];
	off64_t off = CarRecorder->IndexSectorWriteBase[0];
	if (lseek64(CarRecorder->StorageDescriptor, off, SEEK_SET) < 0)
	{
		DDPRINTF("[%s] seek %llx fail\n", __func__, off);
		return (-1);
	}

	errno = 0;
#if defined(AUTO_SPACE)
	if (read(CarRecorder->StorageDescriptor, (char *)IndexBuffer[disk], DiskDataBlocks[CarRecorder->num]*INDEX_SECTOR_SIZE) < 0)
	{
		DDPRINTF("[%s] read %llx fail because '%s'\n", __func__, off, strerror(errno));
		return (-1);
	}
	//memset(IndexBufferDirtyFlag[disk], 0, DiskDataBlocks[CarRecorder->num]*sizeof(int));
#else
	if (read(CarRecorder->StorageDescriptor, &IndexBuffer[disk][0], INDEX_SECTOR_SIZE*DISK_DATA_BLOCKS) < 0)
	{
		DDPRINTF("[%s] read %llx fail because '%s'\n", __func__, off, strerror(errno));
		return (-1);
	}
	for (x=0; x<DISK_DATA_BLOCKS; x++)
		IndexBufferDirtyFlag[disk][x] = 0;
#endif

	return 0;
}



int GetPartDiskSerial(CAR_RECORDER_T *CarRecorder)
{
	int serial;

	off64_t off = OFFSET_DISK_SERIAL;
	if (lseek64(CarRecorder->StorageDescriptor, off, SEEK_SET) < 0)
	{
		DDPRINTF("[%s] seek 0x%llx fail\n", __func__, off);
		return (-1);
	}

	if (read(CarRecorder->StorageDescriptor, &serial, sizeof(serial)) < 0)
	{
		DDPRINTF("[%s] read 0x%llx fail\n", __func__, off);
		return (-1);
	}
	return (serial);
}

int UpdateDiskSerial(DUAL_USB_T *DualUSB, int serial)
{
	off64_t off = OFFSET_DISK_SERIAL;
	if (lseek64(DualUSB->CarRecorder[DualUSB->ActiveCarRecorder].StorageDescriptor, off, SEEK_SET) < 0)
	{
		DDPRINTF("[%s] seek 0x%llx fail\n", __func__, off);
		return (-1);
	}

	if (write(DualUSB->CarRecorder[DualUSB->ActiveCarRecorder].StorageDescriptor, &serial, sizeof(serial)) < 0)
	{
		DDPRINTF("[%s] read 0x%llx fail, serial=%d\n", __func__, off, serial);
		return (-1);
	}
	return (1);
}

int UpdateFlashSerial(DUAL_USB_T *DualUSB, int serial)
{
	int fd_serial;
	char buffer[10];
	int sum;

	sum = serial;
	fd_serial = open("/root/serial", O_WRONLY | O_CREAT | O_TRUNC, 0666);
	memset(buffer, 0, 10);
	sprintf(buffer, "%d", sum);
	write(fd_serial, buffer, strlen(buffer));
	close(fd_serial);
	return 0;
}


int CheckCurrentMark(int disk, CAR_RECORDER_T *CarRecorder)
{
	char *ptr;
	int ret;
	INDEX_SECTOR_T *ist;

	ptr = (char *)IndexBuffer[disk];
	ptr += CarRecorder->IndexSectorWriteOffset;
	ist = (INDEX_SECTOR_T *)ptr;

	switch(ist->IndexTable.FlagMask)
	{
		case EVENT_TYPE_OVER_SPEED:
		case EVENT_TYPE_GSENSOR_TRIGGER:
		case EVENT_TYPE_HAND_TRIGGER:
		case EVENT_TYPE_SUICIDE:
			ret = INDEX_MARK;
			break;
		default:
			ret = INDEX_NO_MARK;
			break;
	}

//DDPRINTF("\033[1;35m[%s] use 'IndexBuffer[%d]', currentMark=0x%02X, ret=%d\033[m \n", __func__, disk, ist->IndexTable.FlagMask, ret);

	return ret;
}


#if defined(AUTO_SPACE)

int CheckMarkThreshold(int disks)
{
	int i, x, limits=0;
	if( (disks <= 0) || (disks > 2) )
		return (MARK_THRESHOLD_STOP);

	for(i=0; i<2; i++)
	{
		INDEX_SECTOR_T *ist = IndexBuffer[i];
		for (x=0; x<DiskDataBlocks[i]; x++)
		{
			switch(ist->IndexTable.FlagMask)
			{
				case EVENT_TYPE_OVER_SPEED:
				case EVENT_TYPE_GSENSOR_TRIGGER:
				case EVENT_TYPE_HAND_TRIGGER:
				case EVENT_TYPE_SUICIDE:
					limits++;
					break;
				default:
					break;
			}
			ist++;
		}
	}

	int total_blocks = DiskDataBlocks[0] +DiskDataBlocks[1];
DDPRINTF("\033[1;32m[%s]\033[m total (%d + %d = %d) blocks, marked %d blocks\n", __func__, DiskDataBlocks[0], DiskDataBlocks[1], total_blocks , limits);

	if (limits >= total_blocks)
		return (MARK_THRESHOLD_STOP);
	//else if (limits >= DISK_LIMIT_BLOCKS*disks) //ivan note, wait for change, 20101008
	//	return (MARK_THRESHOLD_OVER);
	else
		return (MARK_THRESHOLD_UNDER);
}

#else

int CheckMarkThreshold(int disks)
{
	int x, limits=0;
	INDEX_SECTOR_T *ptrTempBuffer = &IndexBuffer[0][0];
	if( (disks <= 0) || (disks > 2) )
		return (MARK_THRESHOLD_STOP);

	for (x=0; x<DISK_DATA_BLOCKS*disks; x++)
	{
		switch(ptrTempBuffer->IndexTable.FlagMask)
		{
			case EVENT_TYPE_OVER_SPEED:
			case EVENT_TYPE_GSENSOR_TRIGGER:
			case EVENT_TYPE_HAND_TRIGGER:
			case EVENT_TYPE_SUICIDE:
				limits++;
				break;
			default:
				break;
		}
		ptrTempBuffer++;
	}

//DDPRINTF("\033[1;32m[%s]\033[m total (%d + %d = %d) blocks, marked %d blocks\n", __func__, DiskDataBlocks[0], DiskDataBlocks[1], total_blocks , limits);

	if (limits >= DISK_DATA_BLOCKS*disks)
		return (MARK_THRESHOLD_STOP);
	else if (limits >= DISK_LIMIT_BLOCKS*disks)
		return (MARK_THRESHOLD_OVER);
	else
		return (MARK_THRESHOLD_UNDER);
}

#endif



int WriteIndexSectorToDisk(CAR_RECORDER_T *CarRecorder, int ActiveCarRecorder)
{
	VBUS_CHECK();

	if(istc.prev != (INDEX_SECTOR_T *)(0) )
	{
		int i;
		istc.prev->IndexTable.FlagRecordEnd = 0;
		istc.prev->Checksum = calc_checksum((unsigned long *)istc.prev, (INDEX_SECTOR_SIZE-4)/4);
		for(i=0; i<2; i++)
		{
			if (lseek64(CarRecorder->StorageDescriptor, CarRecorder->IndexSectorWriteBase[i]+istc.prev_off, SEEK_SET) < 0)
			{
				DDPRINTF("[%s] seek.prev 0x%llx fail.\n", __func__, CarRecorder->IndexSectorWriteBase[i]+istc.prev_off);
			}

			if (write(CarRecorder->StorageDescriptor, istc.prev, sizeof(INDEX_SECTOR_T)) < 0)
			{
				DDPRINTF("[%s] write prev. fail (@position 0x%llx) error.\n", __func__, CarRecorder->IndexSectorWriteBase[i]+istc.prev_off);
			}
			else
			{
				//DDPRINTF("[%s] .. write prev. index sector table to 0x%08llX\n", __func__, CarRecorder->IndexSectorWriteBase[i]+istc.prev_off);
			}
		}
	}
	CarRecorder->IndexSectorBuffer.prev_off = istc.ptr->prev_off;
	CarRecorder->IndexSectorBuffer.self_off = istc.ptr->self_off;
	CarRecorder->IndexSectorBuffer.IndexTable.FlagRecordEnd = 1;
	CarRecorder->IndexSectorBuffer.Checksum = calc_checksum((unsigned long *)&CarRecorder->IndexSectorBuffer, (INDEX_SECTOR_SIZE-4)/4);


	//-------------------------------------------------------------------------------------------
	off64_t off = CarRecorder->IndexSectorWriteBase[0] + CarRecorder->IndexSectorWriteOffset;

	struct timeval tm;
	gettimeofday(&tm, NULL);

	if (lseek64(CarRecorder->StorageDescriptor, off, SEEK_SET) < 0)
	{
		DDPRINTF("[%s] seek 0x%llx fail.\n", __func__, off);
	}

	if (write(CarRecorder->StorageDescriptor, (char *)&(CarRecorder->IndexSectorBuffer), sizeof(INDEX_SECTOR_T)) < 0)
	{
		DDPRINTF("[%s] write position 0x%llx error.\n", __func__, off);
	}
	else
	{
		//DDPRINTF("[%s] .. write index sector table to 0x%08llX\n", __func__, off);
	}

	off = CarRecorder->IndexSectorWriteBase[1] + CarRecorder->IndexSectorWriteOffset;
	if (lseek64(CarRecorder->StorageDescriptor, off, SEEK_SET) < 0)
	{
		DDPRINTF("[%s].2 seek 0x%llx fail.\n", __func__, off);
	}

	if (write(CarRecorder->StorageDescriptor, (char *)&(CarRecorder->IndexSectorBuffer), sizeof(INDEX_SECTOR_T))<0)
	{
		DDPRINTF("[%s].2 write position 0x%llx error.\n", __func__, off);
	}
	sync();
	//DDPRINTF("\033[1;31m[%s]\033[m .. write index sector table to 0x%08llX, ITS=0x%08X, DBS=0x%08X\n",
	//	__func__, off,
	//	ENDIAN_INT(CarRecorder->IndexSectorBuffer.IndexTable.IndexTableSerial),
	//	ENDIAN_INT(CarRecorder->IndexSectorBuffer.IndexTable.DataBlockSerial) );

//debug message
{
	struct timeval tm2;
	gettimeofday(&tm2, NULL);
	if(tm2.tv_usec < tm.tv_usec)
	{
		tm2.tv_sec -= 1;
		tm2.tv_usec += (1000*1000);
	}
	time_t s = tm2.tv_sec -tm.tv_sec;
	suseconds_t us = tm2.tv_usec -tm.tv_usec;

	DDPRINTF("\033[31m[%s]\033[m Active USB is %d, write off=0x%llx, write %4d bytes span %d.%06d second(s)\n", __func__, ActiveCarRecorder, off, sizeof(INDEX_SECTOR_T)*2, (int)s, (int)us);
}

	memcpy((char *)&IndexBuffer[ActiveCarRecorder][CarRecorder->IndexSectorWriteOffset/sizeof(INDEX_SECTOR_T)],
		   (char *)&(CarRecorder->IndexSectorBuffer),
		   sizeof(INDEX_SECTOR_T));

	CarRecorder->IndexSectorWriteOffset += sizeof(INDEX_SECTOR_T);
	CarRecorder->xx = 0;

	return 1;
}




int WriteOneIndexSector(CAR_RECORDER_T *CarRecorder, int FlagMarkThreshold, int ActiveCarRecorder)
{
	char mask=0;
	int i, a, b;
	unsigned char record_end=0;

	//end current recording while change USB
#if defined(AUTO_SPACE)
	if((CarRecorder->IndexSectorWriteOffset +sizeof(INDEX_SECTOR_T)) >= (DiskDataBlocks[CarRecorder->num]*INDEX_SECTOR_SIZE))
		record_end = 1;
	else
		record_end = 0;
#else
	if((CarRecorder->IndexSectorWriteOffset + sizeof(INDEX_SECTOR_T))>=(DISK_DATA_BLOCKS*INDEX_SECTOR_SIZE))
		record_end = 1;
	else
		record_end = 0;
#endif

	CarRecorder->IndexSector.IndexTable.FlagRecordEnd = 0;
	CarRecorder->IndexSector.IndexTable.FlagMask = 0;
	CarRecorder->IndexSector.IndexTable.EventInfoCounter = 0;
	CarRecorder->IndexSector.Tag = ENDIAN_INT(TAG_SECTOR_TABLE);
	CarRecorder->IndexSector.GSensorSensitivityVer = ENDIAN_SHORT(1); //ivan+
	CarRecorder->IndexSector.GSensorSensitivityAlgorithm = ENDIAN_SHORT(1); //ivan+
	CarRecorder->itSerial++; //ivan+
	CarRecorder->IndexSector.IndexTable.IndexTableSerial = ENDIAN_INT(CarRecorder->itSerial);
	//CarRecorder->IndexSector.IndexTable.IndexTableSerial = (CarRecorder->IndexSectorBuffer.IndexTable.IndexTableSerial+1);

//DDPRINTF("\033[32m[%s]\033[m IndexTableSerial=0x%08x\n", __func__, ENDIAN_INT(CarRecorder->IndexSector.IndexTable.IndexTableSerial) );

	CarRecorder->IndexSector.IndexTable.FlagRecordEnd = record_end;
	EventPtr = &EventMask[0][0];
	for(i=0; i<128; i++)
	{
	  	if(*(EventPtr+i))
	  	{
			mask = *(EventPtr+i);
			break;	//we got first event only
		}
	}

	CarRecorder->IndexSector.IndexTable.FlagMask = mask;
	if ( (mask==EVENT_TYPE_NULL) && (FlagMarkThreshold==MARK_THRESHOLD_OVER) )
		CarRecorder->IndexSector.IndexTable.FlagMask = EVENT_TYPE_SUICIDE;

	CarRecorder->IndexSector.IndexTable.EventInfoCounter = 0;
	memset(&CarRecorder->IndexSector.IndexTable.EventInfo[0], 0, sizeof(EVENT_INFO_T)*MAX_EVENT_EACH_16MB_DATA);
	if ((mask!=EVENT_TYPE_NULL)&&(FlagMarkThreshold!=MARK_THRESHOLD_OVER))
	{
		for (a=0; a<16; a++)
			for (b=0; b<8; b++)
			{
				if (EventMask[a][b])
				{
					CarRecorder->IndexSector.IndexTable.EventInfo[a].DataBlockSerial = EventInfo[a][b].DataBlockSerial;
					CarRecorder->IndexSector.IndexTable.EventInfo[a].EventType = EventInfo[a][b].EventType;
					memcpy(CarRecorder->IndexSector.IndexTable.EventInfo[a].EventTime, EventInfo[a][b].EventTime, 6);
					CarRecorder->IndexSector.IndexTable.EventInfoCounter++;
					break;	//���Ĥ@��event
				}
			}
	}

	memset(&CarRecorder->IndexSector.IndexTable.Reserved2[0], 0, 2);
	memset(&CarRecorder->IndexSector.Reserved[0], 0, INDEX_REVERSED_SIZE);

	CarRecorder->IndexSectorBuffer = CarRecorder->IndexSector;

	WriteIndexSectorToDisk(CarRecorder, ActiveCarRecorder);

{
	INDEX_SECTOR_T *ist = (INDEX_SECTOR_T *)&CarRecorder->IndexSectorBuffer;
	unsigned char *p;
	printf("IndexTableSerial : %08X\n", ENDIAN_INT(ist->IndexTable.IndexTableSerial) );
	printf("DataBlockSerial  : %08X\n", ENDIAN_INT(ist->IndexTable.DataBlockSerial) );
	p = ist->IndexTable.DataBlockTimeStamp;
	printf("Start TimeStamp  : %04d.%02d.%02d %02d:%02d:%02d\n", p[0]+2000, p[1], p[2], p[3], p[4], p[5]);
	p = ist->IndexTable.DataBlockEndTimeStamp;
	printf("End TimeStamp    : %04d.%02d.%02d %02d:%02d:%02d\n", p[0]+2000, p[1], p[2], p[3], p[4], p[5]);
	printf("RecordStart=%d, RecordEnd=%d, FlagMask=%02X, checksum=%08lX\n",
		ist->IndexTable.FlagRecordStart,
		ist->IndexTable.FlagRecordEnd,
		ist->IndexTable.FlagMask,
		ist->Checksum);
}

	return 1;
}



int	GetDataBlockFromReadBuffer(CAR_RECORDER_T *CarRecorder)
{
	int i, chn;

	pthread_mutex_lock(&remain_mutex);
#if (CLIENTS == 2)
	while( (rc[0].b->data_len < (DATA_BLOCK_SIZE-512)) &&
			(rc[1].b->data_len < (DATA_BLOCK_SIZE-512)))
	{
//DDPRINTF("\033[1;36m[%s] wait.... 0.data_len=%d, 1.data_len=%d\033[m\n",
//	__func__, rc[0].b->data_len, rc[1].b->data_len);

		pthread_cond_wait(&noBuffer, &remain_mutex);
	}

	if(rc[0].b->data_len > rc[1].b->data_len)
		chn = 0;
	else
		chn = 1;
#else
	while( rc[0].b->data_len < (DATA_BLOCK_SIZE-512) )
	{
		pthread_cond_wait(&noBuffer, &remain_mutex);
	}

	chn = 0;

#endif

	pthread_cond_signal(&noBuffer);
	pthread_mutex_unlock(&remain_mutex);


	struct EBuf *eb = rc[chn].b;
	struct BufLink *bl;

	CarRecorder->CurrentChannel = chn;

	int l_size = DATA_BLOCK_SIZE-512;
	int a_size = 0;
	unsigned char *ptr;
	int cs;
	while(l_size > 0)
	{
		bl = &eb->BUF[eb->rear];
		cs = bl->len;
		ptr = &bl->buf[bl->skip];

		if(cs == 0)	//impossible
		{
			eb->rear++;
			if(eb->rear == BL_NUM)
				eb->rear = 0;
			continue;
		}

		if(l_size < cs) //left space not enough for this packet
		{
			memcpy(&CarRecorder->Buffer[CarRecorder->DataBlock][a_size], ptr, l_size);
			a_size += l_size;
			eb->data_len -= l_size;
			bl->len -= l_size;
			bl->skip += l_size;
			l_size = 0;
			break;
		}
		else
		{
			memcpy(&CarRecorder->Buffer[CarRecorder->DataBlock][a_size], ptr, cs);
			a_size += cs;
			eb->data_len -= cs;
			eb->rear++;
			if(eb->rear == BL_NUM)
				eb->rear = 0;
			l_size -= cs;
		}
	}

#if 1	//SWAP

  #if 0
	unsigned char *ptrChar, a;
	ptrChar = (unsigned char *)&CarRecorder->Buffer[CarRecorder->DataBlock][0];
	for (i=0; i<(DATA_BLOCK_SIZE-512); i+=4)
	{
		a = *(ptrChar +3);
		*(ptrChar +3) = *(ptrChar +0);
		*(ptrChar +0) = a;
		a = *(ptrChar +2);
		*(ptrChar +2) = *(ptrChar +1);
		*(ptrChar +1) = a;
		ptrChar += 4;
	}

  #else

	unsigned long *ul = (unsigned long *)&CarRecorder->Buffer[CarRecorder->DataBlock][0];
	for (i=0; i<(DATA_BLOCK_SIZE-512); i+=4)
	{
		*ul = ( (*ul << 24) | ( (*ul&0x0000FF00) << 8) | ( (*ul&0x00FF0000) >> 8) | (*ul >> 24) );
		ul++;
	}

  #endif

#endif

	return MORE_THAN_DATA_BLCOKS;
	//return LESS_THAN_DATA_BLCOKS;
}



int FillDataTable(CAR_RECORDER_T *CarRecorder)
{
	static int EventCounter = 0;
	DATA_TABLE_T *DataTable,*DataTable2;
	time_t DateTime;
	struct tm tm;
	int	Speed=1, GSensorX=1, GSensorY=1, GSensorZ=1;
	EVENT_INFO_T *Info;

	//event clean;
	EventInfoPtr = &EventInfo[0][0];
	Info = EventInfoPtr+EventCounter; //event counter increased and looping every 16*8
	memset(Info, 0, sizeof(EVENT_INFO_T));
	/*
	Info->DataBlockSerial = 0;
	for(index1=0; index1<6; index1++)
		Info->EventTime[index1] = 0;
	Info->EventType = 0;
	*/

	DataTable2 = (DATA_TABLE_T *)&(CarRecorder->Buffer[CarRecorder->DataBlock][DATA_BLOCK_SIZE -DATA_TABLE_SIZE]);
	DataTable = &CarRecorder->DataTable;

	DataTable->Tag = ENDIAN_INT(TAG_DATA_TABLE);
	++CarRecorder->DataBlockSerial;
	DataTable->DataBlockSerial = ENDIAN_INT(CarRecorder->DataBlockSerial);

	time(&DateTime);
	tm =*(gmtime(&DateTime));
	//tm =*(localtime(&DateTime));

	DataTable->DataBlockTimeStamp[0] = (unsigned char)(tm.tm_year -100);
	DataTable->DataBlockTimeStamp[1] = tm.tm_mon + 1;
	DataTable->DataBlockTimeStamp[2] = tm.tm_mday;
	DataTable->DataBlockTimeStamp[3] = tm.tm_hour;
	DataTable->DataBlockTimeStamp[4] = tm.tm_min;
	DataTable->DataBlockTimeStamp[5] = tm.tm_sec;

	if (CarRecorder->CurrentChannel == 0)
		DataTable->Channel = 0;
	else
		DataTable->Channel = 2;

	if (CarRecorder->FlagRecordStart[CarRecorder->CurrentChannel])	//�}���ɬO1.
	{
		CarRecorder->FlagRecordStart[CarRecorder->CurrentChannel] = 0;
		DataTable->FlagRecordStart = 1;
	}
	else
		DataTable->FlagRecordStart = 0;

	DataTable->FlagMask = 0;
	DataTable->GSensorCounter = 0;
	DataTable->Resolution = currRes[CarRecorder->CurrentChannel];// RES_N_VGA;
	DataTable->FrameRate = currFrr[CarRecorder->CurrentChannel];//30;
	DataTable->FlagAudio = 0;
	memset(DataTable->Reserved, 0, sizeof(DataTable->Reserved));
	memset(&DataTable->GSensorData[0][0], 0, sizeof(DataTable->GSensorData));
	/*
	for (index1=0; index1<26; index1++)
		DataTable->Reserved[index1] = 0;
	for (index1=0; index1<50; index1++)
		for (index2=0; index2<3; index2++)
			DataTable->GSensorData[index1][index2] = 0;
	*/

	pthread_mutex_lock(&gps_mutex);
	//gps_init�u���ܦ����ƥiŪ. �ä����ܩw�����F
	if (gps_init)
	{
		DataTable->GPSCounter = 1;
		memcpy(DataTable->GPSData, gps_buffer, 160);

		GSensorX = CarGSensorX;
		GSensorY = CarGSensorY;
		GSensorZ = CarGSensorZ;

		if (gps_init == GPS_INIT_GPS)
			Speed = CarSpeed;
		else
			Speed = 0;
	}
	else
	{
		DataTable->GPSCounter = 0;
		memset(DataTable->GPSData, 0, sizeof(DataTable->GPSData));
		//for (index2=0; index2<160; index2++)
		//	DataTable->GPSData[index2] = 0;
	}
	pthread_mutex_unlock(&gps_mutex);

	DataTable->FlagMask = 0;

	//ivan+ 'ForceRecord'
	if(forceRecord)
	{
		DataTable->FlagMask = EVENT_TYPE_HAND_TRIGGER;
		Info->EventType = ENDIAN_SHORT(EVENT_TYPE_HAND_TRIGGER);
		Info->DataBlockSerial = DataTable->DataBlockSerial;
		memcpy(Info->EventTime, DataTable->DataBlockTimeStamp, 6);
		KeyStroke = 0;
	}

//�W�t
	if( ( (gps_init == GPS_INIT_GPS) && 
		   (CarRecorder->CarSetting.Limit) && 
		   (CarRecorder->CarSetting.Value < Speed) ) ||
//		(OverSpeedNow) ||
		(KeyStroke=='s') )
	{

DDPRINTF("[%s] KeyStroke='%c', gps_init=%d, limit_value=%d, speed=%d\n",
	__func__, KeyStroke, gps_init, CarRecorder->CarSetting.Value, Speed);

		DataTable->FlagMask = EVENT_TYPE_OVER_SPEED;
		Info->EventType = ENDIAN_SHORT(EVENT_TYPE_OVER_SPEED);
		Info->DataBlockSerial = DataTable->DataBlockSerial;
		memcpy(Info->EventTime, DataTable->DataBlockTimeStamp, 6);
		KeyStroke = 0;
		BuzzerStatus = BUZZER_STATUS_OVER_SPEED;
	}



	//g-sensor�i�H�F���W�t
	if( (gps_init && ((CarRecorder->CarSetting.XAxis < GSensorX) ||
		               (CarRecorder->CarSetting.YAxis < GSensorY) ||
		               (CarRecorder->CarSetting.ZAxis < GSensorZ)) ) ||
		(OverGSensor) ||
		(KeyStroke=='g') )
	{

if(!OverGSensor)
DDPRINTF("[%s] KeyStroke='%c', gps_init=%d, G-setting(%d, %d, %d) got(%d, %d, %d)\n",
	__func__, KeyStroke, gps_init,
	CarRecorder->CarSetting.XAxis, CarRecorder->CarSetting.YAxis, CarRecorder->CarSetting.ZAxis,
	GSensorX, GSensorY, GSensorZ);

		DataTable->FlagMask = EVENT_TYPE_GSENSOR_TRIGGER;
		Info->EventType = ENDIAN_SHORT(EVENT_TYPE_GSENSOR_TRIGGER);
		Info->DataBlockSerial = DataTable->DataBlockSerial;
		memcpy(Info->EventTime,DataTable->DataBlockTimeStamp,6);
		KeyStroke = 0;
		if(!OverGSensor)
			BuzzerStatus = BUZZER_STATUS_GSENSOR;

		if(OverGSensor == 0)
		{
			OverGSensor = time(0);
			backMarkList = 1;
		}
		else
		{
			if(time(0) -OverGSensor >= markTimes)
			{
				OverGSensor = 0;
				DDPRINTF("[%s] OverGSensor end....\n", __func__);
			}
		}
	}

	DataTable->CheckSum = calc_checksum( (unsigned long *)DataTable, (DATA_TABLE_SIZE-4)/4);

	*DataTable2 = *DataTable;

	//fill EventMask
	EventPtr = &EventMask[0][0];
	*(EventPtr+EventCounter) = DataTable->FlagMask;

	EventCounter++;
	if (EventCounter >=128)
		EventCounter = 0;

	return DataTable->FlagMask;
}


int uml_level = 0;
volatile time_t umTime = 0;

int keep_time()
{
	int retval;
	struct rtc_time tm_rtc;

	//errno = 0;
	//fd_rtc = open(PATH_RTC, O_RDWR);
	if(fd_rtc < 0)
	{
		DDPRINTF("[%s] '%s' not open\n", __func__, PATH_RTC);
		return -1;
	}

	errno = 0;
	retval = ioctl(fd_rtc, RTC_RD_TIME, &tm_rtc);
	if(retval != 0)
	{
		DDPRINTF("[%s] ioctl RTC_RD_TIME fail because '%s'\n", __func__, strerror(errno));
		return -2;
	}

	struct stat st;
	int fd, ret;
	ret = stat(TIME_FILE, &st);
	errno = 0;
	if(ret)
		fd = open(TIME_FILE, O_CREAT | O_TRUNC | O_RDWR, 0666);
	else
		fd = open(TIME_FILE, O_TRUNC | O_RDWR);

	if(fd > 0)
	{
		errno = 0;
		size_t ws = write(fd, &tm_rtc, sizeof(struct rtc_time));
		if(ws != sizeof(struct rtc_time))
		{
			DDPRINTF("[%s] write file time fail because '%s'\n", __func__, strerror(errno));
			close(fd);
			return -3;
		}
		close(fd);
	}
	else
	{
		DDPRINTF("[%s] file '%s' open fail because '%s'\n", __func__, TIME_FILE, strerror(errno));
		return -4;
	}

	return 0;
}



int WriteDataBlockToDisk(CAR_RECORDER_T *CarRecorder, int SectionsToWrite)
{
	char *ptr;
	int ret;

if(SectionsToWrite == 0)
	return 1;

	VBUS_CHECK();

	//off64_t off = CarRecorder->DataBlockWriteBase +CarRecorder->DataBlockWriteOffset;
	off64_t off = CarRecorder->DataBlockWriteBase +(CarRecorder->IndexSectorWriteOffset/INDEX_SECTOR_SIZE)*DATA_MACRO_BLOCK +CarRecorder->xx*DATA_BLOCK_SIZE;

//DDPRINTF("\033[32m[%s.%d]\033[m xx=%lld, off=%llX\n", __func__, __LINE__, CarRecorder->xx, off);

	if (lseek64(CarRecorder->StorageDescriptor, off, SEEK_SET) < 0)
	{
		DDPRINTF("[%s] seek %llx fail.\n", __func__, off);
	}

	ptr = CarRecorder->Buffer[0];
	//off = CarRecorder->DataBlockWriteOffset;
	//while (off >= DATA_BLOCK_SIZE*MAX_DATA_BLOCK)
	//	off -= DATA_BLOCK_SIZE*MAX_DATA_BLOCK;
	off64_t off2 = (CarRecorder->xx %MAX_DATA_BLOCK) *DATA_BLOCK_SIZE;

struct timeval tm;
gettimeofday(&tm, NULL);

	errno = 0;
	if ( (ret=write(CarRecorder->StorageDescriptor, &ptr[off2], DATA_BLOCK_SIZE*SectionsToWrite)) != DATA_BLOCK_SIZE*SectionsToWrite)
	{
		DDPRINTF("\033[31m[%s]\033[m write fail because '%s', offset = %llx, expect %d bytes, but write %d bytes.\n", __func__, strerror(errno), off2, DATA_BLOCK_SIZE*SectionsToWrite, ret);
	}
	else
	{
		keep_time();
		CarRecorder->xx += SectionsToWrite;
	}

	sync();

//debug message
{
	struct timeval tm2;
	gettimeofday(&tm2, NULL);
	if(tm2.tv_usec < tm.tv_usec)
	{
		tm2.tv_sec -= 1;
		tm2.tv_usec += (1000*1000);
	}
	time_t s = tm2.tv_sec -tm.tv_sec;
	suseconds_t us = tm2.tv_usec -tm.tv_usec;
	//off = CarRecorder->DataBlockWriteBase +CarRecorder->DataBlockWriteOffset;
	DDPRINTF("\033[1;36m[%s] write %8d bytes at 0x%08llX span %d.%06d second(s)\033[m\n", __func__, DATA_BLOCK_SIZE*SectionsToWrite, off+OFFSET_DATA_BLOCK, (int)s, (int)us);
}

	return 1;
}


int WriteOneDataBlock(CAR_RECORDER_T *CarRecorder)
{
	int index, counter, FlagWriteHD=0, SectionsToWrite=0;
	int ret = RET_NO_EVENT_HAPPEN;

	for (index=0; index<(DATA_MACRO_BLOCK/(DATA_BLOCK_SIZE*MAX_DATA_BLOCK)); index++)
	{
		CarRecorder->DataBlock = 0;
		for (counter=0; counter<MAX_DATA_BLOCK; counter++)
		{
			switch (GetDataBlockFromReadBuffer(CarRecorder))
			{
				case MORE_THAN_DATA_BLCOKS:
					FlagWriteHD = 0;
					break;

				case LESS_THAN_DATA_BLCOKS:
					FlagWriteHD = 1;
					break;

				default :
					while (1)
					{
						printf("strange error !!\n");
						sleep(2);
					}
			}

			SectionsToWrite++;
			// fill DataTable & modified 'CarRecorder->DataTable->FlagMask'
			if(FillDataTable(CarRecorder) == EVENT_TYPE_GSENSOR_TRIGGER)
			{
				ret = RET_MASK_EVENT_HAPPEN;
				umTime = time(0);
			}

			//FillIndexTable
			if ( (index==0) && (counter==0) )
			{
				CarRecorder->IndexSector.IndexTable.DataBlockSerial = CarRecorder->DataTable.DataBlockSerial;
				memcpy(&CarRecorder->IndexSector.IndexTable.DataBlockTimeStamp[0], &CarRecorder->DataTable.DataBlockTimeStamp[0], 6);
				CarRecorder->IndexSector.IndexTable.FlagRecordStart = CarRecorder->DataTable.FlagRecordStart;
			}
			if ( (index==((DATA_MACRO_BLOCK/(DATA_BLOCK_SIZE*MAX_DATA_BLOCK))-1)) && (counter==(MAX_DATA_BLOCK-1)) )
			{
				CarRecorder->IndexSector.IndexTable.DataBlockEndSerial = CarRecorder->DataTable.DataBlockSerial;
				memcpy(&CarRecorder->IndexSector.IndexTable.DataBlockEndTimeStamp[0], &CarRecorder->DataTable.DataBlockTimeStamp[0], 6);
			}

			if (FlagWriteHD)
			{
				FlagWriteHD = 0;
				WriteDataBlockToDisk(CarRecorder, SectionsToWrite);
				//CarRecorder->DataBlockWriteOffset += (DATA_BLOCK_SIZE*SectionsToWrite);
				SectionsToWrite = 0;
			}
			CarRecorder->DataBlock++;
		}
		//�@���g�J 1MB
		WriteDataBlockToDisk(CarRecorder, SectionsToWrite);
		//CarRecorder->DataBlockWriteOffset += (DATA_BLOCK_SIZE*SectionsToWrite);
		SectionsToWrite = 0;
	}
	return ret;
}


#if 0 //ivan, tmp remove
int GetKeyStroke(int fd)
{
	int x;
	
	x=ioctl(fd,GPIOA_CTL_IN,0x0000001b);	
	//x=ioctl(fd,GPIOA_CTL_IN,0x0000001e);	
	printf("gpio31=%d\n",x);
	if (x==0)
	{
		int cmd;
		if (active==0)
		{
			active=3;
			printf("3L,4H,5H\n");
			cmd = LED_RED; LED_ON(cmd);
			cmd = LED_GREEN; LED_OFF(cmd);
			cmd = LED_BLUE; LED_OFF(cmd);
		}	
		else if (active==3)
		{
			active=4;
			printf("3H,4L,5H\n");
			cmd = LED_RED; LED_OFF(cmd);
			cmd = LED_GREEN; LED_ON(cmd);
			cmd = LED_BLUE; LED_OFF(cmd);
		}	
		else if (active==4)
		{
			active=5;
			printf("3H,4H,5L\n");
			cmd = LED_RED; LED_OFF(cmd);
			cmd = LED_GREEN; LED_OFF(cmd);
			cmd = LED_BLUE; LED_ON(cmd);
		}	
		else
		{
			active=3;
			printf("3L,4H,5H\n");
			cmd = LED_RED; LED_ON(cmd);
			cmd = LED_GREEN; LED_OFF(cmd);
			cmd = LED_BLUE; LED_OFF(cmd);
		}	
	}

	sleep(1);
	return 1;	
}

void *thread_keystroke(void *gpio_pointer)
{
	int fd;
	
	fd=(int)gpio_pointer;
	
	while (1)
	{
		GetKeyStroke(fd);
		usleep(100000);
	}
}
#endif


int GetEvent(void)
{
	int event;
	
	//gps_init = 2;
	event=getchar();
	printf("key pressed is %c\n",event);
	return event;
}

void *thread_event(void *nothing)
{
	int x;
	
	while (1)
	{
		x = GetEvent();
		switch(x)
		{
			case 's':
			case 'g':
				KeyStroke = x;
				break;
			case 't':
				printf("update date\n");
				API_System_Date((char *)"030302022010.00");
				break;
			case 'p':
				system("top -b -n1");
				break;
		}
	}
	return 0;
}

/*
void *thread_simulate(void *nothing)
{
	srandom(time(0));
	while (1)
	{
		long t = random();
		if( (t < 100) || (t > 1200) )
			continue;
		sleep(t);
		if(t&0x01)
			KeyStroke='g';
		else
			KeyStroke='s';
	}
}
*/


int UpdateMarkList(CAR_RECORDER_T *CarRecorder, INDEX_SECTOR_T *ist, int secs, int disk)
{
	uml_level++;

	unsigned char *t = ist->IndexTable.DataBlockEndTimeStamp;
	struct tm tm;
	tm.tm_year = t[0] +100;
	tm.tm_mon = t[1] -1;
	tm.tm_mday = t[2];
	tm.tm_hour = t[3];
	tm.tm_min = t[4];
	tm.tm_sec = t[5];
	time_t etime = mktime(&tm);
	t = ist->IndexTable.DataBlockTimeStamp;
	tm.tm_year = t[0] +100;
	tm.tm_mon = t[1] -1;
	tm.tm_mday = t[2];
	tm.tm_hour = t[3];
	tm.tm_min = t[4];
	tm.tm_sec = t[5];
	time_t stime = mktime(&tm);

DDPRINTF("\033[1;31m[%s]\033[m level=%d, block time= %d ~ %d, umTime=%d, ist->prev_off=0x%08x, ist=%p\n",
	__func__, uml_level, (int)stime, (int)etime, (int)umTime, ist->prev_off, ist);

	if(etime <= (umTime -markTimes) || stime > umTime)
	{
		uml_level--;
		return 1;
	}

//------ ModifyMarkList() here!-----------------------
	ist->IndexTable.EventInfoCounter = 0;
	EVENT_INFO_T *eip = ist->IndexTable.EventInfo;

	//----- update SubDataBlock -----
	off64_t off = CarRecorder->DataBlockWriteBase +(16*1024*1024)*(ist->self_off/sizeof(INDEX_SECTOR_T));
	off += (DATA_BLOCK_SIZE -DATA_TABLE_SIZE);
	unsigned char *tt;
	struct tm tmm;
	time_t tetime;
	int i, j, flag;
	DATA_TABLE_T *dtt = (DATA_TABLE_T *)malloc(DATA_TABLE_SIZE);

DDPRINTF("    [%s] start to check sub_block, umTime=%u, markTimes=%d\n", __func__, (int)umTime, markTimes);

	//for(i=0; i<DATA_MACRO_BLOCK/DATA_BLOCK_SIZE; i++) //i == 0 ~ 127
	for(i=0; i<16; i++)
	{
		flag = 0;
		for(j=0; j<8; j++)
		{
			errno = 0;
			if(lseek64(CarRecorder->StorageDescriptor, off, SEEK_SET) < 0)
			{
				DDPRINTF("[%s] seek 0x%llx fail because '%s'\n", __func__, off, strerror(errno));
				uml_level--;
				return (-1);
			}
			errno = 0;
			if(read(CarRecorder->StorageDescriptor, dtt, DATA_TABLE_SIZE) != DATA_TABLE_SIZE)
			{
				DDPRINTF("[%s] read 0x%llx fail because '%s'\n", __func__, off, strerror(errno));
				uml_level--;
				return (-1);
			}
			tt = dtt->DataBlockTimeStamp;
			tmm.tm_year = tt[0] +100;
			tmm.tm_mon = tt[1] -1;
			tmm.tm_mday = tt[2];
			tmm.tm_hour = tt[3];
			tmm.tm_min = tt[4];
			tmm.tm_sec = tt[5];
			tetime = mktime(&tmm);

			if(tetime > umTime)
				break;

			if( (tetime >= (umTime -markTimes)) && (tetime <= (umTime +markTimes)) )
			{
				dtt->FlagMask = EVENT_TYPE_GSENSOR_TRIGGER;
				dtt->CheckSum = calc_checksum( (unsigned long *)dtt, (DATA_TABLE_SIZE -4)/4);
				errno = 0;
				if(lseek64(CarRecorder->StorageDescriptor, off, SEEK_SET) < 0)
				{
					DDPRINTF("[%s] seek 0x%llx fail because '%s'\n", __func__, off, strerror(errno));
					uml_level--;
					return (-1);
				}
				errno = 0;
				if(write(CarRecorder->StorageDescriptor, dtt, DATA_TABLE_SIZE) != DATA_TABLE_SIZE)
				{
					DDPRINTF("[%s] write 0x%llx fail because '%s'\n", __func__, off, strerror(errno));
					uml_level--;
					return (-1);
				}
				if(flag == 0)
				{
					eip->DataBlockSerial = dtt->DataBlockSerial;
					memcpy(eip->EventTime, dtt->DataBlockTimeStamp, 6);
					eip->EventType = ENDIAN_SHORT(dtt->FlagMask);
					ist->IndexTable.EventInfoCounter++;
					flag = 1;
				}
			}
			off += DATA_BLOCK_SIZE;
		}
		eip++;
	}
	free(dtt);
	ist->IndexTable.FlagMask = EVENT_TYPE_GSENSOR_TRIGGER;
	ist->Checksum = calc_checksum( (unsigned long *)ist, (INDEX_SECTOR_SIZE -4)/4);

	for(i=0; i<2; i++)
	{
		off = CarRecorder->IndexSectorWriteBase[i] +ist->self_off;

	DDPRINTF("    [%s] start to write index table, off=0x%08llx, eic=%d, ist->prev_off=0x%08x, ist=%p\n",
		__func__, off, ist->IndexTable.EventInfoCounter, ist->prev_off, ist);

		errno = 0;
		if(lseek64(CarRecorder->StorageDescriptor, off, SEEK_SET) < 0)
		{
			DDPRINTF("[%s] seek 0x%llx fail because '%s'\n", __func__, off, strerror(errno));
			uml_level--;
			return (-1);
		}
		errno = 0;
		if(write(CarRecorder->StorageDescriptor, ist, INDEX_SECTOR_SIZE) != INDEX_SECTOR_SIZE)
		{
			DDPRINTF("[%s] write 0x%llx fail because '%s'\n", __func__, off, strerror(errno));
			uml_level--;
			return (-1);
		}
	}

//------------------------------------------------

	if(ist->self_off != ist->prev_off)
	{
		int idx = ist->prev_off / sizeof(INDEX_SECTOR_T);
		UpdateMarkList(CarRecorder, &IndexBuffer[disk][idx], secs, disk);
	}

	uml_level--;
	return 0;
}




int cr_ready = 0;
int record_playing = 1;

void *ThreadCarRecord(void *gpio_pointer)
{
	int ret;
	DUAL_USB_T *DualUSB;
	CAR_RECORDER_T *CarRecorder;
	pthread_t thread_gps_id, thread_event_id;//, thread_simulate_id;
//	int thread_keystroke_id;
	int looping=0;
	int	FlagMarkThreshold=MARK_THRESHOLD_UNDER;
	int	fd;

	fd = (int)gpio_pointer;
	DualUSB	= &GlobalDualUSB;
	memset((char *)DualUSB, 0, sizeof(GlobalDualUSB));
	DualUSB->CarRecorder[0].num = 0;
	DualUSB->CarRecorder[1].num = 1;

	pthread_mutex_init(&gps_mutex,NULL);
	pthread_create(&thread_gps_id, NULL, thread_gps, (void *)fd);
	pthread_create(&thread_event_id, NULL, thread_event, (void *)0);
	//pthread_create(&thread_keystroke_id, NULL, thread_keystroke, (void *)fd);
	//pthread_create(&thread_simulate_id, NULL, thread_simulate, (void *)0);

	GetUSBStoragePath(DualUSB);
	VBUS_CHECK();
	DDPRINTF("[%s] USBStoragePath=%s,%s\n", __func__, DualUSB->CarRecorder[0].StoragePath, DualUSB->CarRecorder[1].StoragePath);

	int ue = GenerateUSBDiskFile(DualUSB);
	VBUS_CHECK();
	DDPRINTF("[%s] USB DiskCount=%d\n", __func__, DualUSB->USBDiskCount);

	if (ue ==0)
	{
		fprintf(stderr, "\033[1;31m[%s] --ERROR-- NO USB disk !!\033[m\n", __func__);
		fflush(stderr);
		BuzzerStatus = BUZZER_STATUS_NO_USBDISK;
		while (1)
		{
			BuzzerStatus = BUZZER_STATUS_NO_USBDISK;
			sleep(3);
		}
		//API_System_Reboot();
	}
	else
	{
		if(ue & 0x01)
		{
			FixIndexTable(&(DualUSB->CarRecorder[0]));
			FillIndexBuffer(0, &(DualUSB->CarRecorder[0]));
			DualUSB->PartDiskSerial[0] = GetPartDiskSerial(&(DualUSB->CarRecorder[0]));
			printf("[%s] USB 1 serial=0x%08x, IndexTableSerial=0x%08x\n", __func__, DualUSB->PartDiskSerial[0],
				ENDIAN_INT(DualUSB->CarRecorder[0].IndexSectorBuffer.IndexTable.IndexTableSerial) );
		}
		if(ue & 0x02)
		{
			FixIndexTable(&(DualUSB->CarRecorder[1]));
			FillIndexBuffer(0, &(DualUSB->CarRecorder[1]));
			//FillIndexBuffer(1, &(DualUSB->CarRecorder[1]));
			DualUSB->PartDiskSerial[1] = GetPartDiskSerial(&(DualUSB->CarRecorder[1]));
			printf("[%s] USB 2 serial=0x%08x, IndexTableSerial=0x%08x\n", __func__, DualUSB->PartDiskSerial[1],
				ENDIAN_INT(DualUSB->CarRecorder[1].IndexSectorBuffer.IndexTable.IndexTableSerial) );
		}

		if (DualUSB->PartDiskSerial[1] > DualUSB->PartDiskSerial[0])
			DualUSB->ActiveCarRecorder = 1;
		else
			DualUSB->ActiveCarRecorder = 0;
	}

	VBUS_CHECK();


	istc.off = (off64_t)DualUSB->CarRecorder[DualUSB->ActiveCarRecorder].IndexSectorWriteOffset;
	istc.ptr = (INDEX_SECTOR_T *)&IndexBuffer[DualUSB->ActiveCarRecorder][istc.off/sizeof(INDEX_SECTOR_T)];
	istc.ptr->self_off = istc.off;

DDPRINTF("\033[1;31m[%s]\033[m START -- istc.off=0x%08X\n", __func__, (int)istc.off);


	if(access("/root/serial", F_OK))
	{
		//fail
		int fd_serial;
		char buffer[10];
		int sum;

		sum = MIN_DISK_SERIAL;
		fd_serial =open("/root/serial", O_WRONLY | O_CREAT | O_TRUNC, 0666);
		memset(buffer, 0, 10);
		sprintf(buffer, "%d", sum);
		write(fd_serial, buffer, strlen(buffer));
		close(fd_serial);
		DualUSB->DiskSerial = sum;
	}
	else
	{
		int fd_serial;
		char buffer[10];
		int i, sum;
		char data;

		fd_serial = open("/root/serial", O_RDONLY , 0666);
		memset(buffer, 0, 10);

		sum = 0;
		for (i=0; i<10; i++)
		{
			if (read(fd_serial, &data, 1) <= 0)
				break;
			else if ((data > '9') || (data < '0'))
				break;

			sum = sum*10 +data -'0';
		}
		close(fd_serial);

		if (sum < MIN_DISK_SERIAL)
		{
			sum = MIN_DISK_SERIAL;
			fd_serial =open("/root/serial", O_WRONLY | O_CREAT | O_TRUNC, 0666);
			memset(buffer, 0, 10);
			sprintf(buffer, "%d", sum);
			write(fd_serial, buffer, strlen(buffer));
			close(fd_serial);
			sync();
		}
		DualUSB->DiskSerial = sum;
	}
	VBUS_CHECK();
	DDPRINTF("[%s] flash serial = %d\n", __func__, DualUSB->DiskSerial);

	DualUSB->DiskSerial++;	//always ++

	if (DualUSB->DiskSerial > DualUSB->PartDiskSerial[DualUSB->ActiveCarRecorder])
	{
		DualUSB->PartDiskSerial[DualUSB->ActiveCarRecorder] = DualUSB->DiskSerial;
		UpdateDiskSerial(DualUSB, DualUSB->DiskSerial);
		UpdateFlashSerial(DualUSB, DualUSB->DiskSerial);
	}
	else
	{
		DualUSB->DiskSerial = DualUSB->PartDiskSerial[DualUSB->ActiveCarRecorder];
		UpdateFlashSerial(DualUSB,DualUSB->DiskSerial);
	}
	VBUS_CHECK();


DDPRINTF("\033[1;31m[%s]\033[m IndexTableSerial=(0x%08x, 0x%08x)\n", __func__,
	ENDIAN_INT(DualUSB->CarRecorder[0].IndexSectorBuffer.IndexTable.IndexTableSerial),
	ENDIAN_INT(DualUSB->CarRecorder[1].IndexSectorBuffer.IndexTable.IndexTableSerial) );

	cr_ready = 1;

	//while(cr_ready == 0)
	//	usleep(10*1000);

	DDPRINTF("================================\n");
//	BuzzerStatus = BUZZER_STATUS_START_RECORDING;

	while(record_playing == 1)
	{
		FlagMarkThreshold = CheckMarkThreshold(DualUSB->USBDiskCount);
		if (FlagMarkThreshold == MARK_THRESHOLD_STOP)
		{
			while (1)
			{
				printf("stop recording\n");
				LEDStatus = LED_STATUS_STOP_RECORD;
				BuzzerStatus = BUZZER_STATUS_SPACE_FULL;
				sleep(5);
			}
		}
		else if (FlagMarkThreshold == MARK_THRESHOLD_OVER)
		{
			BuzzerStatus = BUZZER_STATUS_SPACE_THRESHOLD;
		}

CHECK_CURRENT_MARK:

		CarRecorder = &DualUSB->CarRecorder[DualUSB->ActiveCarRecorder];

		//switch (CheckCurrentMark(DualUSB->ActiveCarRecorder, &(DualUSB->CarRecorder[DualUSB->ActiveCarRecorder]) ) )
		switch(CheckCurrentMark(DualUSB->ActiveCarRecorder, CarRecorder) )
		{
			case INDEX_NO_MARK:
				//printf("no mark,offset=%x\n",CarRecorder->IndexSectorWriteOffset);
			break;

			case INDEX_MARK:
				DDPRINTF("[%s] IndexTable %d be marked\n", __func__,
					(int)(CarRecorder->IndexSectorWriteOffset/INDEX_SECTOR_SIZE) );
				//CarRecorder->DataBlockWriteOffset 	+= DATA_MACRO_BLOCK;
				CarRecorder->IndexSectorWriteOffset	+= INDEX_SECTOR_SIZE;

#if defined(AUTO_SPACE)
			#if 0 //ivan, 20101029
				if (CarRecorder->DataBlockWriteOffset >= LenDataBlock[CarRecorder->num])
					CarRecorder->DataBlockWriteOffset -= LenDataBlock[CarRecorder->num];
			#else
				//if(CarRecorder->DataBlockWriteOffset >= DiskDataBlocks[CarRecorder->num]*DATA_MACRO_BLOCK)
				//	CarRecorder->DataBlockWriteOffset = 0;
			#endif

				if(CarRecorder->IndexSectorWriteOffset >= (DiskDataBlocks[CarRecorder->num]*INDEX_SECTOR_SIZE))
				{
					//printf("change\n");
					looping++;
				#if 0 //ivan, 20101029
					CarRecorder->IndexSectorWriteOffset -= (DiskDataBlocks[CarRecorder->num]*INDEX_SECTOR_SIZE);
				#else
					CarRecorder->IndexSectorWriteOffset = 0;
				#endif
					if (DualUSB->USBDiskCount == 2)
					{
						if (DualUSB->ActiveCarRecorder)
							DualUSB->ActiveCarRecorder=0;
						else
							DualUSB->ActiveCarRecorder=1;
					}
					CarRecorder = &DualUSB->CarRecorder[DualUSB->ActiveCarRecorder];
					CarRecorder->FlagRecordStart[0] = CarRecorder->FlagRecordStart[1] = 1;
					DualUSB->DiskSerial++;
					UpdateFlashSerial(DualUSB,DualUSB->DiskSerial);
					DualUSB->PartDiskSerial[DualUSB->ActiveCarRecorder] = DualUSB->DiskSerial;
					VBUS_CHECK();
					UpdateDiskSerial(DualUSB,DualUSB->DiskSerial);
					sync();
				}
#else //---------------------------------------------------------------------------
				if (CarRecorder->DataBlockWriteOffset >= LEN_DATA_BLOCK)	//6G
					CarRecorder->DataBlockWriteOffset -= LEN_DATA_BLOCK;
				if (CarRecorder->IndexSectorWriteOffset >= (DISK_DATA_BLOCKS*INDEX_SECTOR_SIZE))
				{
					//printf("change\n");
					looping++;
					CarRecorder->IndexSectorWriteOffset -= (DISK_DATA_BLOCKS*INDEX_SECTOR_SIZE);
					if (DualUSB->USBDiskCount ==2)
					{
						if (DualUSB->ActiveCarRecorder)
							DualUSB->ActiveCarRecorder=0;
						else
							DualUSB->ActiveCarRecorder=1;
					}
					CarRecorder = &DualUSB->CarRecorder[DualUSB->ActiveCarRecorder];
					CarRecorder->FlagRecordStart[0] = CarRecorder->FlagRecordStart[1] = 1;
					DualUSB->DiskSerial++;
					UpdateFlashSerial(DualUSB,DualUSB->DiskSerial);
					DualUSB->PartDiskSerial[DualUSB->ActiveCarRecorder] = DualUSB->DiskSerial;
					VBUS_CHECK();
					UpdateDiskSerial(DualUSB,DualUSB->DiskSerial);
					sync();
				}
#endif //---------------------------------------------------------------------------
				goto CHECK_CURRENT_MARK;
			break;

			default:
				printf("STRANGE!!!!\n");
			break;
		}

		LEDStatus = LED_STATUS_START_RECORD;

		istc.prev = istc.ptr;
		istc.prev_off = istc.off;
		istc.off = (off64_t)CarRecorder->IndexSectorWriteOffset;
		istc.ptr = &IndexBuffer[DualUSB->ActiveCarRecorder][istc.off/sizeof(INDEX_SECTOR_T)];
		istc.ptr->self_off = (int)istc.off;
		istc.ptr->prev_off = (int)istc.prev_off;
		istc.prev->next_off = (int)istc.off;

		ret = WriteOneDataBlock(CarRecorder);
		WriteOneIndexSector(CarRecorder, FlagMarkThreshold, DualUSB->ActiveCarRecorder);
		if (ret == RET_MASK_EVENT_HAPPEN)
		{
			if(backMarkList)
			{
				UpdateMarkList(CarRecorder, istc.ptr, markTimes, DualUSB->ActiveCarRecorder);
				backMarkList = 0;
			}
			umTime = 0;
		}

#if defined(AUTO_SPACE)
		//if (CarRecorder->DataBlockWriteOffset >= LenDataBlock[CarRecorder->num])
		//	CarRecorder->DataBlockWriteOffset -= LenDataBlock[CarRecorder->num];
		if(CarRecorder->IndexSectorWriteOffset >= (DiskDataBlocks[CarRecorder->num]*INDEX_SECTOR_SIZE))
		{
			//printf("change\n");
			looping++;
			CarRecorder->IndexSectorWriteOffset -= (DiskDataBlocks[CarRecorder->num]*INDEX_SECTOR_SIZE);
			if (DualUSB->USBDiskCount ==2)
			{
				if (DualUSB->ActiveCarRecorder)
					DualUSB->ActiveCarRecorder=0;
				else
					DualUSB->ActiveCarRecorder=1;
			}
			CarRecorder = &DualUSB->CarRecorder[DualUSB->ActiveCarRecorder];
			CarRecorder->FlagRecordStart[0] = CarRecorder->FlagRecordStart[1] = 1;

			DualUSB->DiskSerial++;
			UpdateFlashSerial(DualUSB,DualUSB->DiskSerial);
			DualUSB->PartDiskSerial[DualUSB->ActiveCarRecorder] = DualUSB->DiskSerial;
			VBUS_CHECK();
			UpdateDiskSerial(DualUSB,DualUSB->DiskSerial);
			sync();
		}
#else
		if (CarRecorder->DataBlockWriteOffset >= LEN_DATA_BLOCK)
			CarRecorder->DataBlockWriteOffset -= LEN_DATA_BLOCK;
		if (CarRecorder->IndexSectorWriteOffset >= (DISK_DATA_BLOCKS*INDEX_SECTOR_SIZE))
		{
			//printf("change\n");
			looping++;
			CarRecorder->IndexSectorWriteOffset -= (DISK_DATA_BLOCKS*INDEX_SECTOR_SIZE);
			if (DualUSB->USBDiskCount ==2)
			{
				if (DualUSB->ActiveCarRecorder)
					DualUSB->ActiveCarRecorder=0;
				else
					DualUSB->ActiveCarRecorder=1;
			}
			CarRecorder = &DualUSB->CarRecorder[DualUSB->ActiveCarRecorder];
			CarRecorder->FlagRecordStart[0] = CarRecorder->FlagRecordStart[1] = 1;

			DualUSB->DiskSerial++;
			UpdateFlashSerial(DualUSB,DualUSB->DiskSerial);
			DualUSB->PartDiskSerial[DualUSB->ActiveCarRecorder] = DualUSB->DiskSerial;
			VBUS_CHECK();
			UpdateDiskSerial(DualUSB,DualUSB->DiskSerial);
			sync();
		}
#endif

	}

	pthread_cancel(thread_gps_id);
	pthread_cancel(thread_event_id);
	//pthread_cancel(thread_simulate_id);
	//pthread_cancel(thread_keystroke_id);

	printf("END\n");
	return NULL;
}

/*
void cli_re_init()
{
	int i;

	bzero((char *)rc, sizeof(struct RTSPClient)*CLIENTS);
	for(i=0; i<CLIENTS; i++) // 2 clients for now
	{
		rc[i].cseq = 1;
		rc[i].c_ip = self_ip;
		rc[i].RTSPport = 554;
		rc[i].num = i;
		rc[i].first_time = 1;
		srandom((i+11)*7 +(i+5)*3);
		rc[i].mySSRC[0] = random();
		srandom(rc[i].mySSRC[0]);
		rc[i].mySSRC[1] = random();
		rc[i].b = &ebuf[i];
		pthread_mutex_destroy(&rc[i].r_mutex);
		pthread_mutex_init(&rc[i].r_mutex, NULL);
		pthread_mutex_destroy(&rc[i].read_mutex);
		pthread_mutex_init(&rc[i].read_mutex, NULL);
		pthread_mutex_destroy(&rc[i].data_mutex);
		pthread_mutex_init(&rc[i].data_mutex, NULL);
		pthread_mutex_destroy(&rc[i].av_mutex);
		pthread_mutex_init(&rc[i].av_mutex, NULL);
		pthread_cond_destroy(&rc[i].noData);
		pthread_cond_init(&rc[i].noData, NULL);
		pthread_cond_destroy(&rc[i].avSync);
		pthread_cond_init(&rc[i].avSync, NULL);
	}

	pthread_mutex_destroy(&remain_mutex);
	pthread_mutex_init(&remain_mutex, NULL);
	pthread_mutex_destroy(&clients_mutex);
	pthread_mutex_init(&clients_mutex, NULL);

	memset(&ebuf[0], 0, sizeof(struct EBuf) *2);
}
*/

void sigusr1_handler(int signum)
{
	unsigned int data;
	int size;

	static struct timeval tm;

	//printf("receive a signal = %d\n", signum);
	lseek(fd_gpio, OFFSET_BUTTON, SEEK_SET);

	size=read(fd_gpio, &data, 4);
	//printf("receive data = 0x%08X, size=%d, error code=%d\n", data, size, errno);
	switch(data & MASK_BUTTON)
	{
		case BUTTON_PRESS:
			gettimeofday(&tm, NULL);
			printf("button port=%X  num=%X  press\n", GPIO_GET_PORT(data), GPIO_PIN(data));
			break;
		case BUTTON_PRESS_LONG:
			printf("button port=%X  num=%X  long press\n", GPIO_GET_PORT(data), GPIO_PIN(data));
			break;
		case BUTTON_RELEASE:
		{
			struct timeval tm2;
			time_t s;
			suseconds_t us;
			gettimeofday(&tm2, NULL);
			if(tm.tv_usec > tm2.tv_usec)
			{
				tm2.tv_usec += 1000*1000;
				tm2.tv_sec--;
			}
			s = tm2.tv_sec -tm.tv_sec;
			us = tm2.tv_usec -tm.tv_usec;
			printf("button port=%X  num=%X  relase, pass time is %d.%06d second\n", GPIO_GET_PORT(data), GPIO_PIN(data), (int)s, (int)us);
			if(forceRecord==1 || recordPause==1)
			{
				forceRecord = recordPause = 0;
				DDPRINTF("[%s] bact to normal record\n", __func__);
			}
			else if(s < 1)
			{
				if(canPauseRecord)
				{
					recordPause = 1;
					DDPRINTF("[%s] record STOP\n", __func__);
				}
			}
			else if(s >= 1 && s < 5)
			{
				if(canForceRecord)
				{
					forceRecord = 1;
					DDPRINTF("[%s] start 'force record'\n", __func__);
				}
			}
			else if(s >= 5)
			{
			}
		}
			break;
		default:
			printf("unknow button port=%X  num=%X action\n", GPIO_GET_PORT(data), GPIO_PIN(data));
			break;
	}
}



unsigned short icmp_chksum(unsigned short *addr, int len)
{
	int nleft=len;
	int sum=0;
	unsigned short *w=addr;
	unsigned short answer=0;

	while(nleft > 1)
	{
		sum += *w++;
		nleft -= 2;
	}

	if( nleft == 1)
	{       
		*(unsigned char *)(&answer) = *(unsigned char *)w;
		sum += answer;
	}

	sum = (sum >> 16) + (sum & 0xffff);
	sum += (sum >> 16);
	answer = ~sum;

	return answer;
}

int ping_target(int timeout, char *ips)
{

#define MY_PACKET_SIZE	4096

	struct timeval timeo;
	int sockfd;
	struct sockaddr_in addr;
	struct sockaddr_in from;

	struct timeval *tval;
	struct ip *iph;
	struct icmp *icmp;

	char sendpacket[MY_PACKET_SIZE];
	char recvpacket[MY_PACKET_SIZE];

	int n;
	pid_t pid;
	int maxfds = 0;
	fd_set readfds;

	bzero(&addr,sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr(ips);   

	sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
	if (sockfd < 0)
	{
		printf("ip:%s,socket error\n",ips);
		return -1;
	}

	// TimeOut
	timeo.tv_sec = timeout / 1000;
	timeo.tv_usec = timeout % 1000;

	if (setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeo, sizeof(timeo)) == -1)
	{
		printf("ip:%s,setsockopt error\n",ips);
		close(sockfd);
		return -1;
	}

	memset(sendpacket, 0, sizeof(sendpacket));

	pid=getpid();
	int packsize;
	icmp=(struct icmp*)sendpacket;
	icmp->icmp_type=ICMP_ECHO;
	icmp->icmp_code=0;
	icmp->icmp_cksum=0;
	icmp->icmp_seq=0;
	icmp->icmp_id=pid;
	packsize=8+56;
	tval= (struct timeval *)icmp->icmp_data;
	gettimeofday(tval,NULL);
	icmp->icmp_cksum=icmp_chksum((unsigned short *)icmp,packsize);

	n = sendto(sockfd, (char *)&sendpacket, packsize, 0, (struct sockaddr *)&addr, sizeof(addr));
	if (n < 1)
	{
		//syslog(LOG_INFO,"ip:%s,sendto error",ips);
		close(sockfd);
		return -1;
	}

	for(;;)
	{
		FD_ZERO(&readfds);
		FD_SET(sockfd, &readfds);
		maxfds = sockfd + 1;
		n = select(maxfds, &readfds, NULL, NULL, &timeo);
		if (n <= 0)
		{
			//syslog(LOG_INFO,"ip:%s,Time out error",ips);
			close(sockfd);
			return -1;
		}

		memset(recvpacket, 0, sizeof(recvpacket));
		int fromlen = sizeof(from);
		n = recvfrom(sockfd, recvpacket, sizeof(recvpacket), 0, (struct sockaddr *)&from,(socklen_t*) &fromlen);
		if (n < 1)
		{
			break;
		}

		char *from_ip = (char *)inet_ntoa(from.sin_addr);
		if (strcmp(from_ip,ips) != 0)
		{
			printf("ip:%s,Ip different\n",ips);
			close(sockfd);
			return -1;

		}

		iph = (struct ip *)recvpacket;
		icmp=(struct icmp *)(recvpacket + (iph->ip_hl<<2));
		//DDPRINTF("ip:%s, icmp->icmp_type:%d, icmp->icmp_id:%d\n", ips, icmp->icmp_type, icmp->icmp_id);
		if (icmp->icmp_type == ICMP_ECHOREPLY && icmp->icmp_id == pid)
		{
			break;
		}
		else
		{
			continue;
		}
	}

	close(sockfd);

	return 0;
}


int sys_time()
{
	int retval;

	struct rtc_time tm_rtc;
	struct tm *tm_p;
	time_t tv_sys;

	errno = 0;
	fd_rtc = open(PATH_RTC, O_RDWR);
	if(fd_rtc < 0)
	{
		DDPRINTF("[%s] '%s' open fail because '%s'\n", __func__, PATH_RTC, strerror(errno));
		return -1;
	}

	errno = 0;
	retval = ioctl(fd_rtc, RTC_RD_TIME, &tm_rtc);
	if(retval != 0)
	{
		DDPRINTF("[%s] ioctl RTC_RD_TIME fail because '%s'\n", __func__, strerror(errno));
		return -2;
	}

	if (tm_rtc.tm_year < 100)
	{
		int ok = 0;
		struct stat st;
		if(!stat(TIME_FILE, &st) && (st.st_size > 0) )
		{
			int fd = open(TIME_FILE, O_RDONLY);
			if(fd > 0)
			{
				size_t rs = read(fd, &tm_rtc, sizeof(struct rtc_time));
				close(fd);
				if(rs == sizeof(struct rtc_time))
				{
					ok = 1;
					tm_rtc.tm_min += 2;
					DDPRINTF("[%s] Invalid RTC time, restore last time\n", __func__);
				}
			}
		}
		
		if(!ok)
		{
			tm_rtc.tm_year = 110;
			tm_rtc.tm_mon = 9;
			tm_rtc.tm_mday = 10;
			tm_rtc.tm_hour = 0;
			tm_rtc.tm_min = 0;
			tm_rtc.tm_sec = 0;
			DDPRINTF("[%s] Invalid RTC time, set to 2010.10.10 00:00:00\n", __func__);
		}

		errno = 0;
		retval = ioctl(fd_rtc, RTC_SET_TIME, &tm_rtc);
		if(retval != 0)
		{
			DDPRINTF("[%s] ioctl RTC_SET_TIME fail because '%s'\n", __func__, strerror(errno));
		}
	}

	DDPRINTF("\033[32m[%s] current system time -- %04d/%02d/%02d %02d:%02d:%02d\033[m\n",
		__func__, tm_rtc.tm_year+1900, tm_rtc.tm_mon+1, tm_rtc.tm_mday,
		tm_rtc.tm_hour, tm_rtc.tm_min, tm_rtc.tm_sec);


	tm_p = (struct tm *) &tm_rtc;
	tv_sys = mktime((struct tm *) &tm_rtc);

	errno = 0;
	retval = stime(&tv_sys);
	if(retval != 0 )
	{
		DDPRINTF("[%s] set system time fail because '%s'\n", __func__, strerror(errno));
	}

	return 0;
}

int main(int argc, char *argv[])
{
	pthread_t IDThreadCarRecord;
	pthread_t IDThreadLEDControl;
	pthread_t IDThreadBuzzerControl;


	DDPRINTF("\n\033[1;36mGDR TEST AP v0.1\033[m (%s %s)\n\n", __DATE__, __TIME__);
	
	if( (fd_gpio = open(GPIO_DEVICE, O_RDWR)) < 0)
	{
		DDPRINTF("\033[1;31m gpio open fail.\033[m\n");
	}

	int i;
	char *ip, *tail;
	char _ip[]="192.168.100.100";

	char _tail[CLIENTS][64]={"live0-1.sdp"};
	int _clients;

	if(argc > 1)
	{
		ip = argv[1];
	}
	else
	{
		ip = _ip;
	}
	tail = _tail[0];
	
	
//BUZZER test

	printf("\n======= Buzzer test ========\n");
	
	int fd_pwm;
	errno = 0;
	fd_pwm=open(SQ_PWM, O_RDWR);
	if(fd_pwm <= 0){
		printf("\033[1;31m[%s]\033[m pwm_node open fail because '%s'\n", __func__, strerror(errno));
		printf("\n===== Buzzer test end ======\n");
	}
	else{
		errno = 0;
		if(ioctl(fd_pwm, SQ_PWM_RESET) < 0){
			printf("\033[1;31m[%s]\033[m pwm io error because '%s\n", __func__, strerror(errno));
			close(fd_pwm);
			printf("\n===== Buzzer test end ======\n");
		}
		else{
			printf("\n\033[1;31m The buzzer rings\033[m\n");
			BUZZER_ON;
			usleep(500*1000);
			BUZZER_OFF;
			usleep(500*1000);
			BUZZER_ON;
			usleep(500*1000);
			BUZZER_OFF;
			usleep(500*1000);
			BUZZER_ON;
			usleep(500*1000);
			BUZZER_OFF;
			usleep(500*1000);
			close(fd_pwm);
			printf("\n===== Buzzer test end ======\n");
		}
	}

//BUZZER test end

//LED test

	printf("\n========= LED test =========\n");
	printf("\n\033[1;31m All LED flash\033[m\n");
	int ledlight;
	ledlight = LED_RED; LED_ON(ledlight);
	ledlight = LED_GREEN; LED_ON(ledlight);
	ledlight = LED_BLUE; LED_ON(ledlight);
	usleep(500*1000);
	ledlight = LED_RED; LED_OFF(ledlight);
	ledlight = LED_GREEN; LED_OFF(ledlight);
	ledlight = LED_BLUE; LED_OFF(ledlight);
	usleep(500*1000);
	ledlight = LED_RED; LED_ON(ledlight);
	ledlight = LED_GREEN; LED_ON(ledlight);
	ledlight = LED_BLUE; LED_ON(ledlight);
	usleep(500*1000);
	ledlight = LED_RED; LED_OFF(ledlight);
	ledlight = LED_GREEN; LED_OFF(ledlight);
	ledlight = LED_BLUE; LED_OFF(ledlight);
	usleep(500*1000);
	ledlight = LED_RED; LED_ON(ledlight);
	ledlight = LED_GREEN; LED_ON(ledlight);
	ledlight = LED_BLUE; LED_ON(ledlight);
	usleep(500*1000);
	ledlight = LED_RED; LED_OFF(ledlight);
	ledlight = LED_GREEN; LED_OFF(ledlight);
	ledlight = LED_BLUE; LED_OFF(ledlight);
	usleep(500*1000);
	printf("\n======= LED test end =======\n");

//LED test end

//VIP-VOP test

	printf("\n======= VIP-VOP test =======\n");
	printf("\n\033[1;31m please wait 5 seconds and watch TV.\033[m\n");
	system("./vip -v adv7391_init -t 5 -s 1");
	printf("\n===== VIP-VOP test end =====\n");
	
//VIP-VOP test end

//usb test

	int usb1fail = 0,usb2fail = 0;
	int fd_usb,writewords,readwords,mountusb;
	char usbstr[]="write_data_to_usb!",usbbuffer[sizeof(usbstr)];

	printf("\n========= usb test =========\n");
	//usb_3rd_port
	if(access("/dev/usb_3rd_port",F_OK) != 0){
		printf("\n\033[1;31m usb device : /dev/usb_3rd_port not found.\033[m\n");
		printf("\n======= usb test end =======\n");
		return -1;
	}
	else
		mountusb = system("mount /dev/usb_3rd_port1 /mnt");
	
	if(mountusb == 0){
		printf("\ncreate file.\n");
		fd_usb = open("/mnt/usb1.txt", O_CREAT | O_RDWR | O_SYNC, 0666);
		if(fd_usb < 0){
			printf("\nusb create file fail.\n");
			usb1fail = 1;
		}
		else{
			printf("write data %d words : %s\n",sizeof(usbstr),usbstr);
			writewords = write(fd_usb,usbstr,sizeof(usbstr));
			if(writewords < sizeof(usbstr)){
				printf("\nwrite fail : only write %d words!\n",writewords);
				usb1fail = 1;
			}
			close(fd_usb);
	
			printf("\nread file.\n");
			fd_usb = open("/mnt/usb1.txt", O_RDONLY);
			if(fd_usb < 0){
				printf("\nusb open file fail.\n");
				usb1fail = 1;
			}
			else{
				readwords = read(fd_usb,usbbuffer,writewords);
				if(readwords < writewords){
					printf("\nread fail : only read %d words : %s\n",readwords,usbbuffer);
					usb1fail = 1;
				}
				else
					printf("read data %d words : %s\n",readwords,usbbuffer);
				close(fd_usb);
			}
		
			if(unlink("/mnt/usb1.txt") < 0){
				printf("\ndelete data fail!\n");
				usb1fail = 1;
			}
	
			if(strncmp(usbstr,usbbuffer,sizeof(usbstr)) != 0){
				printf("\nwrite & read data not match\n");
				usb1fail = 1;
			}
		}
		system("umount /mnt");
	}
	else
		usb1fail = 1;
	
	if(usb1fail)
		printf("\n\033[1;31m usb_3rd_port test fail!\033[m\n");
	else
		printf("\n\033[1;36m usb_3rd_port test pass!\033[m\n");
	
	
	//usb_4th_port
	if(access("/dev/usb_4th_port",F_OK) != 0){
		printf("\n\033[1;31m usb device : /dev/usb_4th_port not found.\033[m\n");
		usb2fail = 1;
	}
	else{
		mountusb = system("mount /dev/usb_4th_port1 /mnt");
		if(mountusb == 0){
			printf("\ncreate file.\n");
			fd_usb = open("/mnt/usb2.txt", O_CREAT | O_RDWR | O_SYNC, 0666);
			if(fd_usb < 0){
				printf("\nusb create file fail.\n");
				usb2fail = 1;
			}
			else{
				printf("write data %d words : %s\n",sizeof(usbstr),usbstr);
				writewords = 0;
				writewords = write(fd_usb,usbstr,sizeof(usbstr));
				if(writewords < sizeof(usbstr)){
					printf("\nwrite fail : only write %d words!\n",writewords);
					usb2fail = 1;
				}
				close(fd_usb);
	
				printf("\nread file.\n");
				fd_usb = open("/mnt/usb2.txt", O_RDONLY);
				if(fd_usb < 0){
					printf("\nusb open file fail.\n");
					usb2fail = 1;
				}
				else{
					readwords = 0;
					memset(usbbuffer,0,sizeof(usbbuffer));
					readwords = read(fd_usb,usbbuffer,writewords);
					if(readwords < writewords){
						printf("\nread fail : only read %d words : %s\n",readwords,usbbuffer);
						usb2fail = 1;
					}
					else
						printf("read data %d words : %s\n",readwords,usbbuffer);
					close(fd_usb);
				}
	
				if(unlink("/mnt/usb2.txt") < 0){
					printf("\ndelete data fail!\n");
					usb2fail = 1;
				}
	
				if(strncmp(usbstr,usbbuffer,sizeof(usbstr)) != 0){
					printf("\nwrite & read data not match\n");
					usb2fail = 1;
				}
			}
			system("umount /mnt");
		}
		else
			usb2fail = 1;
	}
	
	if(usb2fail)
		printf("\n\033[1;31m usb_4th_port test fail!\033[m\n");
	else
		printf("\n\033[1;36m usb_4th_port test pass!\033[m\n");
	
	printf("\n======= usb test end =======\n");

//usb test end

//Car Record test
	printf("\n===== Car Record test ======\n");

	pthread_create(&IDThreadLEDControl, NULL, (void *)ThreadLEDControl, (void *)fd_gpio);
	pthread_create(&IDThreadBuzzerControl, NULL, (void *)ThreadBuzzerControl, (void *)0);//(void *)fd_gpio);

	bzero(self_ip, INET_ADDRSTRLEN);
	int ret = get_self_ip(self_ip);
	DDPRINTF("get_self_ip return %d, self_ip=%s\n", ret, self_ip);

	noVideo = noAudio = 0;
	catCam0 = catCam1 = 1;

	bzero((char *)rc, sizeof(struct RTSPClient)*CLIENTS);
	for(i=0; i<CLIENTS; i++) // 2 clients for now
	{
		rc[i].cseq = 1;
		rc[i].s_ip = ip;
		rc[i].c_ip = self_ip;
		rc[i].RTSPport = 554;
		sprintf(_tail[i], "live%d-1.sdp", i);
		rc[i].tail = _tail[i];
		rc[i].num = i;
		rc[i].first_time = 1;
		srandom((i+11)*7 +(i+5)*3);
		rc[i].mySSRC[0] = random();
		srandom(rc[i].mySSRC[0]);
		rc[i].mySSRC[1] = random();
		rc[i].b = &ebuf[i];
		pthread_mutex_init(&rc[i].r_mutex, NULL);
		pthread_mutex_init(&rc[i].read_mutex, NULL);
		pthread_cond_init(&rc[i].noData, NULL);
		pthread_cond_init(&rc[i].avSync, NULL);
		pthread_mutex_init(&rc[i].data_mutex, NULL);
		pthread_mutex_init(&rc[i].av_mutex, NULL);
	}
	pthread_mutex_init(&remain_mutex, NULL);
	//pthread_mutex_init(&remain_mutex[1], NULL);
	pthread_mutex_init(&clients_mutex, NULL);

	memset(&ebuf[0], 0, sizeof(struct EBuf) *2);
	//istc.pos = -1;

	pthread_create(&IDThreadCarRecord, NULL, ThreadCarRecord, (void *)fd_gpio);

	//button ---------------------------
	int cmd2 = GPIO_PORT(PN)| GPIO_PIN(4);

	signal(SIGIO, sigusr1_handler);
	errno = 0;
	if(fcntl(fd_gpio, F_SETOWN, getpid()) < 0)
	{
		DDPRINTF("\033[1;31m[%s]\033[m fcntl fail because '%s'\n", __func__, strerror(errno));
	}
	errno = 0;
	int oflags = fcntl(fd_gpio, F_GETFL);
	if(oflags < 0)
	{
		DDPRINTF("\033[1;31m[%s].2\033[m fcntl fail because '%s'\n", __func__, strerror(errno));
	}

	errno = 0;
	ret = fcntl(fd_gpio, F_SETFL, oflags | FASYNC);
	if(ret < 0)
	{
		DDPRINTF("\033[1;31m[%s].3\033[m fcntl fail because '%s'\n", __func__, strerror(errno));
	}

	errno = 0;
	if(ioctl(fd_gpio, SQ_GPIO_SETFASYNC, &cmd2) < 0)
	{
		//DDPRINTF("\033[1;31m[%s].4\033[m fcntl fail because '%s'\n", __func__, strerror(errno));
	}
	//---------------------------------

	while(cr_ready == 0)
		usleep(10*1000);

	static int first_time = 1;

//try_again:

	pthread_mutex_lock(&clients_mutex);
	rtsp_client_num = 0;
	pthread_mutex_unlock(&clients_mutex);

	sys_time();

	while(ping_target(1000, ip) != 0)
	{
		DDPRINTF("[%s] ping %s no response...\n", __func__, ip);
	}

	for(i=0; i<CLIENTS; i++)
	{
		if(create_client(&rc[i]) != 0)
		{
			if(i != 0 && rc[i].num != -1)
				exit_client(&rc[0]);
			break;
		}
	}

	usleep(1*1000*1000);

	//pthread_mutex_lock(&clients_mutex);
	//_clients = rtsp_client_num;
	//pthread_mutex_unlock(&clients_mutex);

	//time_t t1 = time(0);
	int catNum = (catCam0 == 1) + (catCam1 == 1);
	static int try_cnt = 0;
	//int cmd = LED_BLUE;

	int runtime = 0;
	while(runtime < 20)
	{
		pthread_mutex_lock(&clients_mutex);
		_clients = rtsp_client_num;
		pthread_mutex_unlock(&clients_mutex);
		if(_clients < catNum)
		{
			//LED_OFF(cmd);

			LEDStatus = LED_STATUS_STOP_RECORD;

			try_cnt++;
			/*
			if(try_cnt < 10)
			{
				DDPRINTF("\033[1;31mlost connection, try again %d times\033[m\n", try_cnt);
				usleep(500*1000);
				cli_re_init(ip, (char **)_tail);
				for(i=0; i<2; i++)
				{
					rc[i].s_ip = ip;
					sprintf(_tail[i], "live%d-1.sdp", i);
					rc[i].tail = _tail[i];
				}
				goto try_again;
			}
			else
			*/
			{
				BuzzerStatus = BUZZER_STATUS_STREAM_FAIL;
				//usleep(15*100*1000); // 1.5 secs

#if defined(AUTO_SPACE)
				int i;
				for(i=0; i<2; i++)
				{
					if(IndexBuffer[i] != NULL)
						free(IndexBuffer[i]);
					//if(IndexBufferDirtyFlag[i] != NULL)
					//	free(IndexBufferDirtyFlag[i]);
				}
#endif
				pthread_cancel(IDThreadLEDControl);
				pthread_cancel(IDThreadBuzzerControl);
				pthread_cancel(IDThreadCarRecord);
				close(fd_gpio);
				close(fd_rtc);

				while(BuzzerStatus != BUZZER_STATUS_INIT)
				{
					usleep(100*1000);
				}

				//int pid = fork();
				//if(pid == 0) //child process
				{
					execl(argv[0], argv[0], ip, (char *)0);
					return 0;
				}
				//return 0;
			}
		}
		else
		{
			//LED_ON(cmd);
			LEDStatus = LED_STATUS_START_RECORD;
			BuzzerStatus = BUZZER_STATUS_START_RECORDING;

			if(first_time)
			{
				char *content, *header;
				header = (char *)malloc(1024*1024);
				char web_site[256];

				memset(header, 0, 1024*1024);
				sprintf(web_site, "http://%s/cgi-bin/admin/res.0.lua?resolution=%s", ip, ((currRes[0]==RES_N_VGA)?"640x480":"320x240") );
				content = HTTP_request(web_site, 0, 1, header);
				DDPRINTF("\n--- %s ---\n", web_site);
				//printf("%s", content);
				usleep(300*1000);

				memset(header, 0, 1024*1024);
				sprintf(web_site, "http://%s/cgi-bin/admin/fr.0.lua?frame_rate=%d", ip, currFrr[0]);
				content = HTTP_request(web_site, 0, 1, header);
				DDPRINTF("\n--- %s ---\n", web_site);
				//printf("%s", content);
				usleep(300*1000);


				memset(header, 0, 1024*1024);
				sprintf(web_site, "http://%s/cgi-bin/admin/res.1.lua?resolution=%s", ip, ((currRes[1]==RES_N_VGA)?"640x480":"320x240"));
				content = HTTP_request(web_site, 0, 1, header);
				DDPRINTF("\n--- %s ---\n", web_site);
				//printf("%s", content);
				usleep(300*1000);

				memset(header, 0, 1024*1024);
				sprintf(web_site, "http://%s/cgi-bin/admin/fr.1.lua?frame_rate=%d", ip, currFrr[1]);
				content = HTTP_request(web_site, 0, 1, header);
				DDPRINTF("\n--- %s ---\n", web_site);
				//printf("%s", content);
				usleep(300*1000);

				free(header);

				BuzzerStatus = BUZZER_STATUS_OFF;

				first_time = 0;
			}

			cr_ready = 2;
			BuzzerStatus = BUZZER_STATUS_NONE;
			usleep(2*1000*1000);

			try_cnt = 0;
			runtime += 2;
		}
	}
	
	pthread_cancel(IDThreadLEDControl);
	pthread_cancel(IDThreadBuzzerControl);
	pthread_cancel(IDThreadCarRecord);
	close(fd_gpio);
	close(fd_rtc);
	
	printf("\n=== Car Record test end ====\n");

	return 0;
}



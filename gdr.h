#ifndef __GDR_H__
#define __GDR_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"


#define STRANGE_ERROR			0
#define MORE_THAN_DATA_BLCOKS	1
#define	LESS_THAN_DATA_BLCOKS	2

/*
#define	BUZZER_STATUS_INIT					0
#define	BUZZER_STATUS_OVER_SPEED			1
#define	BUZZER_STATUS_GSENSOR				2
#define	BUZZER_STATUS_NO_USBDISK			3
#define	BUZZER_STATUS_START_RECORDING		4
#define	BUZZER_STATUS_SPACE_THRESHOLD		5
#define	BUZZER_STATUS_SPACE_FULL			6
#define BUZZER_STATUS_OFF					98
#define BUZZER_STATUS_NONE					99

#define	LED_STATUS_INIT						0
#define	LED_STATUS_GPS_NOT_READY			1
#define	LED_STATUS_LIVE_VIEW				2
#define	LED_STATUS_PASSWORD_RESTORE			3
#define	LED_STATUS_PASSWORD_RESTORE_COMPLETE	4
#define	LED_STATUS_GPS_READY				5
#define	LED_STATUS_START_RECORD				6
#define	LED_STATUS_STOP_RECORD				7
*/


#define OFFSET_MARK_TABLE_0		0x8000000	//(128*1024*1024)
#define OFFSET_MARK_TABLE_1		0xA000000	//((128+32)*1024*1024)
#define QFCFG_TABLE_OFFSET		0xC000000
#define OFFSET_DISK_SERIAL		0xD000000
#define MAGIC_OFFSET			0xE000000

#define OFFSET_DATA_BLOCK		0x40000000LL	//(1024*1024*1024)


#define SETTING_TAG			0xABABABAB

#define	TAG_SECTOR_TABLE	0xFEFEFEFE
#define	INDEX_SECTOR_SIZE	512


#define	MAX_EVENT_EACH_16MB_DATA		16


#define MIN_DISK_SERIAL			1000
	
#define	LEN_DATA_BLOCK			(0x180000000LL)	//6G
#define	DISK_DATA_BLOCKS		(128*3)
#define	DISK_LIMIT_BLOCKS		(128*3-12*3)

#define	DATA_MACRO_BLOCK	0x1000000LL // (16*1024*1024)
#define	DATA_BLOCK_SIZE		(128*1024)
#define	DATA_TABLE_SIZE		512
#define	TAG_DATA_TABLE		0x000001A5


#define	EVENT_TYPE_NULL				0x00
#define	EVENT_TYPE_GSENSOR_TRIGGER	0x01
#define EVENT_TYPE_HAND_TRIGGER		0x02
//#define EVENT_TYPE_REMOVE_MARK	0x04
#define	EVENT_TYPE_OVER_SPEED		0x08
#define	EVENT_TYPE_SUICIDE			0x10


#define INDEX_AREA_SIZE		(32*1024*1024)
#define INDEX_SIZE			(512)




#define	INDEX_NO_MARK		0
#define	INDEX_MARK			1


#if 0
typedef struct
{
	unsigned long Tag;
	char Fw[12];   // string eg: "V11.24.36"
	unsigned char Data[128];
	char Password[12]; // string eg: "micky123456"
	char XAxis[8];	// string eg: "3.124"
	char YAxis[8];	// string eg: "5.179"
	char ZAxis[8];	// string eg:  "2.323"
	unsigned char Limit;		// int 0~1, eg: 0
	unsigned char Unit;		// int 0~1, 0=KM, 1=MILE, eg: 0
	unsigned char Value;		// int 0~255 ,eg:120 速限
	unsigned char Record;		// int 0~1
	unsigned char Alert;		// int 0~1
	unsigned char AlertItems;	// int value = (1|2|4|8);
	unsigned short DPI;		// int 320 or 640, eg: 640
	unsigned char Frame;		// int 1~30
	unsigned char Auto;		// int 0~1
	unsigned char ManRec;		// int 0~1
	char Scene;		// char eg: 'A'
	unsigned char Cover;		// int 0~1
	unsigned char Reserve;		// int 1~5
	unsigned short DPI2;	//ivan+
	unsigned char Frame2;	//ivan+
	unsigned char Audio;	//ivan+
	unsigned char Reserved[512 - 202 ];
	unsigned long Checksum;
} USB_SETTING_T;
#else

typedef struct
{
	unsigned long Tag;
	char Fw[12];   // string eg: "V11.24.36"
	unsigned char Data[128];
	char Password[12]; // string eg: "micky123456"
	char XAxis[8];	// string eg: "3.124"
	char YAxis[8];	// string eg: "5.179"
	char ZAxis[8];	// string eg:  "2.323"
	unsigned char Limit;		// int 0~1, eg: 0
	unsigned char Unit;		// int 0~1, 0=KM, 1=MILE, eg: 0
	unsigned char Value;		// int 0~255 ,eg:120 速限
	unsigned char Record;		// which camera work ? 0-Off, 1-first, 2-second, 3-1+2
	unsigned char Alert;		// int 0~1
	unsigned char AlertItems;	// int value = (1|2|4|8);
	unsigned short DPI;		// int 320 or 640, eg: 640
	unsigned char Frame;		// int 1~30
	unsigned char Auto;		// int 0~1
	unsigned char ManRec;		// which func. be support? 1-force record, 2-record pause, 3-1+2
	char Scene;		// char eg: 'A'
	unsigned char Cover;		// int 0~1
	unsigned char Reserve;		// int 1~5
	unsigned short DPI2;	//ivan+
	unsigned char Frame2;	//ivan+
	unsigned char Audio;	//ivan+
	unsigned short MarkTimes; //ivan+
	unsigned char Reserved[512 -198 -6]; //ivan -6
	unsigned long Checksum;
}USB_SETTING_T;

#endif



typedef struct // 12 bytes
{
	unsigned int DataBlockSerial;                   
	unsigned char EventTime[6];
	unsigned short EventType;
} EVENT_INFO_T;


typedef struct // 224 bytes
{
	unsigned int IndexTableSerial;
	EVENT_INFO_T EventInfo[MAX_EVENT_EACH_16MB_DATA]; // 12*16 = 192 bytes
	unsigned int DataBlockSerial;	//write in WeiteOneDataBlock

#if 1 //debug   
	unsigned char DataBlockTimeStamp[6];	//write in WeiteOneDataBlock
#else   
	unsigned int DataBlockTimeStamp;
	unsigned char Reserved1[2];
#endif

	unsigned char FlagRecordStart;	//write in WeiteOneDataBlock
	unsigned char FlagRecordEnd;
	unsigned char FlagMask;
	unsigned char EventInfoCounter;//useless......
	unsigned char Reserved2[2];
	unsigned int DataBlockEndSerial;	//write in WriteOneDataBlock
	unsigned char DataBlockEndTimeStamp[6];	//write in WeiteOneDataBlock
} INDEX_TABLE_T;


#if 0 //old code
typedef struct //512 bytes
{
	unsigned int Tag;	//Tag = 0xfefefefe,magic number depends on version		//4 byte
	INDEX_TABLE_T IndexTable;
	unsigned char Reserved[INDEX_SECTOR_SIZE-sizeof(int)-sizeof(INDEX_TABLE_T)-sizeof(int)];
	unsigned int Checksum;
} INDEX_SECTOR_T;
#else
#define INDEX_REVERSED_SIZE	(INDEX_SECTOR_SIZE -sizeof(long) -sizeof(INDEX_TABLE_T) -sizeof(short)*5 -sizeof(int)*3 -sizeof(long)*2)

typedef struct //512 bytes
{
	unsigned long Tag;	//Tag = 0xfefefefe,magic number depends on version		//4 byte
	INDEX_TABLE_T IndexTable;
	unsigned short GSensorSensitivityVer;       // 2010-04-09 add
	unsigned short GSensorSensitivityAlgorithm; // 2010-04-09 add
	unsigned short AudioSettingVer;             // 2010-04-09 add
	unsigned short VideoSettingVer;             // 2010-04-09 add
	unsigned short FW_VER;                      // 2010-04-09 add
	unsigned char Reserved[INDEX_REVERSED_SIZE];
	int prev_off, self_off, next_off; //ivan+
	unsigned long  SetupSerial;
	unsigned long Checksum;
} INDEX_SECTOR_T;
#endif

//#define	MAX_EVENT_EACH_16MB_DATA	16

#define	ENDIAN_INT(x) ((((x)<<24)&0xff000000)|(((x)<<8)&0x00ff0000)|(((x)>>8)&0x0000ff00)|(((x)>>24)&0x000000ff))
#define	ENDIAN_SHORT(x) ((((x)<<8)&0xff00)|(((x)>>8)&0x00ff))


#define	EVENT_FIX
//#define	EVENT_TYPE_NULL				0x00
//#define	EVENT_TYPE_GSENSOR_TRIGGER	0x01
//#define EVENT_TYPE_HAND_TRIGGER	0x02
//#define EVENT_TYPE_REMOVE_MARK		0x04
//#define	EVENT_TYPE_OVER_SPEED		0x08
//#define	EVENT_TYPE_SUICIDE			0x10

//#define	OFFSET_MARK_TABLE_0 (128*1024*1024)
//#define	OFFSET_MARK_TABLE_1 ((128+32)*1024*1024)
//#define	OFFSET_DATA_BLOCK (1024*1024*1024)

#define	CHANNEL_OUT	0
#define	CHANNEL_IN	1


typedef struct 
{
   unsigned int Tag;
   unsigned int DataBlockSerial;	//must init
#if 1 //debug   
   unsigned char DataBlockTimeStamp[6];
#else   
   unsigned int DataBlockTimeStamp;
   unsigned char Reserved1[2];
#endif
   unsigned char Channel;   //0 & 2 for 2 camera                          
   unsigned char FlagRecordStart;	//錄的第一筆.其中一個應該是在最開頭
   unsigned char FlagMask;					//強制錄影
   unsigned char GSensorCounter;	//how many Gsensor data ? GSensorCounter is 0 in Aptos
   unsigned char GPSCounter;			//how many gps data
   unsigned char Resolution;	
   unsigned char FrameRate;     
   unsigned char FlagAudio;      
   unsigned char Reserved[26];   
   unsigned short GSensorData[50][3];  
   unsigned char GPSData[160];   
   unsigned int CheckSum;
} DATA_TABLE_T;


#define	MAX_STORAGE_PATH	128
#define	STORAGE_UNITS		2


#define	MAX_MSG_SIZE		128

typedef struct {
		long	mtype;
		int		Command;
		char	mtext[MAX_MSG_SIZE];
} MSGBUF ;


#define	KEY_SHM_ENCODE		1680
#define	KEY_SEM_ENCODE		1681
#define	KEY_SEM_API			1682
#define	KEY_MSG_CONTROL		1683
#define	KEY_MSG_RESPONSE	1684
#define	KEY_SEM_DP_SESSION	1685
#define	KEY_SEM_SNAPSHOT	1686
#define	KEY_SEM_MOTION_HANDLE	1687
#define KEY_MSG_MOTION		1689

#define	KEY_SYSTEM_MSG_CONTROL	1720

#define	MSG_CONTROL			0x01
#define	MSG_SYSTEM_REBOOT	0x10
#define	MSG_SYSTEM_DATE		0x11

#define	MAX_DATA_BLOCK	8

typedef struct
{
	int	Limit;
	int	Value;
	int	XAxis;
	int	YAxis;
	int	ZAxis;
	int Resolution;
	int	FrameRate;
	int Scene;
} CAR_SETTING_T;

typedef struct
{
	long long DataBlockWriteBase;
	long long IndexSectorWriteBase[2];	
	//long long DataBlockWriteOffset;
	long long xx;
	long long IndexSectorWriteOffset;

	char Buffer[MAX_DATA_BLOCK][DATA_BLOCK_SIZE];
	//int Channel[MAX_DATA_BLOCK];
	int DataBlockSerial;
	int DataBlock;	//buffer裡有幾個128K
	char StoragePath[MAX_STORAGE_PATH];
	int StorageDescriptor;
	int CurrentChannel;
	int FlagRecordStart[2];
	int PingPong;	//PingPong=0 or 1
	DATA_TABLE_T DataTable;
	INDEX_SECTOR_T IndexSector;	//也是暫存用的
	INDEX_SECTOR_T IndexSectorBuffer;
	CAR_SETTING_T CarSetting;
	int num; //ivan+
	unsigned int itSerial; //ivan+
} CAR_RECORDER_T;	

typedef struct
{
	CAR_RECORDER_T CarRecorder[2];
	int USBDiskCount;
	int ActiveCarRecorder;	//0 or 1
	int DiskSerial;
	int PartDiskSerial[2];
} DUAL_USB_T;

#define	ENCODER_BUFFER_BLOCKS	32	//16
#define	ENCODER_BUFFER_LEN		(ENCODER_BUFFER_BLOCKS * (DATA_BLOCK_SIZE-512))	

//1. IndexRead,永遠指向. 64K boundry
//2. IndexWrite.指向. 下一個寫入點
//3. InitCondition IndexRead=IndexWrite=0
//4. DataRemain. 一直算這個.有64K時. 就可以read.

typedef struct
{
	int IndexRead;	//by DATA_BLOCK_SIZE
	int IndexWrite;	//by Byte
	int DataRemain;	//not yet read datas.,by Byte
	char Buffer[ENCODER_BUFFER_BLOCKS][DATA_BLOCK_SIZE-512];

} ENCODER_BUFFER_T;

#define	MARK_THRESHOLD_UNDER	0
#define	MARK_THRESHOLD_OVER		1
#define	MARK_THRESHOLD_STOP		2


#define	RES_NTSC_D1			0x00
#define	RES_NTSC_FIELD		0x01 
#define	RES_NTSC_CIF		0x03
#define	RES_NTSC_QCIF		0x04 
#define	RES_PAL_D1			0x05
#define	RES_PAL_FIELD		0x06
#define	RES_PAL_CIF			0x07
#define	RES_PAL_QCIF		0x08 
#define	RES_N_VGA			0x09
#define	RES_N_QVGA			0x0a
#define	RES_N_QQVGA			0x0b
#define	RES_P_VGA			0x0c 
#define	RES_P_QVGA			0x0d 
#define	RES_P_QQVGA			0x0e




#define	SQ_PWM "/dev/sq-pwm"

#define SQ_PWM_IOC_MAGIC	0xEE
#define SQ_PWM_RESET		_IO(SQ_PWM_IOC_MAGIC, 0x00)
#define SQ_PWM_START		_IO(SQ_PWM_IOC_MAGIC, 0x01)
#define SQ_PWM_STOP			_IO(SQ_PWM_IOC_MAGIC, 0x02)
#define SQ_PWM_SET_FREQ		_IOW(SQ_PWM_IOC_MAGIC, 0x03, unsigned int)
#define	SQ_GPIO_SETFASYNC	_IOW(SQ_GPIO_IOC_MAGIC, 0x04, int)

#define	MASK_BUTTON			0x000F0000
#define BUTTON_PRESS		0x00010000
#define BUTTON_PRESS_LONG	0x00020000
#define BUTTON_RELEASE		0x00040000

#define OFFSET_BUTTON		0x00FF0000



#endif


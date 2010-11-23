
#ifndef _LARGEFILE64_SOURCE
#define _LARGEFILE64_SOURCE
#endif

/*
 * 首先讀取位於 0xA000000 的 index sector table（雖然 0x8000000 也有一份，但實際使用的是這份）
 * 找出最後一個有使用的 index sector 並將其 FlagRecordEnd 設為1
 *
 * 接著讀取位於 0xC000000 的 config table 並對其進行各項設定
 *
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#include "gdr.h"


unsigned long calc_checksum_us(unsigned long *data, int len)
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
	cfg->Checksum = calc_checksum_us( (unsigned long *)cfg, (512-4)/4);
}

int fix_index_sector_table(char *fname, off64_t tab)
{
	char buf[512*64*16]; // 16M*64 = 1G, *16 = 16G
	int fd;

	memset(buf, 0, sizeof(buf));

	errno = 0;
	if( (fd = open(fname, O_RDWR)) < 0)
	{
		printf("'%s' open fail because '%s'\n", fname, strerror(errno));
		return -99;
	}

	//off64_t tab = OFFSET_MARK_TABLE_1;
	errno = 0;
	if(lseek64(fd, tab, SEEK_SET) < 0)
	{
		printf("seek 0x%llu fail because '%s'\n", tab, strerror(errno));
		close(fd);
		return -1;
	}

	errno = 0;
	size_t rs = read(fd, buf, sizeof(buf));
	if(rs != sizeof(buf))
	{
		printf("read fail because '%s' (expect %d bytes but only %d bytes)\n", strerror(errno), sizeof(buf), rs);
		close(fd);
		return -2;
	}

	INDEX_SECTOR_T *ist = (INDEX_SECTOR_T *)buf;
	if(ist->IndexTable.IndexTableSerial == 0)
		return 0;
	while(ist->IndexTable.IndexTableSerial != 0)
		ist++;
	ist--;
	if(ist->IndexTable.FlagRecordEnd == 0)
	{
		printf("check & fix index sector table.\n");
		ist->IndexTable.FlagRecordEnd = 1;
	}

	lseek64(fd, tab, SEEK_SET);
	errno = 0;
	if( (rs = write(fd, buf, sizeof(buf))) != sizeof(buf))
	{
		printf("write fail because '%s' (expect %d bytes but only %d bytes)\n", strerror(errno), sizeof(buf), rs);
		close(fd);
		return -3;
	}

	close(fd);
	return 0;
}

void Usage(char *name)
{
	printf("Usage : %s -f FILENAME -t MARK_TABLE\n", name);
	printf("    FILENAME -- e.g. /dev/sdb or ./sda.usb\n");
	printf("    MARK_TABLE -- 1 = 0xA000000, other = 0x8000000\n");
}

int main(int argc, char *argv[])
{

#if defined(TARGET_SQ)
	char *str = "/dev/usb_3rd_port";
#else
	char str[] = "/dev/sdb";
#endif

	char *fname = str;
	int fd;
	char tmp[64];
	int num;
    off64_t off, tab=OFFSET_MARK_TABLE_0;
	int c;

	opterr = 0;
	while ((c = getopt (argc, argv, "f:t:")) != -1)
	{
		switch (c)
		{
			case 'f':
				fname = optarg;
				break;
			case 't':
				if(optarg[0] == '1')
					tab = OFFSET_MARK_TABLE_1;
				else
					tab = OFFSET_MARK_TABLE_0;
				break;
			default:
				Usage(argv[0]);
				break;
		}
	}

	if(fix_index_sector_table(fname, tab) == -99)
		return -1;

	off = QFCFG_TABLE_OFFSET;

	errno = 0;
	fd = open(fname, O_RDWR);
	if(fd < 0)
	{
		printf("file '%s' open fail, because '%s'\n", fname, strerror(errno));
		return (-1);
	}

	USB_SETTING_T usb;
	char key, quit=0;
	int ws;

	errno = 0;
	if(lseek64(fd, off, SEEK_SET) < 0)
	{
		printf("[%s] seek position %llx fail because '%s'\n", fname, off, strerror(errno));
		close(fd);
		return (-3);
	}

	errno = 0;
	if( (ws = read(fd, (char *)&usb, 512)) != 512)
	{
		printf("read() fail because '%s', expect 512 bytes but got %d bytes\n", strerror(errno), ws);
		close(fd);
		return (-2);
	}

	if(usb.Tag != SETTING_TAG)
	{
		printf("Invalid Config, write default.\n");
		config_default(&usb);
	}

	unsigned char *p = (unsigned char *)&usb.Tag;
	printf("\ncurrent USB settings....\n");
	printf("Discriminator : %02X %02X %02X %02X\n", p[0], p[1], p[2], p[3]);
	printf("Camera 1 -- resolution = %d, frame rate = %d\n", usb.DPI, usb.Frame);
	printf("Camera 2 -- resolution = %d, frame rate = %d\n", usb.DPI2, usb.Frame2);
	printf("X/Y/Z Axis = (%s, %s, %s)\n", usb.XAxis, usb.YAxis, usb.ZAxis);
	printf("Speed limitation enable = %d, unit = %d, speed = %d\n", usb.Limit, usb.Unit, usb.Value);
	printf("Video record = %d, Audio record = %d\n", usb.Record, usb.Audio);
	printf("Alarm = %d, AlarmType = 0x%X\n", usb.Alert, usb.AlertItems);
	printf("ManRec = 0x%02X, MarkTimes = %d(sec)\n\n", usb.ManRec, usb.MarkTimes);

	printf("press Enter to continue...\n");
	getchar();

	do
	{
		printf("\n--- Camera 1 ---------------\n");
		printf(" 1. Select Resolution \n");
		printf(" 2. Set FrameRate \n");
		printf("--- Camera 2 ---------------\n");
		printf(" 3. Select Resolution \n");
		printf(" 4. Set FrameRate \n");
		printf("--- Record -----------------\n");
		printf(" 5. Video \n");
		printf(" 6. Audio \n");
		printf("--- G-Sensor ---------------\n");
		printf(" 7. X-Axis \n");
		printf(" 8. Y-Axis \n");
		printf(" 9. Z-Axis \n");
		printf("--- Speed Limitation -------\n");
		printf(" a. Enabled? \n");
		printf(" b. Value \n");
		printf(" c. Uint \n");
		printf("--- Alarm ------------------\n");
		printf(" d. Enabled? \n");
		printf(" e. Type \n");
		printf("--- Special Function -------\n");
		printf(" f. CAN Force Record? \n");
		printf(" g. CAN Record Pause? \n");
		printf(" h. MarkTimes? \n");
		printf("----------------------------\n");
		printf(" w. Wirte Default Config \n");
		printf("----------------------------\n");
		printf(" q. quit\n");

		switch (key = getchar())
		{
			case '1':
				printf("Camera 1 -- 1:640x480, 2:320x240 : ");
				scanf("%s", tmp);
				num = atoi(tmp);
				if(num==2)
					usb.DPI = 320;
				else
					usb.DPI = 640;
				break;
			case '2':
				printf("Camera 1 -- 1 ~ 30 : ");
				scanf("%s", tmp);
				num = atoi(tmp);
				if(num < 0 || num > 30)
					num = 30;
				usb.Frame = num;
				break;
			case '3':
				printf("Camera 2 -- 1:640x480, 2:320x240 : ");
				scanf("%s", tmp);
				num = atoi(tmp);
				if(num==2)
					usb.DPI2 = 320;
				else
					usb.DPI2 = 640;
				break;
			case '4':
				printf("Camera 2 -- 1 ~ 30 : ");
				scanf("%s", tmp);
				num = atoi(tmp);
				if(num < 0 || num > 30)
					num = 30;
				usb.Frame2 = num;
				break;
			case '5':
				printf("0:OFF, 1:Cam1, 2:Cam2, 3:Cam1+Cam2 : ");
				scanf("%s", tmp);
				num = atoi(tmp);
				if(num < 0 || num > 3)
					num = 3;
				usb.Record = num;
				break;
			case '6':
				printf("0:OFF, 1:ON : ");
				scanf("%s", tmp);
				num = atoi(tmp);
				if(num == 1)
					usb.Audio = 1;
				else
					usb.Audio = 0;
				break;
			case '7':
				printf("X (e.g. 3.124) : ");
				scanf("%s", tmp);
				memcpy(usb.XAxis, tmp, 5);
				usb.XAxis[5] = 0;
				break;
			case '8':
				printf("Y (e.g. 5.301) : ");
				scanf("%s", tmp);
				memcpy(usb.YAxis, tmp, 5);
				usb.YAxis[5] = 0;
				break;
			case '9':
				printf("Z (e.g. 2.254) : ");
				scanf("%s", tmp);
				memcpy(usb.ZAxis, tmp, 5);
				usb.ZAxis[5] = 0;
				break;
			case 'a':
			case 'A':
				printf("0:OFF, 1:ON : ");
				scanf("%s", tmp);
				num = atoi(tmp);
				if(num == 1)
					usb.Limit = 1;
				else
					usb.Limit = 0;
				break;
			case 'b':
			case 'B':
				printf("1 ~ 255 : ");
				scanf("%s", tmp);
				num = (unsigned char)atoi(tmp);
				usb.Value = num;
				break;
			case 'c':
			case 'C':
				printf("0:KM, 1:MILE : ");
				scanf("%s", tmp);
				num = atoi(tmp);
				if(num == 1)
					usb.Unit = 1;
				else
					usb.Unit = 0;
				break;
			case 'd':
			case 'D':
				printf("0:OFF, 1:ON : ");
				scanf("%s", tmp);
				num = atoi(tmp);
				if(num == 1)
					usb.Alert = 1;
				else
					usb.Alert = 0;
				break;
			case 'e':
			case 'E':
				printf("1:NoSpace | 2:NoUSB | 4:Event | 8:Speed : ");
				scanf("%s", tmp);
				num = atoi(tmp) & 0x0F;
				usb.AlertItems = num;
				break;
			case 'f':
			case 'F':
				printf("forceRecord  0:OFF, 1:ON : ");
				scanf("%s", tmp);
				num = (tmp[0] == '1')?1:0;
				usb.ManRec |= num;
				break;
			case 'g':
			case 'G':
				printf("recordPause  0:OFF, 1:ON : ");
				scanf("%s", tmp);
				num = (tmp[0] == '1')?2:0;
				usb.ManRec |= num;
				break;
			case 'h':
			case 'H':
				printf("How many seconds to be mark : ");
				scanf("%s", tmp);
				num = atoi(tmp);
				usb.MarkTimes = num;
				break;
			case 'q':
			case 'Q':
				quit = 1;
				break;
			case 'w':
			case 'W':
				config_default(&usb);
				break;
		}
	} while(quit==0);

	usb.Checksum = calc_checksum_us( (unsigned long *)&usb, 128-1);

	errno = 0;
	if(lseek64(fd, off, SEEK_SET) < 0)
	{
		printf("[%s] seek position %llx fail because '%s'\n", fname, off, strerror(errno));
		close(fd);
		return (-3);
	}

	errno = 0;
	if( (ws = write(fd, (char *)&usb, 512)) != 512)
	{
		printf("write() fail because '%s', expect 512 bytes but write %d bytes\n", strerror(errno), ws);
		close(fd);
		return (-4);
	}


	unsigned char Magic[8];
	time_t MagicTime = time(NULL);
	Magic[0]=0xff;
	Magic[1]=0x11;
	Magic[2]=0x22;
	Magic[3]=0x33;
	memcpy(&Magic[4], &MagicTime, 4);

	off = 0xE000000LL;
	errno = 0;
	if(lseek64(fd, off, SEEK_SET) < 0)
	{
		printf("[%s] seek position %llx fail because '%s'\n", fname, off, strerror(errno));
		close(fd);
		return (-3);
	}

	errno = 0;
	if( (ws = write(fd, (char *)&usb, 8)) != 8)
	{
		printf("write() fail because '%s', expect 8 bytes but write %d bytes\n", strerror(errno), ws);
		close(fd);
		return (-4);
	}

	printf("\nnew USB settings....\n");
	printf("Camera 1 -- resolution = %d, frame rate = %d\n", usb.DPI, usb.Frame);
	printf("Camera 2 -- resolution = %d, frame rate = %d\n", usb.DPI2, usb.Frame2);
	printf("X/Y/Z Axis = (%s, %s, %s)\n", usb.XAxis, usb.YAxis, usb.ZAxis);
	printf("Speed limitation enable = %d, unit = %d, speed = %d\n", usb.Limit, usb.Unit, usb.Value);
	printf("Video record = %d, Audio record = %d\n", usb.Record, usb.Audio);
	printf("Alarm = %d, AlarmType = 0x%X\n", usb.Alert, usb.AlertItems);
	printf("ManRec = 0x%02X, MarkTimes = %d(sec)\n\n", usb.ManRec, usb.MarkTimes);
	printf("Checksum = 0x%08lX\n\n", usb.Checksum);

	printf("\n\nUSB Setting DONE!! \n\n");
	close(fd);
	return 0;
}



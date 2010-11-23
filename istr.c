#ifndef _LARGEFILE64_SOURCE
#define _LARGEFILE64_SOURCE
#endif

/*
	Index Sector Table reader
*/

#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

#include "gdr.h"


void Usage(char *s)
{
	printf("Usage : %s -d DEVICE_NAME -t -b -f\n", s);
	printf("  -d -- specify device name e.g. /dev/sdb\n");
	printf("  -t -- use backup index table\n");
	printf("  -b -- batch mode (not stop)\n");
	printf("  -f -- fix index table\n");
	printf("  -s -- read sub_data_table\n");
}


int ReadDataTables(int index, DATA_TABLE_T *dt_buf, int fd)
{
	int i;
	DATA_TABLE_T *buf;
	off64_t off = OFFSET_DATA_BLOCK +(index *DATA_MACRO_BLOCK);
	off += (DATA_BLOCK_SIZE -DATA_TABLE_SIZE);

	errno = 0;
	if(lseek64(fd, off, SEEK_SET) < 0)
	{
		printf("seek 0x%llx fail because '%s'\n", off, strerror(errno));
		return (-1);
	}

	size_t rs;
	for(i=0; i<DATA_MACRO_BLOCK/DATA_BLOCK_SIZE; i++)
	{
		errno = 0;
		rs = read(fd, buf, DATA_TABLE_SIZE);
		if(rs != DATA_TABLE_SIZE)
		{
			printf("read sub_data_block %d fail because '%s' (expect %d bytes but only %d bytes)\n", i, strerror(errno), DATA_TABLE_SIZE, rs);
			return (-1);
		}
		buf++;
	}

	return 0;
}



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


int main(int argc, char *argv[])
{
#define	BUF_SIZE	(512*64*16)

	char *buf; // 16M*64 = 1G, *16 = 16G
	int fd;
	int c, do_batch=0, do_fix=0, do_sub_data_table=0;
	off64_t tab = OFFSET_MARK_TABLE_0;

	char *dn;
#if defined(TARGET_SQ)
	char *dev_name = "/dev/usb_3rd_port";
#else
	char dev_name[] = "/dev/sdb";
#endif

	dn = dev_name;

	opterr = 0;
	while ((c = getopt (argc, argv, "d:tbfs")) != -1)
	{
		switch (c)
		{
			case 'd':
				dn = optarg;
				break;
			case 't':
				tab = OFFSET_MARK_TABLE_1;
				break;
			case 'b':
				do_batch = 1;
				break;
			case 'f':
				do_fix = 1;
				break;
			case 's':
				do_sub_data_table = 1;
				break;
			default:
				Usage(argv[0]);
				return 0;
		}
	}


	errno = 0;
	buf = (char *)malloc(BUF_SIZE);
	if(buf == NULL)
	{
		printf("malloc fail because '%s'\n", strerror(errno));
		exit(-1);
	}

	memset(buf, 0, BUF_SIZE);

	errno = 0;
	if( (fd = open(dn, O_RDWR)) < 0)
	{
		printf("%s open fail because '%s'\n", dn, strerror(errno));
		exit(-1);
	}

	errno = 0;
	if(lseek64(fd, tab, SEEK_SET) < 0)
	{
		printf("seek 0x%llx fail because '%s'\n", tab, strerror(errno));
		close(fd);
		exit(-1);
	}

	errno = 0;
	size_t rs = read(fd, buf, BUF_SIZE);
	if(rs != BUF_SIZE)
	{
		printf("read fail because '%s' (expect %d bytes but only %d bytes)\n", strerror(errno), BUF_SIZE, rs);
		close(fd);
		exit(-1);
	}

	printf("\033[1;32m read table from 0x%8llX\033[m\n", tab);

	INDEX_SECTOR_T *ist = (INDEX_SECTOR_T *)buf;

	if(do_fix == 1)
	{
		unsigned int serial, old_serial;
		INDEX_SECTOR_T *old_ist = ist;
		ist++;
		while(ist->IndexTable.IndexTableSerial != 0)
		{
			serial = ENDIAN_INT(ist->IndexTable.IndexTableSerial);
			old_serial = ENDIAN_INT(old_ist->IndexTable.IndexTableSerial);
			old_serial++;
			if(serial != old_serial)
			{
				old_ist->IndexTable.FlagRecordEnd = 1;
				ist->IndexTable.FlagRecordStart = 1;
				old_ist->Checksum = calc_checksum( (unsigned long *)old_ist, (INDEX_SECTOR_SIZE-4)/4);
				ist->Checksum = calc_checksum( (unsigned long *)ist, (INDEX_SECTOR_SIZE-4)/4);
			}
			old_ist = ist;
			ist++;
		}
		lseek64(fd, tab, SEEK_SET);
		write(fd, buf, BUF_SIZE);
	}


	ist = (INDEX_SECTOR_T *)buf;
	unsigned char *p;
	int recs=0;
	//int sdb_num = (DATA_MACRO_BLOCK/DATA_BLOCK_SIZE);
	while(ist->IndexTable.IndexTableSerial != 0)
	{
		printf("\n--- %d ---------------------------\n", recs);
		if(ist->Tag != TAG_SECTOR_TABLE)
			printf("Tag : \033[1;31m0x%08lX\033[m(\033[1;32m0x%08X\033[m), ", ist->Tag, TAG_SECTOR_TABLE);
		else
			printf("Tag : 0x%08lX, ", ist->Tag);
		unsigned long cs = calc_checksum( (unsigned long *)ist, (INDEX_SECTOR_SIZE-4)/4);
		if((ist->Checksum) != cs)
			printf("Checksum : \033[1;31m0x%08lX\033[m(\033[1;32m0x%08lX\033[m)\n", (ist->Checksum), cs);
		else
			printf("Checksum : 0x%08lX\n", (ist->Checksum));
		printf("IndexTableSerial : %08X\n", ENDIAN_INT(ist->IndexTable.IndexTableSerial) );
		printf("DataBlockSerial  : %08X\n", ENDIAN_INT(ist->IndexTable.DataBlockSerial) );
		p = ist->IndexTable.DataBlockTimeStamp;
		printf("Start TimeStamp  : %04d.%02d.%02d %02d:%02d:%02d\n", p[0]+2000, p[1], p[2], p[3], p[4], p[5]);
		p = ist->IndexTable.DataBlockEndTimeStamp;
		printf("End TimeStamp    : %04d.%02d.%02d %02d:%02d:%02d\n", p[0]+2000, p[1], p[2], p[3], p[4], p[5]);
		printf("RecordStart=%d, RecordEnd=%d, EventMask=%02X\n", ist->IndexTable.FlagRecordStart, ist->IndexTable.FlagRecordEnd, ist->IndexTable.FlagMask);
		printf("prev_off=0x%08X, self_off=0x%08X, next_off=0x%08X\n", ist->prev_off, ist->self_off, ist->next_off);
		//if(getchar() == 'd')
		if(ist->IndexTable.FlagMask)
		{
			int i;
			EVENT_INFO_T *eip = ist->IndexTable.EventInfo;
			unsigned char *t;
			for(i=0; i<16; i++)
			{
				t = eip->EventTime;
				printf("  event : type=%d, time=%04d.%02d.%02d %02d:%02d:%02d, serial=0x%08x\n",
				ENDIAN_SHORT(eip->EventType),
				t[0]+2000, t[1], t[2], t[3], t[4], t[5],
				ENDIAN_INT(eip->DataBlockSerial) );
				eip++;
			}
		}
		if(do_sub_data_table == 1)
		{
			int i=0;
			INDEX_SECTOR_T *ist2 = (INDEX_SECTOR_T *)buf;
			ist2 += recs;
			DATA_TABLE_T dtt;
			off64_t soff = OFFSET_DATA_BLOCK +DATA_MACRO_BLOCK*recs +DATA_BLOCK_SIZE -DATA_TABLE_SIZE;
			printf(" -------- DataOffset = 0x%08llX\n", OFFSET_DATA_BLOCK +DATA_MACRO_BLOCK*recs);
			//for(i=0; i<DATA_MACRO_BLOCK/DATA_BLOCK_SIZE; i++)
			{
				lseek64(fd, soff, SEEK_SET);
				errno = 0;
				if(read(fd, &dtt, DATA_TABLE_SIZE) < 0)
				{
					printf("sub data table read fail because '%s'\n", strerror(errno));
					close(fd);
					free(buf);
					exit(-1);
				}
				unsigned char *t = dtt.DataBlockTimeStamp;
				printf("    sub %d, time=%04d.%02d.%02d %02d:%02d:%02d, serial=0x%08X, chn=%d\n", i,
					t[0]+2000, t[1], t[2], t[3], t[4], t[5],
					ENDIAN_INT(dtt.DataBlockSerial), dtt.Channel);
				//soff += DATA_BLOCK_SIZE;
				ist2->IndexTable.DataBlockSerial = dtt.DataBlockSerial;
				ist2->IndexTable.DataBlockEndSerial = ENDIAN_INT(ENDIAN_INT(dtt.DataBlockSerial) +127);
				ist2->Checksum = calc_checksum( (unsigned long *)ist2, (INDEX_SECTOR_SIZE-4)/4);
			}
		}
		if(!do_batch)
		{
			printf("--- (press enter to next record) -----------\n");
			getchar();
		}
		ist++;
		recs++;
	}
	if(recs == 0)
		printf("Index table is empty.\n");
	else
	{
		printf("Total %d records.\n", recs);

		if(do_sub_data_table && do_fix)
		{
			lseek64(fd, tab, SEEK_SET);
			write(fd, buf, BUF_SIZE);
		}

	}

	free(buf);
	close(fd);

	return 0;
}




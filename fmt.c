/*
 *    initial IndexSectorTable & clear first data block
 */

#ifndef _LARGEFILE64_SOURCE
#define _LARGEFILE64_SOURCE
#endif

#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

#include "gdr.h"

#if defined(TARGET_SQ)
#undef INDEX_AREA_SIZE
#define INDEX_AREA_SIZE		(4*1024*1024)
#endif


void Usage(char *s)
{
	printf("Usage : %s [-d DEVICE_NAME] [-f|-b N]\n", s);
	printf("  -d -- specify device name e.g. /dev/sdb\n");
	printf("  -f -- full format(include sub_data_table)\n");
	printf("  -b -- like '-f' but only N blocks\n");
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


void ist_default(INDEX_SECTOR_T *IndexSector)
{
	memset(IndexSector, 0, INDEX_SECTOR_SIZE);
	IndexSector->Tag = ENDIAN_INT(TAG_SECTOR_TABLE);
	//IndexSector->GSensorSensitivityVer = ENDIAN_SHORT(1);
	//IndexSector->GSensorSensitivityAlgorithm = ENDIAN_SHORT(1);
	IndexSector->Checksum = calc_checksum( (unsigned long *)IndexSector, (INDEX_SECTOR_SIZE-4)/4);
}

// index table max capacity is 0xA000000 - 0x8000000 = 32M
// 32M / 512b = 65536(blocks)

int main(int argc, char *argv[])
{
	int i, t;
	off64_t tab[2] = {OFFSET_MARK_TABLE_0, OFFSET_MARK_TABLE_1};

	char *buf;
	int fd;
	int c, full_format=0, fmt_blocks=0;
	INDEX_SECTOR_T *ist;
	off64_t soff, disk_size;

	char *dn;
#if defined(TARGET_SQ)
	char *dev_name = "/dev/usb_3rd_port";
#else
	char dev_name[] = "/dev/sdb";
#endif

	dn = dev_name;


	opterr = 0;
	while ((c = getopt (argc, argv, "d:fb:")) != -1)
	{
		switch (c)
		{
			case 'd':
				dn = optarg;
				break;
			case 'f':
				full_format = 1;
				break;
			case 'b':
				full_format = 2;
				fmt_blocks = strtol(optarg, NULL, 10);
				break;
			default:
				Usage(argv[0]);
				return 0;
		}
	}


	errno = 0;
	if( (fd = open(dn, O_RDWR | O_SYNC, 0666)) < 0)
	{
		printf("%s open fail because '%s'\n", dn, strerror(errno));
		exit(-1);
	}

	buf = malloc(INDEX_AREA_SIZE);
	memset(buf, 0, INDEX_AREA_SIZE);

	ist = (INDEX_SECTOR_T *)buf;
	for(i=0; i<INDEX_AREA_SIZE/INDEX_SIZE; i++)
	{
		ist_default(ist);
		ist++;
	}

	printf("format start....\n");

	for(t=0; t<2; t++)
	{
		errno = 0;
		if(lseek64(fd, tab[t], SEEK_SET) < 0)
		{
			printf("seek 0x%llu fail because '%s'\n", tab[t], strerror(errno));
			close(fd);
			free(buf);
			exit(-1);
		}

		errno = 0;
		size_t ws = write(fd, buf, INDEX_AREA_SIZE);
		if(ws != INDEX_AREA_SIZE)
		{
			printf("write fail because '%s' (expect %d bytes but only %d bytes)\n", strerror(errno), INDEX_AREA_SIZE, ws);
			close(fd);
			free(buf);
			exit(-1);
		}
	}

	printf("IndexTable initial OK...\n");

	off64_t tab2 = OFFSET_DATA_BLOCK;
	errno = 0;
	if(lseek64(fd, tab2, SEEK_SET) < 0)
	{
		printf("seek 0x%llu fail because '%s'\n", tab2, strerror(errno));
		close(fd);
		free(buf);
		exit(-1);
	}

	memset(buf, 0, INDEX_AREA_SIZE);
	errno = 0;
	size_t ws = write(fd, buf, INDEX_AREA_SIZE);
	if(ws != INDEX_AREA_SIZE)
	{
		printf("write fail because '%s' (expect %d bytes but only %d bytes)\n", strerror(errno), INDEX_AREA_SIZE, ws);
		close(fd);
		free(buf);
		exit(-1);
	}

	if(full_format == 1)
	{
		disk_size = lseek64(fd, 0, SEEK_END);
		soff = OFFSET_DATA_BLOCK +DATA_MACRO_BLOCK +DATA_BLOCK_SIZE -DATA_TABLE_SIZE;
		fmt_blocks = (disk_size -OFFSET_DATA_BLOCK) /DATA_MACRO_BLOCK;
	}

	if(full_format > 0)
	{
		int i;
		DATA_TABLE_T dtt;
		memset(&dtt, 0, sizeof(DATA_TABLE_T));
		dtt.CheckSum = dtt.Tag = ENDIAN_INT(TAG_DATA_TABLE);

		printf("format data blocks now, please wait a few minutes...\n");
		for(i=0; i<fmt_blocks*(DATA_MACRO_BLOCK/DATA_BLOCK_SIZE); i++)
		{
			lseek64(fd, soff, SEEK_SET);
			errno = 0;
			if(write(fd, &dtt, DATA_TABLE_SIZE) < 0)
			{
				printf("sub data table %d write fail because '%s'\n", i, strerror(errno));
				close(fd);
				free(buf);
				exit(-1);
			}
			if(i%(DATA_MACRO_BLOCK/DATA_BLOCK_SIZE) == 0)
			{
				printf(" block : %3d / %3d\r", (int)(i/(DATA_MACRO_BLOCK/DATA_BLOCK_SIZE) +1), fmt_blocks);
				fflush(stdout);
			}
			soff += DATA_BLOCK_SIZE;
		}
		printf("\nsub_data_table format OK.\n");
	}

	close(fd);
	free(buf);
	printf("Format DONE.\n");

	return 0;
}





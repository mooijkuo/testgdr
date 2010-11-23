#include <stdio.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>


#define	SQ_GPIO "/dev/sq-gpio"

#define GPIO_PORT(x)    ((x & 0x000000FF) << 8)
#define GPIO_PIN(x)     (x & 0x000000FF)

#define SQ_GPIO_IOC_MAGIC       'g'
#define SQ_GPIO_SETHIGH         _IOW(SQ_GPIO_IOC_MAGIC, 0x01, int)
#define SQ_GPIO_SETLOW          _IOW(SQ_GPIO_IOC_MAGIC, 0x02, int)

#define PM 0xc
#define PN 0xb //LED

//G-5, R-6, B-7
#define	LED_GREEN	(GPIO_PORT(PN)| GPIO_PIN(5))
#define LED_RED		(GPIO_PORT(PN)| GPIO_PIN(6))
#define LED_BLUE	(GPIO_PORT(PN)| GPIO_PIN(7))

#define	LED_OFF(cmd)		ioctl(fd_gpio, SQ_GPIO_SETLOW, &(cmd))
#define LED_ON(cmd)	ioctl(fd_gpio, SQ_GPIO_SETHIGH, &(cmd))


int main(int argc, char *argv[])
{
	int fd_gpio;
	unsigned char port, pin, high_low;
	char *hl[] = { "low", "high" };

	if(argc < 4)
	{
		printf("Usage : %s PORT(0-f) PIN(0-f) [high=0|low=1]\n", argv[0]);
		return 0;
	}

	port = strtol(argv[1], NULL, 16);
	pin = strtol(argv[2], NULL, 16);
	switch(argv[3][0])
	{
		case 'h':
		case 'H':
		case '1':
			high_low = 1;
			break;
		default:
			high_low = 0;
			break;
	}

	if(port > 0x0F || pin > 0x0F || high_low > 1)
	{
		printf("something wrong!! port=0x%02x pin=0x%02x high_low=%d\n", port, pin, high_low);
		return 1;
	}

	printf("excute GPIO port=0x%02x pin=0x%02x %s\n", port, pin, hl[high_low]);

	errno = 0;
	if( (fd_gpio=open(SQ_GPIO, O_RDWR)) < 0)
	{
		printf("GPIO open file fail because '%s'\n", strerror(errno) );
		return 1;
	}

	int cmd;
	cmd = (GPIO_PORT(port) | GPIO_PIN(pin));
	errno = 0;
	if(high_low)
	{
		if(ioctl(fd_gpio, SQ_GPIO_SETHIGH, &(cmd)) < 0)
		{
			printf("ioctl() fail because '%s'\n", strerror(errno));
		}
	}
	else
	{
		if(ioctl(fd_gpio, SQ_GPIO_SETLOW, &(cmd)) < 0)
		{
			printf("ioctl() fail because '%s'\n", strerror(errno));
		}
	}

	return 0;
}

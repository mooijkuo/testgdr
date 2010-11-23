#ifndef __GPIOCTL_H__
#define	__GPIOCTL_H__

#include <linux/ioctl.h>

#define	GPIOB_CTL_OUT	0x01
#define	GPIOA_CTL_OUT	0x02
#define	GPIOB_CTL_IN	0x03
#define	GPIOA_CTL_IN	0x04

#define	GPIOCTL_MAJOR_NUMBER	240


#define	GPS_INIT_NULL		0
#define	GPS_INIT_GSENSOR	1	//Aptos. G-sensorㄧ開始就有效
#define	GPS_INIT_GPS		2	//after gps is available

void *thread_gps(void *gpio_pointer);


#endif
                               

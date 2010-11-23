#ifndef LED_BTN_H
#define	LED_BTN_H

#include <sys/ioctl.h>

int GetPIPKey(int fd);

#define	BUZZER_STATUS_INIT				0
#define	BUZZER_STATUS_OVER_SPEED		1
#define	BUZZER_STATUS_GSENSOR			2
#define	BUZZER_STATUS_NO_USBDISK		3
#define	BUZZER_STATUS_START_RECORDING	4
#define	BUZZER_STATUS_SPACE_THRESHOLD	5
#define	BUZZER_STATUS_SPACE_FULL		6
#define BUZZER_STATUS_STREAM_FAIL		10
#define BUZZER_STATUS_OFF				98
#define BUZZER_STATUS_NONE				99

#define	LED_STATUS_INIT					0
#define	LED_STATUS_GPS_NOT_READY		1
#define	LED_STATUS_LIVE_VIEW			2
#define	LED_STATUS_PASSWORD_RESTORE		3
#define	LED_STATUS_PASSWORD_RESTORE_COMPLETE	4
#define	LED_STATUS_GPS_READY			5
#define	LED_STATUS_START_RECORD			6
#define	LED_STATUS_STOP_RECORD			7

int ThreadLEDControl(void *nothing);
int ThreadBuzzerControl(void *nothing);

/* ivan, old code

//LED GPIO MACRO
#define	LED_RED_ON				ioctl(fd,GPIOA_CTL_OUT,0x00000300)
#define	LED_GREEN_ON			ioctl(fd,GPIOA_CTL_OUT,0x00000400)
#define	LED_YELLOW_ON			ioctl(fd,GPIOA_CTL_OUT,0x00000500)
#define	IR_LED_ON					ioctl(fd,GPIOA_CTL_OUT,0x00001c00);	\
													ioctl(fd,GPIOA_CTL_OUT,0x00001d00)


#define	LED_RED_OFF				ioctl(fd,GPIOA_CTL_OUT,0x00000301)
#define	LED_GREEN_OFF			ioctl(fd,GPIOA_CTL_OUT,0x00000401)
#define	LED_YELLOW_OFF		ioctl(fd,GPIOA_CTL_OUT,0x00000501)
#define	IR_LED_OFF				ioctl(fd,GPIOA_CTL_OUT,0x00001c01);	\
													ioctl(fd,GPIOA_CTL_OUT,0x00001d01)
													
//#define	PUSH_BUTTON				ioctl(fd,GPIOA_CTL_IN,0x0000001b)	//std
#define	PUSH_BUTTON				ioctl(fd,GPIOA_CTL_IN,0x0000001c)		//allen

#define	BUTTON_PRESSED		0
#define	BUTTON_RELEASED		1

#define	BUZZER_ON		printf("buzzer on\n"); \
										BuzzerDevice->APIs.MiscAPI.SetBuzzer(BuzzerDevice,1)
										
#define	BUZZER_OFF	printf("buzzer off\n");	\
										BuzzerDevice->APIs.MiscAPI.SetBuzzer(BuzzerDevice,0)

#define GPS_RST_DEVICE   	"/dev/gpioctl"
#define	GPIO_DEVICE			GPS_RST_DEVICE
*/


#define GPS_RST_DEVICE		"/dev/sq-gpio"
#define	GPIO_DEVICE			GPS_RST_DEVICE

#define GPIO_PORT(x)    ((x & 0x000000FF) << 8)
#define GPIO_PIN(x)     (x & 0x000000FF)
#define	GPIO_GET_PORT(x)	((x & 0x0000FF00) >> 8)

#define SQ_GPIO_IOC_MAGIC       'g'
#define SQ_GPIO_SETHIGH         _IOW(SQ_GPIO_IOC_MAGIC, 0x01, int)
#define SQ_GPIO_SETLOW          _IOW(SQ_GPIO_IOC_MAGIC, 0x02, int)

#define PM 0xc
#define PN 0xd //LED

//G-5, R-6, B-7
#define	LED_GREEN	(GPIO_PORT(PN)| GPIO_PIN(5))
#define LED_RED		(GPIO_PORT(PN)| GPIO_PIN(6))
#define LED_BLUE	(GPIO_PORT(PN)| GPIO_PIN(7))
#define LED_YELLOW	(GPIO_PORT(PN)| GPIO_PIN(7))

#define	LED_OFF(cmd)	ioctl(fd_gpio, SQ_GPIO_SETLOW, &(cmd))
#define LED_ON(cmd)		ioctl(fd_gpio, SQ_GPIO_SETHIGH, &(cmd))

//LED GPIO MACRO
#define	IR_LED_ON		ioctl(fd,GPIOA_CTL_OUT,0x00001c00);	\
						ioctl(fd,GPIOA_CTL_OUT,0x00001d00)
#define	IR_LED_OFF		ioctl(fd,GPIOA_CTL_OUT,0x00001c01);	\
						ioctl(fd,GPIOA_CTL_OUT,0x00001d01)
													
//#define	PUSH_BUTTON				ioctl(fd,GPIOA_CTL_IN,0x0000001b)	//std
#define	PUSH_BUTTON				ioctl(fd,GPIOA_CTL_IN,0x0000001c)		//allen

#define	BUTTON_PRESSED		0
#define	BUTTON_RELEASED		1

#if 0 //ivan old code
#define	BUZZER_ON		printf("buzzer on\n"); \
						BuzzerDevice->APIs.MiscAPI.SetBuzzer(BuzzerDevice,1)
										
#define	BUZZER_OFF		printf("buzzer off\n");	\
						BuzzerDevice->APIs.MiscAPI.SetBuzzer(BuzzerDevice,0)
#else

#define BUZZER_ON		ioctl(fd_pwm, SQ_PWM_START)
#define BUZZER_OFF		ioctl(fd_pwm, SQ_PWM_STOP)

/*
if(fd_pwm > 0)
{
	err=ioctl(fd_pwm, SQ_PWM_START);
	if(err) {
		printf("[02]io error=%d\n", err);
	}
	usleep(500*1000);
	err=ioctl(fd_pwm, SQ_PWM_STOP);
	if(err) {
		printf("[03]io error=%d\n", err);
	}
	usleep(500*1000);
}
*/

#endif



#endif

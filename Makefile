

ifndef CROSS
CROSS=/opt/arm-2009q1/bin/arm-none-linux-gnueabi-
endif

CC = $(CROSS)gcc
CPP = $(CROSS)g++
LD = $(CROSS)ld
STRIP = $(STRIP)strip

STRIPTOOL = $(CROSS)strip
STRIP = $(STRIPTOOL) --remove-section=.note --remove-section=.comment $(NAME)

AR = $(CROSS)ar


CFLAGS = -pthread -Wall
CFLAGS += -O2
#CFLAGS += -g2
#CFLAGS += -static
CFLAGS += -DIVAN_DEBUG
CFLAGS += -DAUTO_SPACE

SQFLAGS = -Wall
SQFLAGS += -DTARGET_SQ

LDFLAGS	= -lm
LIBS :=

#DEPEND_FILES = systemmsg.o gpiobctl.o RTSPc.o gdr.o HTTPc.o
#PROG = gdr.sq
DEPEND_FILES = systemmsg.o gpiobctl.o RTSPc.o testgdr.o HTTPc.o
PROG = testgdr


$(PROG): $(DEPEND_FILES)
	$(CC) -o $@ $(CFLAGS) $(DEPEND_FILES) $(LDFLAGS) $(LIBS)
#	$(STRIP) -s $@

#gdr-d: $(DEPEND_FILES)
#	$(CC) -o $@ $(CFLAGS) $(DEPEND_FILES) $(LDFLAGS) $(LIBS)
gdr-d: $(DEPEND_FILES)
	$(CC) -o $@ $(CFLAGS) $(DEPEND_FILES) $(LDFLAGS) $(LIBS)


.c.o:
	$(CC) -c -o $@ $(CFLAGS) $<

util: fmt istr led us

fmt: fmt.o
	$(CC) -o fmt.sq fmt.c $(SQFLAGS)

istr: istr.o
	$(CC) -o istr.sq istr.c $(SQFLAGS)

led: led.o
	$(CC) -o led.sq led.c $(SQFLAGS)

us: usb_setting.o
	$(CC) -o us.sq usb_setting.c $(SQFLAGS)

#us: usb_setting.o
#	$(CPP) -o us.sq usb_setting.cpp $(SQFLAGS) -static

clean:
	-@\rm -rf $(PROG) *~ a.out *.o
	-@\rm fmt.sq istr.sq led.sq us.sq



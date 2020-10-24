#Cross compiler definition
CC = $(CROSS_COMPILE)gcc
RM = rm

#-Wall
CFLAGS  = -g -std=gnu99 -Wall -Werror

CFLAGS += -I/opt/redpitaya/include/redpitaya
CFLAGS += /root/RedPitaya/api/src/kiss_fft/kiss_fft.c -lm
CFLAGS += /root/RedPitaya/api/src/kiss_fft/kiss_fftr.c -lm
CFLAGS += -I/root/RedPitaya/api/src/kiss_fft
LDFLAGS = -L/opt/redpitaya/lib
LDLIBS = -lm -lpthread -lrp

CFLAGS += -DFIXED_POINT=32
CFLAGS += -DDEBUG

SRCS=$(wildcard *iic.c)
OBJS=$(SRCS:.c=)

all: $(OBJS)

%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

clean:
	$(RM) *.o
	$(RM) $(OBJ)

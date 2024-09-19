TARGET=aesdsocket

CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -O3 -Wall -Wextra -pedantic -ggdb3 -DUSE_AESD_CHAR_DEVICE # To get where leaks are
LDFLAGS ?= -lpthread

SRC := $(wildcard *.c)
OBJS :=  $(SRC:.c=.o)

all: $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) -c -I. $< -o $@
	
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

clean:
	rm -f *.o
	rm -f *~
	rm -f $(TARGET)
  
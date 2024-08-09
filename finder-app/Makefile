TARGET=writer

CC=$(CROSS_COMPILE)gcc
CFLAGS=-O3 -Wall -Wextra -pedantic

OBJS = $(TARGET).o

all: $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@
	
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

clean:
	rm -f *.o
	rm -f *~
	rm -f $(TARGET)
  
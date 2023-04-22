SRCS=curse.c
OBJS=$(SRCS:.c=.o)
CFLAGS=-O3
LDLIBS=-lcurses -lpthread

.PHONY: all clean

all: curse

clean:
	$(RM) curse $(OBJS)

TRG = psu
PREFIX=/usr
BINDIR=$(PREFIX)/bin
CC = gcc
SRCS = $(TRG).c
OBJS = $(SRCS:.c=.o)

CFLAGS += -W -Wall -O2
LDFLAGS += -s

all: $(TRG)

$(TRG): $(OBJS)

clean:
	$(RM) $(TRG) *~ *.o jj.jj *.so *.so.*

install: $(TRG)
	install --directory $(DESTDIR)$(BINDIR)
	install $(TRG) $(DESTDIR)$(BINDIR)

uninstall:
	$(RM) $(DESTDIR)$(BINDIR)/$(TRG)

%.o: %.c
	$(COMPILE.c) $(DIRFLAGS) $(OUTPUT_OPTION) $<


OUTDIR=rel
CFLAGS=-D__LINUX__ -Wall
LDFLAGS=-lpthread -lrt

ifneq ($(strip $(rel)), 1)
CFLAGS += -D_DEBUG -g
#CFLAGS += -D_DEBUG_RUDP
OUTDIR=dbg
else
CFLAGS += -O2
endif

ifeq ($(strip $(target)), android)
CROSS=arm-linux-androideabi-
OUTDIR := $(addsuffix -android, $(OUTDIR))
LDFLAGS=-lm -c
endif

CC=$(CROSS)gcc
AR=$(CROSS)ar
LDFLAGS+= -L$(OUTDIR) -lrudp

LIBSRC = rudp.c platform_adpt.c crc32.c rudp.h rudp_imp.h	#rudp_timer.c
LIBOBJS = $(patsubst %.c, $(OUTDIR)/%.o, $(filter %.c, $(LIBSRC)))

TARGETS=*.o *.a rudpsvr rudpclt simulconn
TARGETS := $(addprefix $(OUTDIR)/, $(TARGETS))

.PHONY: clean chkdir

all: chkdir $(OUTDIR)/librudp.a

test: chkdir $(OUTDIR)/rudpsvr $(OUTDIR)/rudpclt $(OUTDIR)/simulconn $(OUTDIR)/nblk_svr \
	$(OUTDIR)/rudpsels $(OUTDIR)/rudpselc $(OUTDIR)/rudpsel #$(OUTDIR)/simconn


$(OUTDIR)/nblk_svr: $(OUTDIR)/librudp.a $(OUTDIR)/nblk_svr.o
	$(CC) -o $@ $^ $(LDFLAGS)

$(OUTDIR)/rudpsvr: $(OUTDIR)/librudp.a $(OUTDIR)/rudpsvr.o
	$(CC) -o $@ $^ $(LDFLAGS) 

$(OUTDIR)/rudpclt: $(OUTDIR)/librudp.a $(OUTDIR)/rudpclt.o
	$(CC) -o $@ $^ $(LDFLAGS)

$(OUTDIR)/simulconn: $(OUTDIR)/librudp.a $(OUTDIR)/simulconn.o
	$(CC) -o $@ $^ $(LDFLAGS)

$(OUTDIR)/librudp.a: $(LIBOBJS) 
	$(AR) -r $@ $^

$(OUTDIR)/simconn: $(OUTDIR)/librudp.a $(OUTDIR)/simconn.o
	$(CC) -o $@ $^ $(LDFLAGS) 

$(OUTDIR)/punch: $(OUTDIR)/librudp.a $(OUTDIR)/rudp_punch.o
	$(CC) -o $@ $^ $(LDFLAGS)

$(OUTDIR)/rudpsels: $(OUTDIR)/librudp.a $(OUTDIR)/rudpsels.o
	$(CC) -o $@ $^ $(LDFLAGS)

$(OUTDIR)/rudpsel: $(OUTDIR)/librudp.a $(OUTDIR)/rudpsel.o
	$(CC) -o $@ $^ $(LDFLAGS)

$(OUTDIR)/rudpselc: $(OUTDIR)/librudp.a $(OUTDIR)/rudpselc.o
	$(CC) -o $@ $^ $(LDFLAGS)

#$(LIBOBJS): $(LIBSRC)

$(OUTDIR)/%.o: %.c
	$(CC) -c -o $@ $^ $(CFLAGS)


clean: 
	rm -f $(TARGETS)

chkdir:
	@if [ ! -d $(OUTDIR) ]; then mkdir $(OUTDIR); fi

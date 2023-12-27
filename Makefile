# Makefile for longmynd

SRC = main.c nim.c ftdi.c stv0910.c stv0910_utils.c stvvglna.c stvvglna_utils.c stv6120.c stv6120_utils.c ftdi_usb.c fifo.c udp.c beep.c ts.c libts.c mymqtt.c pcrpts.c
OBJ = ${SRC:.c=.o}

# Should only be used when compiling on pluto...
# pluto=1

ifdef pluto

#FOR 0.37
CROSS_COMPILE=arm-linux-
SYSROOT=/home/linuxdev/prog/pluto/pluto-ori/pluto-ori-frm/37plutosdr-fw/buildroot/output/staging
PAPR_ORI=/home/linuxdev/prog/pluto/pluto-ori/pluto-ori-frm/pluto-buildroot/board/pluto/overlay/root/datv

CXX=$(CROSS_COMPILE)g++
CC=$(CROSS_COMPILE)gcc
HOST_DIR=/home/linuxdev/prog/pluto/firm033/pluto_radar/plutosdr-fw/buildroot/output/host
CFLAGS = -fpermissive -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable -O2 -mfpu=neon --sysroot=$(SYSROOT) -mfloat-abi=hard
else
CXX=g++
CC=gcc
CFLAGS = -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable -g -O2 -fpermissive
endif
# Build parallel
MAKEFLAGS += -j$(shell nproc || printf 1)


#CFLAGS += -Wall -Wextra -Wpedantic -Wunused -DVERSION=\"${VER}\" -pthread -D_GNU_SOURCE
LDFLAGS += -lusb-1.0 -lm -lasound -lpthread -lmosquitto -lcivetweb -lz

all: _print_banner longmynd fake_read ts_analyse

debug: COPT = -Og
debug: CFLAGS += -ggdb -fno-omit-frame-pointer
debug: all

werror: CFLAGS += -Werror
werror: all

_print_banner:
	@echo "Compiling longmynd with $(CXX) $(shell $(CXX) -dumpfullversion) on $(shell $(CXX) -dumpmachine)"

fake_read: fake_read.c
	@echo "  CC     "$@
	@${CC} fake_read.c -o $@

ts_analyse: ts_analyse.c libts.o
	@echo "  CXX     "$@
	@${CXX} ${CFLAGS} ts_analyse.c libts.o -o $@

longmynd: ${OBJ}
	@echo "  LD     "$@
	@${CXX} ${COPT} ${CFLAGS} -o $@ ${OBJ} ${LDFLAGS}

%.o: %.c
	@echo "  CXX     "$<
	@${CXX} ${COPT} ${CFLAGS} -c -fPIC -o $@ $<

clean:
	@rm -rf longmynd fake_read ts_analyse ${OBJ}

install:	
	cp longmynd $(PAPR_ORI)

.PHONY: all clean


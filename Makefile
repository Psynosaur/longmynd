# Makefile for longmynd

SRC = main.c \
      nim.c \
      ftdi.c \
      ftdi_dual.c \
      ftdi_usb.c \
      stv0910.c \
      stv0910_utils.c \
      stv6120.c \
      stv6120_utils.c \
      stvvglna.c \
      stvvglna_utils.c \
      fifo.c \
      udp.c \
      beep.c \
      ts.c \
      tuner2.c \
      libts.c \
      mymqtt.c \
      pcrpts.c \
      register_logging.c \
      json_output.c
OBJ = ${SRC:.c=.o}

ifeq ($(env),local)
#FOR 0.37
#CROSS_COMPILE=arm-linux-
#SYSROOT=/home/linuxdev/prog/pluto/pluto-ori/pluto-ori-frm/37plutosdr-fw/buildroot/output/staging
#PAPR_ORI=/home/linuxdev/prog/pluto/pluto-ori/pluto-ori-frm/pluto-buildroot/board/pluto/overlay/root/datv
# TO BE UNCOMENTED for NATIVE
#CROSS_COMPILE = arm-linux-gnueabihf-
#SYSROOT=/home/linuxdev/prog/pluto/pluto-ori/pluto-ori-frm/f5oeo38plutosdr-fw/buildroot/output/staging
#TOOLS_PATH = PATH="/home/linuxdev/prog/pluto/pluto-ori/pluto-ori-frm/f5oeo38plutosdr-fw/buildroot/output/host/bin:/home/linuxdev/prog/pluto/pluto-ori/pluto-ori-frm/f5oeo38plutosdr-fw/buildroot/output/host/sbin:$(PATH)"


CXX=$(CROSS_COMPILE)g++
CC=$(CROSS_COMPILE)gcc
#HOST_DIR=/home/linuxdev/prog/pluto/firm033/pluto_radar/plutosdr-fw/buildroot/output/host
CFLAGS = -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable -O2 -mfpu=neon -mfloat-abi=hard
else
CXX=g++
CC=gcc
CFLAGS = -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable -g -O2
endif
# Build parallel
MAKEFLAGS += -j$(shell nproc || printf 1)


#CFLAGS += -Wall -Wextra -Wpedantic -Wunused -DVERSION=\"${VER}\" -pthread -D_GNU_SOURCE
LDFLAGS += -lusb-1.0 -lm -lasound -lpthread -lmosquitto -lcivetweb -lz

VERSION=$(shell git describe --always --tags)#Get version 


all: _print_banner longmynd fake_read ts_analyse archive

debug: COPT = -Og
debug: CFLAGS += -ggdb -fno-omit-frame-pointer
debug: all

werror: CFLAGS += -Werror
werror: all

_print_banner:
	@$(TOOLS_PATH) echo "Compiling longmynd with $(CXX) $(shell $(CXX) -dumpfullversion) on $(shell $(CXX) -dumpmachine)"

fake_read: fake_read.c
	@echo "  CC     "$@
	@$(TOOLS_PATH) ${CC} fake_read.c -o $@

ts_analyse: ts_analyse.c libts.o
	@echo "  CXX     "$@
	@$(TOOLS_PATH) ${CXX} ${CFLAGS} ts_analyse.c libts.o -o $@

longmynd: ${OBJ}
	@echo "  LD     "$@
	@$(TOOLS_PATH) ${CXX} ${COPT} ${CFLAGS} -o $@ ${OBJ} ${LDFLAGS}

%.o: %.c
	@echo "  CXX     "$<
	@$(TOOLS_PATH) ${CXX} ${COPT} ${CFLAGS} -c -fPIC -o $@ $<

clean:
	@rm -rf longmynd fake_read ts_analyse ${OBJ}

install:	
	cp longmynd $(PAPR_ORI)

archive: longmynd
	mkdir -p Release && zip -r Release/longmynd-fw-$(VERSION).zip longmynd


.PHONY: all clean


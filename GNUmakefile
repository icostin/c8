N:=c8
D:=C8

SRC:=c8.c

DEVDIR:=$(abspath $(or $(DEVDIR),../targets))

TARGET:=$(shell $(CC) -v 2>&1 | grep "^Target: " | sed "s/^Target: //")
TCPFX:=$(CC:%gcc=%)
TCPFX:=$(TCPFX:%clang=%)
TCPFX:=$(TCPFX:%cc=%)
AR:=$(TCPFX)ar
STRIP:=$(TCPFX)strip
CFG:=$(or $(CFG),release)
O:=$(DEVDIR)/$(TARGET)-$(CFG)
B:=/tmp/$(shell whoami)-$(TARGET)-$(CFG)-$N
LIBPFX:=$(if $(findstring mingw,$(CC)),,lib)
LIBEXT:=$(if $(findstring mingw,$(CC)),.dll,.so)
EXEEXT:=$(if $(findstring mingw,$(CC)),.exe,)

EXEDYN:=$B/$N$(EXEEXT)
EXESTA:=$B/$Ns$(EXEEXT)
CF:=-std=c99 -Wall -Werror -Wextra -fvisibility=hidden -I$O/include

DYNCF:=$(CF)
STACF:=$(CF)

CF_debug:=-O0 -D_DEBUG
CF_release:=-O3 -ffast-math -fomit-frame-pointer -DNDEBUG

OK_MSG=[32mOK[0m
FAILED_MSG=[31;1mFAILED[22m
NORMAL_COLOR=[0m
PROJECT_COLOR=[33;1m
TARGET_COLOR=[35;1m
IMSG="$(PROJECT_COLOR)$(N) $(TARGET_COLOR)$(TARGET)-$(CFG)$(NORMAL_COLOR)"

.PHONY: all install clean

all: $(EXEDYN) $(EXESTA)

install: all
	mkdir -p $O/bin
	cp -f $(EXEDYN) $O/bin/
	cp -f $(EXESTA) $O/bin/
	@echo $(IMSG)

clean:
	-rm -rf $B

$B:
	mkdir -p $@
	chmod 700 $@

$(patsubst %.c,$B/%-dyn.o,$(SRC)): $B/%-dyn.o: %.c | $B
	$(CC) -c -o $@ $< $(DYNCF) $(CF_$(CFG))


$(patsubst %.c,$B/%-sta.o,$(SRC)): $B/%-sta.o: %.c | $B
	$(CC) -c -o $@ $< $(STACF) $(CF_$(CFG))

$(EXEDYN): $(SRC) | $B
	$(CC) -o $@ $< -I. $(DYNCF) $(CF_$(CFG)) -L$O/lib -lc42 -lc42svc -lc42clia

$(EXESTA): $(SRC) | $B
	$(CC) -static -o $@ $< -D$D_STATIC -DC42_STATIC -DC42SVC_STATIC -I. $(STACF) $(CF_$(CFG)) -L$O/lib -lc42 -lc42svc -lc42clia


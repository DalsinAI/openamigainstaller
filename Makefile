# OpenInstaller on the host: the interpreter with the text front end, for
# the tests (make test). On the Amiga (make amiga, as
# package/amiga/build-ci.sh does with everything pinned): 68040, soft float,
# the OpenLayout and GadTools windows, libcurl and gfopentls.library.
CC ?= cc
CFLAGS ?= -O2 -g
CFLAGS += -std=c99 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Wno-unused-parameter
CURL_LIBS ?= $(shell pkg-config --libs libcurl 2>/dev/null || echo -lcurl)
ifeq ($(NO_CURL),1)
CFLAGS += -DOI_NO_CURL
CURL_LIBS =
endif

SRC = src/main.c src/oi_eval.c src/oi_parse.c src/oi_fn.c src/oi_fn_core.c \
      src/oi_fn_ask.c src/oi_fn_file.c src/oi_fn_extra.c src/oi_sys_common.c \
      src/oi_sys_posix.c src/oi_net_curl.c src/oi_sha256.c src/ui_text.c src/ui_pages.c

openinstaller: $(SRC) src/*.h
	$(CC) $(CFLAGS) -o $@ $(SRC) $(CURL_LIBS)

test: openinstaller
	sh tests/run.sh ./openinstaller

asan: $(SRC) src/*.h
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer -o openinstaller-asan $(SRC) $(CURL_LIBS)
	sh tests/run.sh ./openinstaller-asan

# ---- The Amiga -------------------------------------------------------------
AMIGA_CC     ?= m68k-amigaos-gcc
AMIGA_SDK    ?= build-amiga/sdk
AMIGA_LAYOUT ?= build-amiga/gfopenlayout
AMIGA_CURL   ?= build-amiga/net
AMIGA_SPECS  ?= $(AMIGA_SDK)/Lib/m68k-soft-m68040.specs
AMIGA_DATE   ?= $(shell date +%-d.%-m.%Y)
AMIGA_OPT    ?= -Os -m68040 -mtune=68040 -fomit-frame-pointer -fno-strict-aliasing
AMIGA_CFLAGS = $(AMIGA_OPT) -noixemul -specs=$(AMIGA_SPECS) -std=gnu99 -Wall -Wextra -Werror -Wno-unused-parameter \
                -isystem $(AMIGA_SDK)/OpenNDK/Include_H -isystem $(AMIGA_SDK)/Include -I$(AMIGA_LAYOUT)/include \
                -DOI_DATE='"$(AMIGA_DATE)"'
AMIGA_SRC = src/main.c src/oi_eval.c src/oi_parse.c src/oi_fn.c src/oi_fn_core.c \
            src/oi_fn_ask.c src/oi_fn_file.c src/oi_fn_extra.c src/oi_sys_common.c \
            src/oi_sys_amiga.c src/oi_net_curl.c src/oi_sha256.c src/ui_text.c src/ui_pages.c \
            src/ui_olui.c src/ui_gadtools.c src/panel_amiga.c build-amiga/logo_png.c
ifeq ($(NO_CURL),1)
AMIGA_NET =
AMIGA_CFLAGS += -DOI_NO_CURL
else
AMIGA_CFLAGS += -I$(AMIGA_CURL)/include -DCURL_STATICLIB
AMIGA_NET = $(AMIGA_CURL)/lib/libcurl.a $(AMIGA_CURL)/lib/libnghttp2.a $(AMIGA_CURL)/lib/libz.a -latomic -lpthread
endif

amiga: OpenInstaller.68k

build-amiga/logo_png.c: assets/ac-logo.png tools/bin2c.py
	mkdir -p build-amiga
	python3 -I tools/bin2c.py assets/ac-logo.png oi_logo_png > $@

OpenInstaller.68k: $(AMIGA_SRC) src/*.h
	$(AMIGA_CC) $(AMIGA_CFLAGS) -o $@ $(AMIGA_SRC) $(AMIGA_NET) -lamiga -lm

clean:
	rm -f openinstaller openinstaller-asan OpenInstaller.68k build-amiga/logo_png.c

.PHONY: openinstaller test asan clean amiga OpenInstaller.68k

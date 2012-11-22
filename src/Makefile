# /******************************************************
#  Talamasca
#  by Jeroen Massar <jeroen@unfix.org>
#  (C) Copyright Jeroen Massar 2004 All Rights Reserved
#  http://unfix.org/projects/talamasca/
# *******************************************************
# $Author: $
# $Id: $
# $Date: $
# ******************************************************/
# Source Makefile for talamasca - Jeroen Massar <jeroen@unfix.org>
#
# TALAMASCA_VERSION and TALAMASCA_OPTIONS need to be defined, gets done by toplevel Makefile
#
# One should make this using the main Makefile (thus one dir up)

BINS	= talamasca
SRCS	= talamasca.c linklist.c common.c server.c user.c channel.c config.c hash_md5.c
INCS	= talamasca.h linklist.h
DEPS	= ../Makefile Makefile
OBJS	= talamasca.o linklist.o common.o server.o user.o channel.o config.o hash_md5.o
WARNS	= -W -Wall -pedantic -Wno-format -Wno-unused
EXTRA   = -g3
CFLAGS	= $(WARNS) $(EXTRA) -D_GNU_SOURCE -D'TALAMASCA_VERSION="$(TALAMASCA_VERSION)"' $(TALAMASCA_OPTIONS)
LDFLAGS	=
COMPILE	= @echo "* Compiling to $@"; gcc -c $(CFLAGS)
LINK	= @echo "* Linking $@"; gcc $(CFLAGS)
RM	= @echo "* Removing $@"; rm

# Stripping engine
ifeq ($(shell echo $(TALAMASCA_OPTIONS) | grep -c "DEBUG"),1)
STRIP	=
else
STRIP	= @echo "* Stripping $@"; strip $@
endif

all:	$(BINS)

talamasca:	$(OBJS) ${INCS} ${DEPS}
	$(LINK) -o $@ $(OBJS) $(LDFLAGS)
	$(STRIP)

$(OBJS): %.o: %.c $(DEPS) ${INCS}
	$(COMPILE) $< -o $@

clean:
	$(RM) -f $(OBJS) $(BINS)

# Mark targets as phony
.PHONY : all clean


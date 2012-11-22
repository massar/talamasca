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
#
# Toplevel Makefile allowing easy distribution.
# Use this makefile for doing almost anything
# 'make help' shows the possibilities
#

# The name of the application
TALAMASCA=talamasca

# The version of this release
TALAMASCA_VERSION=2004.10.17
export TALAMASCA_VERSION

# TALAMASCA Compile Time Options
# Append one of the following option on wish to
# include certain features, -O3 is the default
#
# Optimize             : -O3
# Enable Debugging     : -DDEBUG
TALAMASCA_OPTIONS=-O9 -DDEBUG

# Export it to the other Makefile
export TALAMASCA_OPTIONS

# Tag it with debug when it is run with debug set
ifeq ($(shell echo $(TALAMASCA_OPTIONS) | grep -c "DEBUG"),1)
TALAMASCA_VERSION:=$(TALAMASCA_VERSION)-debug
endif

# Do not print "Entering directory ..."
MAKEFLAGS += --no-print-directory

# Misc bins, making it easy to quiet them :)
RM=@rm -f
MV=@mv
MAKE:=@${MAKE}
CP=@echo [Copy]; cp
RPMBUILD=@echo [RPMBUILD]; rpmbuild
RPMBUILD_SILENCE=>/dev/null 2>/dev/null

# Configure a default RPMDIR
ifeq ($(shell echo "${RPMDIR}/" | grep -c "/"),1)
RPMDIR=/usr/src/redhat/
endif

# Change this if you want to install into another dirtree
# Required for eg the Debian Package builder
DESTDIR=

# Get the source dir, needed for eg debsrc
SOURCEDIR := $(shell pwd)
SOURCEDIRNAME := $(shell basename `pwd`)

# Paths
sbindir=/usr/sbin/
srcdir=src/

all:	${srcdir}
	$(MAKE) -C src all

help:
	@echo "Talamasca"
	@echo "Website: http://unfix.org/projects/talamasca/"
	@echo "Author : Jeroen Massar <jeroen@unfix.org>"
	@echo
	@echo "Makefile targets:"
	@echo "all      : Build everything"
	@echo "help     : This little text"
	@echo "install  : Build & Install"
	@echo "clean    : Clean the dirs to be pristine in bondage"
	@echo
	@echo "Distribution targets:"
	@echo "dist     : Make all distribution targets"
	@echo "tar      : Make source tarball (tar.gz)"
	@echo "bz2      : Make source tarball (tar.bz2)"
	@echo "deb      : Make Debian binary package (.deb)"
	@echo "debsrc   : Make Debian source packages"
	@echo "rpm      : Make RPM package (.rpm)"
	@echo "rpmsrc   : Make RPM source packages"

install: all
	mkdir -p $(DESTDIR)${sbindir}
	${CP} src/$(TALAMASCA) $(DESTDIR)${sbindir}

# Clean all the output files etc
distclean: clean

clean: debclean
	$(MAKE) -C src clean

# Generate Distribution files
dist:	tar bz2 deb debsrc rpm rpmsrc

# tar.gz
tar:	clean
	-${RM} ../talamasca_${TALAMASCA_VERSION}.tar.gz
	tar -zclof ../talamasca_${TALAMASCA_VERSION}.tar.gz *

# tar.bz2
bz2:	clean
	-${RM} ../talamasca_${TALAMASCA_VERSION}.tar.bz2
	tar -jclof ../talamasca_${TALAMASCA_VERSION}.tar.bz2 *

# .deb
deb:	clean
	# Copy the changelog
	${CP} doc/changelog debian/changelog
	debian/rules binary
	${MAKE} clean

# Source .deb
debsrc: clean
	# Copy the changelog
	${CP} doc/changelog debian/changelog
	cd ..; dpkg-source -b ${SOURCEDIR}; cd ${SOURCEDIR}
	${MAKE} clean

# Cleanup after debian
debclean:
	if [ -f configure-stamp ]; then debian/rules clean; fi;

# RPM
rpm:	rpmclean tar
	-${RM} ${RPMDIR}/RPMS/i386/talamasca-${TALAMASCA_VERSION}*.rpm
	${RPMBUILD} -tb --define 'talamasca_version ${TALAMASCA_VERSION}' ../talamasca_${TALAMASCA_VERSION}.tar.gz ${RPMBUILD_SILENCE}
	${MV} ${RPMDIR}/RPMS/i386/talamasca-*.rpm ../
	@echo "Resulting RPM's:"
	@ls -l ../talamasca-${TALAMASCA_VERSION}*.rpm
	${MAKE} clean
	@echo "RPMBuild done"

rpmsrc:	rpmclean tar
	-${RM} ${RPMDIR}/RPMS/i386/talamasca-${TALAMASCA_VERSION}*src.rpm
	${RPMBUILD} -ts --define 'talamasca_version ${PROJECT_VERSION}' ../talamasca_${TALAMASCA_VERSION}.tar.gz ${RPMBUILD_SILENCE}
	${MV} ${RPMDIR}/RPMS/i386/talamasca-*.src.rpm ../
	@echo "Resulting RPM's:"
	@ls -l ../talamasca-${TALAMASCA_VERSION}*.rpm
	${MAKE} clean}
	@echo "RPMBuild-src done"

rpmclean: clean
	-${RM} ../${PROJECT}_${PROJECT_VERSION}.tar.gz

# Mark targets as phony
.PHONY : all install help clean dist tar bz2 deb debsrc debclean rpm rpmsrc


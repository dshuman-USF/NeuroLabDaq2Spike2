
#DEBUG_OR_NOT= -g -ggdb3
DEBUG_OR_NOT= -O2
AUTOMAKE_OPTIONS= -Wno-portability

MSWIN_DIR=mswin

AM_CXXFLAGS=
AM_CPPFLAGS = $(DEBUG_OR_NOT) -DVERSION=\"$(VERSION)\"
AM_CFLAGS = $(DEBUG_OR_NOT) -Wall -std=c99 
AM_FFLAGS = -fno-underscoring -Wall -frecord-marker=4 -fconvert=big-endian

noinst_PROGRAMS = local_daq2spike2
bin_PROGRAMS = daq2spike2 read_spike cyg2daq cyg_fixup cyg2cyg25KHz \
					print_cygdate edt_split anfixbdt4spike2 edt2spike2 edt2spike2.exe

dist_bin_SCRIPTS = bdt_fix.py

read_spike_SOURCES = read_spike.cpp
local_daq2spike2_SOURCES = local_daq2spike2.cpp local_daq2spike2.h
daq2spike2_SOURCES = daq2spike2.cpp 
cyg2daq_SOURCES = cyg2daq.cpp
cyg2cyg25KHz_SOURCES = cyg2cyg25KHz.cpp
cyg_fixup_SOURCES = cyg_fixup.cpp
print_cygdate_SOURCES = print_cygdate.cpp
edt_split_SOURCES = edt_split.cpp
edt2spike2_SOURCES = edt2spike2.cpp edt2spike2_win.pro Makefile.am
anfixbdt4spike2_SOURCES = anfixbdt4spike2.f

dist_doc_DATA = daq2spike2.odt daq2spike2.pdf daq2spike2.doc ChangeLog COPYING LICENSE COPYRIGHTS README

checkin_files = $(read_spike_SOURCES) \
					  $(local_daq2spike2_SOURCES) \
 					  $(daq2spike2_SOURCES) \
                 $(cyg2daq_SOURCES) \
                 $(cyg2cyg25KHz_SOURCES) \
                 $(cyg_fixup_SOURCES) \
                 $(print_cygdate_SOURCES) \
					  $(edt_split_SOURCES) \
					  $(edt2spike2_SOURCES) \
					  $(dist_doc_DATA)

EXTRA_DIST = debian cyg_upscale.m

$(bin_PROGRAMS): Makefile

BUILT_SOURCES = Makefile_edt2spike2_win.qt

CLEANFILES = ${BUILT_SOURCES}) \
				 ${MSWIN_DIR}/*.o

release:
	make checkin_release
	make clean
	make

clean:
	@rm -f $(noinst_PROGAMS) $(bin_PROGRAMS) *.o

mswin:
	@mkdir -p $(MSWIN_DIR)

Makefile_edt2spike2_win.qt: edt2spike2_win.pro Makefile.am
edt2spike2.exe$(EXEEXT): mswin Makefile_edt2spike2_win.qt $(edt2spike2_SOURCES)
 ifdef MXE_QMAKE
	make -f Makefile_edt2spike2_win.qt
 else
	@echo MXE environment not installed, windows program not built
 endif

simbuild.exe$(EXEEXT): mswin simbuild.pro Makefile_simbuild_win.qt $(simbuild_SOURCES)

local_daq2spike2_CXXFLAGS = $(DEBUG_OR_NOT) -Wall -std=gnu++17 -pipe -Wall -W -D_REENTRANT -fPIC ${DEFINES}

read_spike_CXXFLAGS = $(DEBUG_OR_NOT) -Wall -std=gnu++17 -pipe -Wall -W -D_REENTRANT -fPIC ${DEFINES}

daq2spike2_CXXFLAGS = $(DEBUG_OR_NOT) -Wall  -std=gnu++17 -pipe -Wall -W -D_REENTRANT -fPIC ${DEFINES} 
daq2spike2_LDADD = -lson64 -lpthread

cyg2daq_CXXFLAGS = $(DEBUG_OR_NOT) -Wall -std=gnu++17 -pipe -Wall -W -D_REENTRANT -fPIC  ${DEFINES}

cyg2cyg25KHz_CXXFLAGS = $(DEBUG_OR_NOT) -Wall -std=gnu++17 -pipe -Wall -W -D_REENTRANT -fPIC ${DEFINES} 

cyg_fixup_CXXFLAGS = $(DEBUG_OR_NOT) -Wall -std=gnu++17 -pipe -Wall -W -D_REENTRANT -fPIC ${DEFINES} 
cyg_fixup_LDADD = -lson64 -lpthread

print_cygdate_CXXFLAGS = $(DEBUG_OR_NOT) -Wall -std=gnu++17 -pipe -Wall -W -D_REENTRANT -fPIC ${DEFINES} 

edt_split_CXXFLAGS = $(DEBUG_OR_NOT) -Wall -std=gnu++17 -pipe -Wall -W -D_REENTRANT -fPIC ${DEFINES} 

edt2spike2_CXXFLAGS = $(DEBUG_OR_NOT) -Wall -std=gnu++17 -pipe -Wall -W -D_REENTRANT -fPIC ${DEFINES} 
edt2spike2_LDFLAGS = -pthread
edt2spike2_LDADD = $(LDDADD) -lson64

checkin_release:
	git add $(checkin_files) Makefile.am configure.ac && git -uno -S commit -m "Release files for version $(VERSION)"

checkpoint:
	git add $(checkin_files) Makefile.am configure.ac && git commit -uno -S -q -m "checkpoint files for version $(VERSION)"

checkpoint_withcomment:
	git add $(checkin_files) Makefile.am configure.ac && git commit -uno -S -q 

deb:
	@echo 'Making debian packages'
	make distdir &&\
	cd $(distdir) &&\
	dpkg-buildpackage -Zgzip -uc -us && \
	cd .. && \
	rm -rf $(distdir)


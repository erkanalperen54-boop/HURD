## Process this file with automake to produce Makefile.in.

libexec_PROGRAMS = migcom
bin_SCRIPTS = mig

AM_YFLAGS = -d

EXTRA_DIST = gensym.awk cpu.sym

CLEANFILES = cpu.h *.sym[co] lexxer.c parser.c parser.h

SUBDIRS = tests

AWK_V = $(AWK_V_$(V))
AWK_V_ = $(AWK_V_$(AM_DEFAULT_VERBOSITY))
AWK_V_0 = @echo "  AWK    $@";

migcom_SOURCES = error.c error.h global.c global.h			\
		 header.c lexxer.h lexxer.l message.h mig_string.h	\
		 migcom.c parser.h parser.y routine.c routine.h		\
		 server.c statement.c statement.h string.c		\
		 type.c type.h user.c utils.c utils.h write.h
migcom_LDADD = @LEXLIB@

SUFFIXES = .h .symo .symc .sym

.sym.symc:
	$(AWK_V) $(AWK) -f $(srcdir)/gensym.awk $< > $@
.symc.symo:
	$(AM_V_CC) $(TARGET_CC) -S $(TARGET_CPPFLAGS) $(TARGET_CFLAGS) -ffreestanding -x c -o $@ $<
.symo.h:
	$(AM_V_GEN) sed -e '/^[^*].*$$/d' -e 's/^[*]/#define/' -e 's/mAgIc[^-0-9]*//' \
	    $< > $@
cpu.symc: $(srcdir)/gensym.awk

lexxer.c: parser.h

$(migcom_OBJECTS): cpu.h

#
# Building a distribution.
#

dist-hook: gen-ChangeLog

gen_start_commit = 12a56fa3ce0d65a25e7bcf68aba03d8ba189273f
ChangeLog_files = ChangeLog
.PHONY: gen-ChangeLog
gen-ChangeLog:
	$(AM_V_GEN)if test -d $(top_srcdir)/.git; then			\
	  (cd $(top_srcdir)/ &&						\
	  ./gitlog-to-changelog	--strip-tab				\
	    $(gen_start_commit).. &&					\
	  echo) >> $(distdir)/cl-t &&					\
	  for f in $(ChangeLog_files); do				\
	    (cd $(top_srcdir)/ &&					\
	    git show $(gen_start_commit):$$f) >> $(distdir)/cl-t &&	\
	    rm -f $(distdir)/$$f &&					\
	    mv $(distdir)/cl-t $(distdir)/$$f				\
	    || exit $$?;						\
	  done;								\
	fi

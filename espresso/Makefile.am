SUBDIRS = main mincov sparse util .

noinst_LTLIBRARIES = libespresso.la

AM_CFLAGS = -Wall $(all_includes) -I$(srcdir) \
	      -I$(srcdir)/main -I$(srcdir)/mincov \
	      -I$(srcdir)/util -I$(srcdir)/sparse

libespresso_la_SOURCES =
libespresso_la_LIBADD = main/libmain.la util/libutil.la sparse/libsparse.la mincov/libmincov.la

#bin_PROGRAMS = espresso
#espresso_LDADD = main/libmain_original.la util/libutil.la sparse/libsparse.la mincov/libmincov.la
#espresso_SOURCES = Main.


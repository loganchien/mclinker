AUTOMAKE_OPTIONS = foreign

ACLOCAL_AMFLAGS = -I m4

# We define the global AM_CPPFLAGS as everything we compile includes from these
# directories.
AM_CPPFLAGS = -I$(srcdir)/include/

# Modifies compiler and linker flags for pthreads compatibility.
if HAVE_PTHREADS
  AM_CXXFLAGS = @PTHREAD_CFLAGS@
  AM_LIBS = @PTHREAD_LIBS@
else
  AM_CXXFLAGS = -DGTEST_HAS_PTHREAD=0
endif

# Build rules for libraries.
noinst_LTLIBRARIES = libgtest.la
libgtest_la_SOURCES = src/gtest-all.cpp

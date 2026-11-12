SUBDIRS = src docs

dfu_programmer: $(srcdir)/dfu_completion $(srcdir)/update-bash-completion.sh $(srcdir)/src/arguments.c
	$(AM_V_GEN)$(srcdir)/update-bash-completion.sh $(srcdir) $@ >/dev/null

bash-completion: dfu_programmer
.PHONY: bash-completion
all: bash-completion
CLEANFILES = dfu_programmer

EXTRA_DIST = bootstrap.sh
EXTRA_DIST += dfu_completion
EXTRA_DIST += dfu-programmer.spec
EXTRA_DIST += update-bash-completion.sh

# ---- make check: hardware-free unit tests ----
check_PROGRAMS = test/unit/test_intel_hex
TESTS = $(check_PROGRAMS)

UNIT_CPPFLAGS = -I$(top_srcdir)/src -I$(top_srcdir)/test/unit -I$(top_builddir)/src $(LIBUSB_INCLUDE_FLAGS)
UNIT_CFLAGS = -Wall $(WERROR_CFLAGS)

test_unit_test_intel_hex_SOURCES = test/unit/test_intel_hex.c test/unit/unit.h \
	src/intel_hex.c src/util.c
test_unit_test_intel_hex_CPPFLAGS = $(UNIT_CPPFLAGS)
test_unit_test_intel_hex_CFLAGS = $(UNIT_CFLAGS)
test_unit_test_intel_hex_LDADD =

CLEANFILES += test_intel_hex.tmp.hex test_intel_hex.tmp.out

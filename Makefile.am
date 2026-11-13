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

# ---- udev rules (Linux) ----
# udev/70-dfu-programmer.rules is the output of `dfu-programmer --targets-udev`.
# After changing the target table, run `make update-udev-rules`;
# `make check` fails until the two match.
EXTRA_DIST += udev/70-dfu-programmer.rules
if INSTALL_UDEV_RULES
udevrules_DATA = udev/70-dfu-programmer.rules
endif
AM_DISTCHECK_CONFIGURE_FLAGS = --with-udevrulesdir='$${libdir}/udev/rules.d'

update-udev-rules: src/dfu-programmer$(EXEEXT)
	$(AM_V_GEN)src/dfu-programmer$(EXEEXT) --targets-udev > $(srcdir)/udev/70-dfu-programmer.rules
.PHONY: update-udev-rules

# ---- generated docs ----
# The supported-chips lists in README.md and the man page come from
# `dfu-programmer --targets-markdown/-tex/-html`. After changing the target
# table, run `make update-docs`; `make check` fails until they match.
EXTRA_DIST += update-docs.sh
update-docs: src/dfu-programmer$(EXEEXT)
	$(AM_V_GEN)$(SHELL) $(srcdir)/update-docs.sh src/dfu-programmer$(EXEEXT) $(srcdir) $(srcdir)
.PHONY: update-docs

# ---- make check: hardware-free unit tests ----
check_PROGRAMS = test/unit/test_intel_hex
TESTS = $(check_PROGRAMS) test/udev-rules.sh test/docs-targets.sh
TEST_EXTENSIONS = .sh
SH_LOG_COMPILER = $(SHELL)
EXTRA_DIST += test/udev-rules.sh test/docs-targets.sh

UNIT_CPPFLAGS = -I$(top_srcdir)/src -I$(top_srcdir)/test/unit -I$(top_builddir)/src $(LIBUSB_INCLUDE_FLAGS)
UNIT_CFLAGS = -Wall $(WERROR_CFLAGS)

test_unit_test_intel_hex_SOURCES = test/unit/test_intel_hex.c test/unit/unit.h \
	src/intel_hex.c src/util.c
test_unit_test_intel_hex_CPPFLAGS = $(UNIT_CPPFLAGS)
test_unit_test_intel_hex_CFLAGS = $(UNIT_CFLAGS)
test_unit_test_intel_hex_LDADD =

CLEANFILES += test_intel_hex.tmp.hex test_intel_hex.tmp.out
CLEANFILES += udev-rules.tmp udev-rules.want

clean-local:
	-rm -rf docs-targets.tmp

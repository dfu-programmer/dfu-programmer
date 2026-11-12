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

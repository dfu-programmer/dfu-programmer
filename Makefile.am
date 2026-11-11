SUBDIRS = src docs

dfu_programmer: dfu_completion update-bash-completion.sh src/arguments.c
	./update-bash-completion.sh

bash-completion: dfu_programmer
.PHONY: bash-completion
all: bash-completion

EXTRA_DIST = bootstrap.sh
EXTRA_DIST += dfu_completion
EXTRA_DIST += dfu-programmer.spec
EXTRA_DIST += update-bash-completion.sh

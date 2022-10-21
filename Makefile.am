SUBDIRS = src docs

dfu_programmer: dfu_completion update-bash-completion.sh src/arguments.c
	./update-bash-completion.sh

bash-completion: dfu_programmer
.PHONY: bash-completion
all: bash-completion

EXTRA_DIST = bootstrap.sh dfu_completion \
             fedora/10-dfu-programmer.fdi \
             fedora/dfu-programmer.spec

#! /bin/sh
aclocal -I m4
autoheader
automake --foreign --add-missing --copy
autoconf

if [ "$(echo `uname`)" = "Linux" ]; then
    TARGET_START_LINE=$( grep START_TARGET_LIST_LINE -n src/arguments.c  | \
                            sed "s/^\(.*\):.*/\1/")
    TARGET_END_LINE=$( grep END_TARGET_LIST_LINE -n src/arguments.c | \
                            sed "s/^\(.*\):.*/\1/")
    TARGET_INFO=$( sed -n "$TARGET_START_LINE,$TARGET_END_LINE p" src/arguments.c | \
      grep "^\s*{ \"" | sed 's/.*"\(.*\)".*,\s*\([xX0-9A-Fa-f]*\)\s*},/\1::\2/' )

    DFU_COMP=dfu_programmer

    echo \#\ autocomplete\ script > $DFU_COMP
    echo "# code sourced from dfu_completion, DO NOT EDIT IN THIS FILE" \
        >> $DFU_COMP
    echo >> $DFU_COMP
    echo -n "TARGET_INFO=\" " >> $DFU_COMP
    echo -n $TARGET_INFO >> $DFU_COMP
    echo " \"" >> $DFU_COMP
    echo >> $DFU_COMP
    echo >> $DFU_COMP

    cat dfu_completion >> $DFU_COMP

    chmod +x $DFU_COMP

    #if [ "$(cat /etc/*release | grep DISTRIB_ID | sed 's/DISTRIB_ID=//')" = "Ubuntu" ]]; then
    echo "To install tab completion run"
    echo " \$ sudo mv $DFU_COMP /etc/bash_completion.d/"
    #fi
    echo "To use tab completion in this terminal run"
    echo " \$ source $DFU_COMP"
fi

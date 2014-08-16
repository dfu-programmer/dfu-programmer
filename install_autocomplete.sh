#!/bin/bash

TARGET_START_LINE=$( grep START_TARGET_LIST_LINE -n src/arguments.c  | \
                        sed "s/^\(.*\):.*/\1/")
TARGET_END_LINE=$( grep END_TARGET_LIST_LINE -n src/arguments.c | \
                        sed "s/^\(.*\):.*/\1/")
TARGET_INFO=$( sed -n "$TARGET_START_LINE,$TARGET_END_LINE p" src/arguments.c | \
  grep "^\s*{ \"" | sed 's/.*"\(.*\)".*,\s*\([xX0-9A-Fa-f]*\)\s*},/\1::\2/' )

echo \#\ autocomplete\ script > dfu_programmer
echo >> dfu_programmer
echo >> dfu_programmer
echo -n "TARGET_INFO=\" " >> dfu_programmer
echo -n $TARGET_INFO >> dfu_programmer
echo " \"" >> dfu_programmer
echo "# code sourced from dfu_completion, DO NOT EDIT IN THIS FILE"
echo >> dfu_programmer
echo >> dfu_programmer

cat dfu_completion >> dfu_programmer

chmod +x dfu_programmer

sudo mv dfu_programmer /etc/bash_completion.d/

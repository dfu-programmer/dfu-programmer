#!/bin/bash

# sed - without printing by default (-n)
# Remove lines up to and including the start line, which includes: START_TARGET_LIST_LINE
# Remove lines after and including the end line, which includes: END_TARGET_LIST_LINE
# Get the part between the quotes and the last number
TARGET_INFO=$( sed -n '1,/START_TARGET_LIST_LINE/d; /END_TARGET_LIST_LINE/,$d; s/.*"\(.*\)".*,\s*\([xX0-9A-Fa-f]*\)\s*},/\1::\2/p' src/arguments.c )

# Output file
DFU_COMP=dfu_programmer

# Write the header with the TARGET_INFO
cat << EOF > $DFU_COMP
# autocomplete script
# code sourced from \`dfu_completion\`
# DO NOT EDIT IN THIS FILE

TARGET_INFO="$TARGET_INFO"

EOF

# Remove everything up to and including the first empty line
sed 1,/^\$/d dfu_completion >> $DFU_COMP

chmod +x $DFU_COMP

#if [ "$(cat /etc/*release | grep DISTRIB_ID | sed 's/DISTRIB_ID=//')" = "Ubuntu" ]]; then
echo "To install tab completion run"
echo " \$ sudo mv $DFU_COMP /etc/bash_completion.d/"
#fi
echo "To use tab completion in this terminal run"
echo " \$ source $DFU_COMP"

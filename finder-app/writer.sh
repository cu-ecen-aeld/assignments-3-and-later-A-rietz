#!/bin/bash

if [ "$#" -ne 2 ]; then
  echo "Error: Missing arguments."
  echo "Usage: $0 <writefile> <writestr>"
  exit 1
fi

writefile="$1"
writestr="$2"

dirpath=$(dirname "$writefile")

sudo mkdir -p "$dirpath"
if [ $? -ne 0 ]; then
  echo "Error: Failed to create directory $dirpath"
  exit 1
fi

echo "$writestr" > "$writefile"
if [ $? -ne 0 ]; then
  echo "Error: Failed to write to file $writefile"
  exit 1
fi

exit 0
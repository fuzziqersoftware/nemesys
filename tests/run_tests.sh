#!/bin/bash

set -e

export PYTHONDONTWRITEBYTECODE=1

for FILE in *.py; do
  if [ -e $FILE.input.1 ]; then
    for INPUT_FILE in $FILE.input.*; do
      echo "-- python3 $FILE ($INPUT_FILE)"
      python3 $FILE < $INPUT_FILE > output.$INPUT_FILE.txt
    done
  else
    echo "-- python3 $FILE"
    python3 $FILE > output.$FILE.txt
  fi
done

for OPTIONS in "" "-XNoInlineRefcounting" "-XNoEagerCompilation" "-XNoInlineRefcounting -XNoEagerCompilation"; do
  for FILE in *.py; do
    if [ -e $FILE.input.1 ]; then
      for INPUT_FILE in $FILE.input.*; do
        echo "-- nemesys $OPTIONS $FILE ($INPUT_FILE)"
        ../nemesys $OPTIONS $FILE < $INPUT_FILE | diff -U3 output.$INPUT_FILE.txt -
      done
    else
      echo "-- nemesys $OPTIONS $FILE"
      ../nemesys $OPTIONS $FILE | diff -U3 output.$FILE.txt -
    fi
  done
done

echo "-- all tests passed"

rm -f output.*.txt

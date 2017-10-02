#!/bin/bash

set -e

export PYTHONDONTWRITEBYTECODE=1

for FILE in tests/*.py; do
  if [ -e $FILE.input.1 ]; then
    for INPUT_FILE in $FILE.input.*; do
      echo "-- $FILE ($INPUT_FILE)"
      python3 $FILE < $INPUT_FILE > $FILE.expected_output
      ./nemesys $FILE < $INPUT_FILE | diff -U3 $FILE.expected_output -
    done
  else
    echo "-- $FILE"
    python3 $FILE > $FILE.expected_output
    ./nemesys $FILE | diff -U3 $FILE.expected_output -
  fi
  rm $FILE.expected_output
done

echo "-- all tests passed"

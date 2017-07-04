#!/bin/bash

set -e

echo "-- tests.hello_world_trivial"
./nemesys tests.hello_world_trivial | diff - tests/hello_world.expected_output
echo "-- tests.hello_world_combination"
./nemesys tests.hello_world_combination | diff - tests/hello_world.expected_output

echo "-- all tests passed"

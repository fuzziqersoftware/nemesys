#!/bin/bash

set -e

echo "-- tests.hello_world_trivial"
./nemesys tests.hello_world_trivial | diff -U3 tests/hello_world.expected_output -
echo "-- tests.hello_world_combination"
./nemesys tests.hello_world_combination | diff -U3 tests/hello_world.expected_output -
echo "-- tests.echo_once"
echo 'Hello world!' | ./nemesys tests.echo_once | diff -U3 tests/hello_world.expected_output -
echo "-- tests.repr"
./nemesys tests.repr | diff -U3 tests/repr.expected_output -
echo "-- tests.integers"
./nemesys tests.integers | diff -U3 tests/integers.expected_output -

echo "-- all tests passed"

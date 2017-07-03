#!/bin/bash

echo "-- tests.hello_world"
./nemesys tests.hello_world | diff - tests/hello_world.expected_output

echo "-- all tests passed"

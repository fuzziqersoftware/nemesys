#!/bin/bash

set -e

echo "-- tests.hello_world_trivial"
./nemesys tests/hello_world_trivial.py | diff -U3 tests/hello_world.expected_output -
echo "-- tests.hello_world_combination"
./nemesys tests/hello_world_combination.py | diff -U3 tests/hello_world.expected_output -
echo "-- tests.echo_once"
echo 'Hello world!' | ./nemesys tests/echo_once.py | diff -U3 tests/hello_world.expected_output -
echo "-- tests.repr"
./nemesys tests/repr.py | diff -U3 tests/repr.expected_output -
echo "-- tests.integers"
./nemesys tests/integers.py | diff -U3 tests/integers.expected_output -
#echo "-- tests.floats"
#./nemesys tests/floats.py | diff -U3 tests/floats.expected_output -
echo "-- tests.functions"
./nemesys tests/functions.py | diff -U3 tests/functions.expected_output -
echo "-- tests.explicit_globals"
./nemesys tests/explicit_globals.py | diff -U3 tests/explicit_globals.expected_output -
echo "-- tests.lists"
./nemesys tests/lists.py | diff -U3 tests/lists.expected_output -
echo "-- tests.loops"
./nemesys tests/loops.py | diff -U3 tests/loops.expected_output -
echo "-- tests.argv"
./nemesys tests/argv.py arg1 --arg2 "arg3 has spaces" | diff -U3 tests/argv.expected_output -
echo "-- tests.quine_read"
./nemesys tests/quine_read.py | diff -U3 tests/quine_read.py -
echo "-- tests.quine_exec"
./nemesys tests/quine_exec.py | diff -U3 tests/quine_exec.py -
echo "-- tests.quine_nemesys"
./nemesys tests/quine_nemesys.py | diff -U3 tests/quine_nemesys.py -
echo "-- tests.classes"
./nemesys tests/classes.py | diff -U3 tests/classes.expected_output -
echo "-- tests.static_analysis"
./nemesys tests/static_analysis.py | diff -U3 tests/static_analysis.expected_output -
echo "-- tests.exceptions_basic (no y)"
echo "ok" | ./nemesys tests/exceptions_basic.py | diff -U3 tests/exceptions_basic.no_y.expected_output -
echo "-- tests.exceptions_basic (y)"
echo "yep" | ./nemesys tests/exceptions_basic.py | diff -U3 tests/exceptions_basic.y.expected_output -
echo "-- tests.exceptions_nested (no y)"
echo "ok" | ./nemesys tests/exceptions_nested.py | diff -U3 tests/exceptions_nested.no_y.expected_output -
echo "-- tests.exceptions_nested (y)"
echo "yep" | ./nemesys tests/exceptions_nested.py | diff -U3 tests/exceptions_nested.y.expected_output -
echo "-- tests.exceptions_functions (no y)"
echo "ok" | ./nemesys tests/exceptions_functions.py | diff -U3 tests/exceptions_functions.no_y.expected_output -
echo "-- tests.exceptions_functions (y)"
echo "yep" | ./nemesys tests/exceptions_functions.py | diff -U3 tests/exceptions_functions.y.expected_output -

echo "-- all tests passed"

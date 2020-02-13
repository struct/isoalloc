#!/bin/bash
# This script runs all debug tests including vulnerable
# examples of code that should crash

export LD_LIBRARY_PATH=build/
export LD_PRELOAD=build/libisoalloc.so

rm test_output.txt

tests=("tests")
failure=0
succeeded=0

for t in "${tests[@]}"; do
    echo -n "Running $t test"
    $(build/$t >> test_output.txt 2>&1)
    ret=$?

    if [ $ret -ne 0 ]; then
        echo "... Failed"
        failure=$((failure+1))
    else
        echo "... Succeeded"
        succeeded=$((succeeded+1))
    fi
done

vuln_tests=("double_free" "heap_overflow" "heap_underflow")

for t in "${vuln_tests[@]}"; do
    echo -n "Running $t test"
    $(build/$t >> test_output.txt 2>&1)
    ret=$?

    if [ $ret -ne 0 ]; then
        echo "... Succeeded"
        succeeded=$((succeeded+1))
    else
        echo "... Failed"
        failure=$((failure+1))
    fi
done

echo "$succeeded Tests passed"
echo "$failure Tests failed"

unset LD_LIBRARY_PATH
unset LD_PRELOAD

if [ $failure -ne 0 ]; then
    exit -1
else
    exit 0
fi

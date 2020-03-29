#!/bin/bash
# This script runs all debug tests including vulnerable
# examples of code that should crash

$(echo '' > test_output.txt)

tests=("tests" "big_tests" "interfaces_test" "thread_tests")
failure=0
succeeded=0

for t in "${tests[@]}"; do
    echo -n "Running $t test"
    echo -n "Running $t test" >> test_output.txt 2>&1
    $(LD_LIBRARY_PATH=build/ LD_PRELOAD=build/libisoalloc.so build/$t >> test_output.txt 2>&1)
    ret=$?

    if [ $ret -ne 0 ]; then
        echo "... Failed"
        echo "... Failed" >> test_output.txt 2>&1
        failure=$((failure+1))
    else
        echo "... Succeeded"
        echo "... Succeeded" >> test_output.txt 2>&1
        succeeded=$((succeeded+1))
    fi
done

fail_tests=("double_free" "heap_overflow" "heap_underflow" "leaks_test"
            "wild_free" "unaligned_free" "incorrect_chunk_size_multiple")

for t in "${fail_tests[@]}"; do
    echo -n "Running $t test"
    echo -n "Running $t test" >> test_output.txt 2>&1
    $(LD_LIBRARY_PATH=build/ LD_PRELOAD=build/libisoalloc.so build/$t >> test_output.txt 2>&1)
    ret=$?

    if [ $ret -ne 0 ]; then
        echo "... Succeeded"
        echo "... Succeeded" >> test_output.txt 2>&1
        succeeded=$((succeeded+1))
    else
        echo "... Failed"
        echo "... Failed" >> test_output.txt 2>&1
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

#!/usr/bin/env bash
$(echo '' > tagging_test_output.txt)

tests=("tagged_ptr_test" "tagged_ptr_test_cpp")
failure=0
succeeded=0

$(ulimit -c 0)

export LD_LIBRARY_PATH=build/

for t in "${tests[@]}"; do
    echo -n "Running $t test"
    echo "Running $t test" >> tagging_test_output.txt 2>&1
    $(build/$t >> tagging_test_output.txt 2>&1)
    ret=$?

    if [ $ret -ne 0 ]; then
        echo "... Failed"
        echo "... Failed" >> tagging_test_output.txt 2>&1
        failure=$((failure+1))
    else
        echo "... Succeeded"
        echo "... Succeeded" >> tagging_test_output.txt 2>&1
        succeeded=$((succeeded+1))
    fi
done

fail_tests=("bad_tag_ptr_test" "verify_tag_ptr_test" "uaf_tag_ptr_test")

for t in "${fail_tests[@]}"; do
    echo -n "Running $t test"
    echo "Running $t test" >> tagging_test_output.txt 2>&1
    $(build/$t >> tagging_test_output.txt 2>&1)
    ret=$?

    if [ $ret -ne 0 ]; then
        echo "... Succeeded"
        echo "... Succeeded" >> tagging_test_output.txt 2>&1
        succeeded=$((succeeded+1))
    else
        echo "... Failed"
        echo "... Failed" >> tagging_test_output.txt 2>&1
        failure=$((failure+1))
    fi
done

echo "$succeeded Tests passed"
echo "$failure Tests failed"

unset LD_LIBRARY_PATH
unset LD_PRELOAD

if [ $failure -ne 0 ]; then
    cat tagging_test_output.txt
    exit -1
else
    exit 0
fi

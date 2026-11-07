#!/usr/bin/env bash
# Runs the heap profiler test and checks the data file
# the profiler writes when the process exits
$(echo '' > profiler_test_output.txt)

$(ulimit -c 0)

export LD_LIBRARY_PATH=build/
export ISO_ALLOC_PROFILER_FILE_PATH=build/iso_alloc_profiler.data
rm -f $ISO_ALLOC_PROFILER_FILE_PATH

failure=0

echo -n "Running profiler_test test"
echo "Running profiler_test test" >> profiler_test_output.txt 2>&1
$(build/profiler_test >> profiler_test_output.txt 2>&1)
ret=$?

if [ $ret -ne 0 ]; then
    echo "... Failed"
    failure=$((failure+1))
else
    echo "... Succeeded"
fi

# The profile must record sampled allocations and frees
# and a row for the largest chunk size. That row lands
# one entry past the end of an undersized map
echo -n "Checking profiler data file"

if [ ! -s $ISO_ALLOC_PROFILER_FILE_PATH ]; then
    echo "... Failed (no data file)"
    failure=$((failure+1))
elif ! grep -qE '^alloc_sampled=[1-9]' $ISO_ALLOC_PROFILER_FILE_PATH ||
     ! grep -qE '^free_sampled=[1-9]' $ISO_ALLOC_PROFILER_FILE_PATH ||
     ! grep -qE '^alloc_backtrace=' $ISO_ALLOC_PROFILER_FILE_PATH ||
     ! grep -qE '^65536,[0-9]+,[1-9]' $ISO_ALLOC_PROFILER_FILE_PATH; then
    echo "... Failed (unexpected contents)"
    cat $ISO_ALLOC_PROFILER_FILE_PATH
    failure=$((failure+1))
else
    echo "... Succeeded"
fi

unset LD_LIBRARY_PATH
unset LD_PRELOAD
unset ISO_ALLOC_PROFILER_FILE_PATH

if [ $failure -ne 0 ]; then
    cat profiler_test_output.txt
    exit -1
else
    exit 0
fi

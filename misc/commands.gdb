file tests
set env LD_LIBRARY_PATH=.
set env LD_PRELOAD=./libisoalloc.so
r
i r
x/i $rip
bt
info locals
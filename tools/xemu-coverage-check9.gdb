set pagination off
set confirm off
set architecture i386
target remote 127.0.0.1:1234
printf "=== Tracing sub_0006E2E0's real caller (one of the dispatcher's own callers) ===\n"
hbreak *0x0006E2E0
continue
printf "--- stop 1 ---\n"
print/x $eip
x/1xw $esp
continue
printf "--- stop 2 ---\n"
print/x $eip
x/1xw $esp
continue
printf "--- stop 3 ---\n"
print/x $eip
x/1xw $esp

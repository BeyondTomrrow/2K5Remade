set pagination off
set confirm off
set architecture i386
target remote 127.0.0.1:1234
printf "=== Reconnected. Tracing sub_0008D490/sub_0009F940's real callers on the working reference ===\n"
hbreak *0x0008D490
hbreak *0x0009F940
printf "=== Continuing ===\n"
continue
printf "--- stop 1 ---\n"
print/x $eip
bt 6
continue
printf "--- stop 2 ---\n"
print/x $eip
bt 6
continue
printf "--- stop 3 ---\n"
print/x $eip
bt 6
continue
printf "--- stop 4 ---\n"
print/x $eip
bt 6

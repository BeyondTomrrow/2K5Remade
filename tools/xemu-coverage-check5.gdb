set pagination off
set confirm off
set architecture i386
target remote 127.0.0.1:1234
printf "=== Fresh boot. Tracing the full chain in one pass: 13610 -> 1124E0 -> 64CD0 -> 11A7C0 ===\n"
hbreak *0x00013610
hbreak *0x001124E0
hbreak *0x00064CD0
hbreak *0x0011A7C0
printf "=== Continuing from reset ===\n"
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
continue
printf "--- stop 5 ---\n"
print/x $eip
bt 6
continue
printf "--- stop 6 ---\n"
print/x $eip
bt 6

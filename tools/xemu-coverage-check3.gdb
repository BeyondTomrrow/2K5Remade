set pagination off
set confirm off
set architecture i386
target remote 127.0.0.1:1234
printf "=== Reconnected. Watching who calls sub_000432F0 and who writes the 0xB11224 gate counter ===\n"
hbreak *0x000432F0
watch *(int*)0xB11224
printf "=== Continuing: first stop should be either the call or the watchpoint ===\n"
continue
printf "--- stop 1: eip, gate value, call-stack ---\n"
print/x $eip
print/x *(int*)0xB11224
bt 6
continue
printf "--- stop 2 ---\n"
print/x $eip
print/x *(int*)0xB11224
bt 6
continue
printf "--- stop 3 ---\n"
print/x $eip
print/x *(int*)0xB11224
bt 6
continue
printf "--- stop 4 ---\n"
print/x $eip
print/x *(int*)0xB11224
bt 6

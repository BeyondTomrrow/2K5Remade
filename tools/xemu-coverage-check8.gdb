set pagination off
set confirm off
set architecture i386
target remote 127.0.0.1:1234
printf "=== Fresh boot: hbreak sub_000650A0, watch the flag-set at 0xA83A18, rwatch the registry pointer read at 0x4E7DFC ===\n"
hbreak *0x000650A0
watch *(int*)0xA83A18
rwatch *(int*)0x4E7DFC
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
continue
printf "--- stop 4 ---\n"
print/x $eip
x/1xw $esp
continue
printf "--- stop 5 ---\n"
print/x $eip
x/1xw $esp
continue
printf "--- stop 6 ---\n"
print/x $eip
x/1xw $esp

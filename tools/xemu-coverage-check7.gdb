set pagination off
set confirm off
set architecture i386
target remote 127.0.0.1:1234
printf "=== Fresh boot. Finding the true caller of sub_000650A0 via [ESP] ===\n"
hbreak *0x000650A0
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

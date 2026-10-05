set pagination off
set confirm off
set architecture i386
target remote 127.0.0.1:1234
printf "=== Reconnected. Reading true return address via [ESP] directly (bt is unreliable for fpo_leaf functions) ===\n"
hbreak *0x00064CD0
hbreak *0x0011A7C0
continue
printf "--- stop 1 ---\n"
print/x $eip
printf "true return address: "
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

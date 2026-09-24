set pagination off
set confirm off
set architecture i386
target remote 127.0.0.1:1234
printf "=== first sub_000650A0 after the main loop body sub_00074790 starts ===\n"
hbreak *0x00074790
continue
printf "main loop entered: ret=%08x\n", *(unsigned int*)$esp
delete
hbreak *0x000650A0
continue
printf "650A0: esp=%08x\n", $esp
x/64xw $esp

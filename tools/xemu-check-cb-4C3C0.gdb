set pagination off
set confirm off
set architecture i386
target remote 127.0.0.1:1234
printf "=== completion callback 0x4C3C0: who ran the worker that collected it ===\n"
hbreak *0x0004C3C0
continue
printf "esp=%08x\n", $esp
x/48xw $esp
continue
printf "esp=%08x\n", $esp
x/48xw $esp

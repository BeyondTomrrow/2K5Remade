set pagination off
set confirm off
set architecture i386
target remote 127.0.0.1:1234
printf "=== divisor MEM32(0xB38F30+0x1138): who writes it, and its value when sub_000414F0 divides ===\n"
watch *(int*)0xB3A068
hbreak *0x0004153B
define report
  printf "eip=%08x ret=%08x ebx=%08x div=%d num=%d f1140=%d\n", $eip, *(unsigned int*)$esp, $ebx, *(int*)0xB3A068, *(int*)0xB3AC64, *(int*)0xB3A070
end
continue
report
continue
report
continue
report
continue
report
continue
report
continue
report

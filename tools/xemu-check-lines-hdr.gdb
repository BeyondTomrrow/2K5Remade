set pagination off
set confirm off
set architecture i386
target remote 127.0.0.1:1234
printf "=== who writes the lines.bin header channel field (desc+0x48 @ 0x817B8D08) ===\n"
watch *(int*)0x817B8D08
define report
  printf "eip=%08x esp=%08x val=%08x\n", $eip, $esp, *(int*)0x817B8D08
  x/24xw $esp
end
continue
report
continue
report
continue
report
continue
report

set pagination off
set confirm off
set architecture i386
target remote 127.0.0.1:1234
printf "=== archive read order: task 0x42440 body, submit 0x3BE40, worker item 0x438D0, file read 0x483D0 ===\n"
hbreak *0x00042440
hbreak *0x0003BE40
hbreak *0x000438D0
hbreak *0x000483D0
define report
  printf "eip=%08x ret=%08x ecx=%08x edx=%08x a1=%08x a2=%08x\n", $eip, *(unsigned int*)$esp, $ecx, $edx, *(unsigned int*)($esp+4), *(unsigned int*)($esp+8)
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

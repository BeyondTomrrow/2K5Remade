set pagination off
set confirm off
set architecture i386
target remote 127.0.0.1:1234
printf "=== order: XDSP handler 165DC0, XDSP done 165D90, task cb 42440, header consumer D1450 ===\n"
hbreak *0x00165DC0
hbreak *0x00165D90
hbreak *0x00042440
hbreak *0x000D1450
define report
  printf "eip=%08x ret=%08x esp=%08x\n", $eip, *(unsigned int*)$esp, $esp
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

set pagination off
set confirm off
set architecture i386
target remote 127.0.0.1:1234
printf "=== NtReadFile calls whose buffer covers the lines.bin header (0x817B8C80..0x817B8D40 on xemu) ===\n"
hbreak *0x00038FC0
continue
printf "NtReadFile kernel addr=%08x\n", *(unsigned int*)0x4E3C30
delete
eval "hbreak *0x%x if (*(unsigned int*)($esp+0x18) <= 0x817B8D08) && (*(unsigned int*)($esp+0x18) + *(unsigned int*)($esp+0x1C) > 0x817B8D08)", *(unsigned int*)0x4E3C30
define report
  printf "ret=%08x handle=%08x apc=%08x apcctx=%08x iosb=%08x buf=%08x len=%08x off=%08x:%08x\n", *(unsigned int*)$esp, *(unsigned int*)($esp+4), *(unsigned int*)($esp+0xC), *(unsigned int*)($esp+0x10), *(unsigned int*)($esp+0x14), *(unsigned int*)($esp+0x18), *(unsigned int*)($esp+0x1C), *(unsigned int*)(*(unsigned int*)($esp+0x20)+4), *(unsigned int*)(*(unsigned int*)($esp+0x20))
  x/24xw $esp
end
continue
report
continue
report
continue
report

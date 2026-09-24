set pagination off
set confirm off
set architecture i386
target remote 127.0.0.1:1234
printf "=== contiguous allocations on real hardware (Mm*ContiguousMemory*), until sub_000748A0 ===\n"
hbreak *0x00016BD1
continue
delete
set $a165 = *(unsigned int*)0x4E3C08
set $a166 = *(unsigned int*)0x4E3CA4
set $a171 = *(unsigned int*)0x4E3B8C
printf "Mm165=%08x Mm166=%08x Mm171=%08x\n", $a165, $a166, $a171
eval "break *0x%x", $a165
commands
  silent
  printf "Alloc(size=0x%x) ret->%08x\n", *(unsigned int*)($esp+4), *(unsigned int*)$esp
  eval "tbreak *0x%x", *(unsigned int*)$esp
  commands
    silent
    printf "   = %08x\n", $eax
    continue
  end
  continue
end
eval "break *0x%x", $a166
commands
  silent
  printf "AllocEx(size=0x%x low=%08x high=%08x align=0x%x) ret->%08x\n", *(unsigned int*)($esp+4), *(unsigned int*)($esp+8), *(unsigned int*)($esp+12), *(unsigned int*)($esp+16), *(unsigned int*)$esp
  eval "tbreak *0x%x", *(unsigned int*)$esp
  commands
    silent
    printf "   = %08x\n", $eax
    continue
  end
  continue
end
eval "break *0x%x", $a171
commands
  silent
  printf "Free(%08x)\n", *(unsigned int*)($esp+4)
  continue
end
hbreak *0x000748A0
continue
printf "reached sub_000748A0\n"
delete
detach

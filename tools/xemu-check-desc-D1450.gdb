set pagination off
set confirm off
set architecture i386
target remote 127.0.0.1:1234
printf "=== sub_000D1450 entry: format descriptor edi (edi+0x48 is 0 on native) ===\n"
hbreak *0x000D1450
continue
printf "ret=%08x esi(obj)=%08x edi(desc)=%08x eax=%08x\n", *(unsigned int*)$esp, $esi, $edi, $eax
x/32xw $edi
x/32xb $edi
continue
printf "ret=%08x esi(obj)=%08x edi(desc)=%08x eax=%08x\n", *(unsigned int*)$esp, $esi, $edi, $eax
x/32xw $edi

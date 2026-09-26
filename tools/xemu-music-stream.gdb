set pagination off
set confirm off
set architecture i386
target remote 127.0.0.1:1234
printf "=== music streamer ring copies (sub_0003F860), 0x1B000-byte rings only ===\n"
hbreak *0x0003F860 if ($eax - $edi) == 0x1b000
set $k = 0
while ($k < 40)
  continue
  printf "dst=%08x src=%08x ring=%08x..%08x arg=%x src8=%08x off=%x mod36=%d\n", $ebx, $edx, $edi, $eax, *(unsigned int*)($esp+4), *(unsigned int*)$edx, $ebx - $edi, ($ebx - $edi) % 36
  set $k = $k + 1
end
delete
detach
quit

set pagination off
set confirm off
set architecture i386
target remote 127.0.0.1:1234
hbreak *0x0003F860 if ($eax - $edi) == 0x1b000
set $k = 0
while ($k < 700)
  continue
  set $k = $k + 1
  if ($k % 100) == 0
    printf "copy %d dst=%08x off=%x mod36=%d\n", $k, $ebx, $ebx - $edi, ($ebx - $edi) % 36
    eval "dump binary memory E:/NFL2K5-PC/logs/xemu_ringL_%d.bin 0x83b60c20 0x83b7bc20", $k
  end
end
delete
detach
quit

set pagination off
set confirm off
set architecture i386
target remote 127.0.0.1:1234
printf "=== chunk handler registry (head 0xB0957C) at first sub_000D1450 ===\n"
hbreak *0x000D1450
continue
set $n = *(unsigned int*)0xB0957C
set $k = 0
while ($n != 0 && $k < 64)
  printf "node=%08x tag=%08x handler=%08x\n", $n, *(unsigned int*)($n+8), *(unsigned int*)($n+12)
  set $n = *(unsigned int*)$n
  set $k = $k + 1
end

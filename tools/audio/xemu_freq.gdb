set pagination off
set confirm off
set architecture i386
target remote 127.0.0.1:1234
# NFL 2K5's music state machine (sub_0003DBC0) at 0x0003DE7E is about to call the stream's
# SetFrequency (0x004478C5) with esi = the new frequency in Hz, edi = the stream: log every
# change (the call is skipped when the frequency did not change). docs/AUDIO-FIX-20261021.md.
hbreak *0x0003DE7E
set $k = 0
while ($k < 1500)
  continue
  printf "FREQ %d stream %08x track %08x\n", $esi, $edi, *(unsigned int*)($esp + 0xC)
  set $k = $k + 1
end
delete
detach
quit

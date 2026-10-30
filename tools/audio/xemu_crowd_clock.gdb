set pagination off
set confirm off
set architecture i386
target remote 127.0.0.1:1234
# Crowd-reaction pitch against the guest clock (docs/AUDIO-REPORT-HOTKEY-20261022.md).
# At 0x0003DE7E the sound state machine (sub_0003DBC0) is about to call the stream's SetFrequency
# with esi = the new frequency in Hz, edi = the stream. For the crowd-reaction stream (810b8cd8 on
# a retail boot) print the frequency, KeTickCount (guest milliseconds: its address is in the XBE's
# kernel thunk slot 0x004E3D30; the guest clock stops while gdb holds the VM, so the breakpoint
# does not distort it) and the game's crowd sequencer objects (list head 0x00A8BDF8, next +0x2130,
# sequencer time +0x2118 advanced by the frame dt, pitch in cents +0x2150).
hbreak *0x0003DE7E
set $k = 0
set $n = 0
while ($k < 6000 && $n < 400)
  continue
  set $k = $k + 1
  if ($edi == 0x810b8cd8)
    set $o = *(unsigned int*)0x00A8BDF8
    printf "CROWD tick %u hz %d", *(unsigned int*)(*(unsigned int*)0x004E3D30), $esi
    set $j = 0
    while ($o != 0 && $j < 4)
      printf " [%08x t=%f c=%f]", $o, *(float*)($o + 0x2118), *(float*)($o + 0x2150)
      set $o = *(unsigned int*)($o + 0x2130)
      set $j = $j + 1
    end
    printf "\n"
    set $n = $n + 1
  end
end
delete
detach
quit

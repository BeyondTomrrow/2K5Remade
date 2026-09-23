set pagination off
set confirm off
set architecture i386
target remote 127.0.0.1:1234
printf "=== Reconnected. Re-arming hardware breakpoints ===\n"
hbreak *0x00043980
hbreak *0x000432F0
hbreak *0x00061950
hbreak *0x00043BE0
ignore 2 2000
ignore 4 2000
printf "=== Ignoring further hits of already-confirmed 432F0/43BE0; watching for 43980 (archive-read submit) or 61950 ===\n"
continue
print/x $eip
continue
print/x $eip
continue
print/x $eip
continue
print/x $eip
continue
print/x $eip

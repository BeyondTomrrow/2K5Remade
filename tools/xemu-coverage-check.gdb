set pagination off
set confirm off
set architecture i386
target remote 127.0.0.1:1234
printf "=== Connected. Setting hardware breakpoints on native-build dead-code candidates ===\n"
hbreak *0x00043980
hbreak *0x000432F0
hbreak *0x00061950
hbreak *0x00043BE0
printf "=== Breakpoints set. Continuing boot; this may take a while (real Xbox boot + game load) ===\n"
continue
printf "=== First breakpoint hit (or error) above ===\n"
printf "EIP: "
print/x $eip
printf "=== Continuing to look for further hits, up to 3 more ===\n"
continue
print/x $eip
continue
print/x $eip
continue
print/x $eip

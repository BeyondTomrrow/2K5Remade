set pagination off
set confirm off
set architecture i386
target remote 127.0.0.1:1234
printf "Connected to Xemu. The Xbox CPU is paused.\n"
printf "Useful commands: info registers, x/16i $eip, x/32wx $esp, continue\n"

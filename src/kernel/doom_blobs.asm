section .rodata
global doom_bin_start
global doom_bin_end
global wad_start
global wad_end

doom_bin_start:
    incbin "repo/pkgs/doom-generic.bin"
doom_bin_end:

wad_start:
    incbin "doom1.wad"
wad_end:

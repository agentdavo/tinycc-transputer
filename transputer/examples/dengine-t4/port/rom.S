/* The packed dEngine files (mkrom.py), linked into every node's image.
 * ROM_FILE is defined on the command line. */
    .section .rodata
    .balign 8
    .global de_rom_image
    .global de_rom_image_end
de_rom_image:
    .incbin ROM_FILE
de_rom_image_end:

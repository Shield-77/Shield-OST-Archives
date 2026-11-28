    .section .rodata
    .balign 4
    .global shield_ca_bundle_start
    .global shield_ca_bundle_end
shield_ca_bundle_start:
    .incbin "romfs/ca-bundle.pem"
shield_ca_bundle_end:

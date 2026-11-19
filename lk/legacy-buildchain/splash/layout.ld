/* The alignment gap after LK's exception handlers.
 * image.py checks it is unused and writes the header before this code. */
SECTIONS {
    . = 0xb5001a60;
    .text : { *(.text*) *(.rodata*) *(.data.splash) }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
    ASSERT(. <= 0xb5002000, "LK splash extension exceeds boot padding")
}

lk_mode_names = 0xb50b7c20;
lk_font = 0xb50de848;
lk_lcd_enabled = 0xb56bae24;
lk_lcd_base = 0xb56bae10;
lk_lcd_pitch = 0xb56bae20;
lk_lcd_width = 0xb50ffc98;
lk_lcd_height = 0xb50ffc9a;
lk_lcd_bpix = 0xb50ffc9e;
lk_lcd_flush = 0xb5086d00;
lk_draw_bitmap = 0xb5088a6c;
lk_draw_splash = 0xb5088bd4;
lk_log_mode = 0xb5035f60;
lk_log_fastboot = 0xb5035c78;
lk_warning_line = 0xb50883fc;

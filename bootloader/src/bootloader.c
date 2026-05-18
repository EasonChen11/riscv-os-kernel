void bootloader_main(unsigned long hartid, const void *dtb)
{
    (void)hartid;
    (void)dtb;
    for (;;)
        asm volatile("wfi");
}

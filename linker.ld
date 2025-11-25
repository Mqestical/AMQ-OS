/* linker.ld - Complete UEFI linker script for x86_64 */

OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

SECTIONS
{
    . = 0;
    ImageBase = .;

    /* ===================== TEXT (CODE) ===================== */
    .text : ALIGN(4096)
    {
        _text = .;
        *(.text.head)
        *(.text)
        *(.text.*)
        *(.gnu.linkonce.t.*)
        . = ALIGN(16);
    }
    _etext = .;
    _text_size = . - _text;

    /* ===================== RELOCATION ===================== */
    .reloc : ALIGN(4096)
    {
        *(.reloc)
    }

    /* ===================== READ-ONLY DATA ===================== */
    .rodata : ALIGN(4096)
    {
        _rodata = .;
        *(.rodata)
        *(.rodata.*)
        *(.rodata1)
        *(.gnu.linkonce.r.*)
        . = ALIGN(16);
    }
    _erodata = .;
    _rodata_size = . - _rodata;

    /* ===================== DATA (INITIALIZED) ===================== */
    .data : ALIGN(4096)
    {
        _data = .;
        *(.data)
        *(.data.*)
        *(.data1)
        *(.gnu.linkonce.d.*)
        *(.sdata)
        *(.sdata.*)
        *(.got.plt)
        *(.got)

        /* UEFI runtime service data */
        *(.dynamic)
        *(.dynsym)
        *(.dynstr)
        *(.hash)

        . = ALIGN(16);
    }
    _edata = .;
    _data_size = . - _data;

    /* ===================== BSS (UNINITIALIZED) ===================== */
    .bss : ALIGN(4096)
    {
        _bss = .;
        *(.sbss)
        *(.sbss.*)
        *(.scommon)
        *(.dynbss)
        *(COMMON)
        *(.bss)
        *(.bss.*)
        *(.gnu.linkonce.b.*)
        . = ALIGN(16);
    }
    _ebss = .;
    _bss_size = . - _bss;

    /* ===================== DISCARD SECTIONS ===================== */
    /DISCARD/ :
    {
        *(.note.*)
        *(.comment)
        *(.eh_frame)
        *(.eh_frame_hdr)
        *(.discard)
    }
}

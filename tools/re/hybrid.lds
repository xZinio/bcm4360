/* SPDX-License-Identifier: ISC */
/*
 * `ld -r` script of tools/re/hybrid.py: links the hybrid core for the
 * emulator. Broadcom's object comes first in every section, so that its
 * offsets (and every sub_XXXXXX name of the tools) stay as they are; the open
 * code follows. Everything ends up in the five sections tools/re/blob.py
 * loads. Only the placement differs from the link of bcm4360.ko; which
 * definition each symbol resolves to is the same.
 */
SECTIONS
{
	.text : {
		*wlc_hybrid_open.o(.text)
		*(.text .text.*)
	}
	.rodata.str1.1 : {
		*wlc_hybrid_open.o(.rodata.str1.1)
	}
	.rodata : {
		*wlc_hybrid_open.o(.rodata)
		*(.rodata .rodata.*)
	}
	.data : {
		*wlc_hybrid_open.o(.data)
		*(.data .data.*)
	}
	.bss : {
		*wlc_hybrid_open.o(.bss)
		*(.bss .bss.* COMMON)
	}
	/DISCARD/ : {
		*(.comment) *(.note.*) *(.llvm_addrsig) *(.eh_frame)
	}
}

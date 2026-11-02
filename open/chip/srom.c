// SPDX-License-Identifier: ISC
/*
 * The board variables of a card ("boardflags=0x...", "pa2ga0=...") made from
 * its SROM, from the SROM image in its OTP, or taken from the platform's
 * NVRAM: the PCI path of Broadcom's srom_var_init with the conversion of a
 * revision-11 image, and srom_read.
 *
 * Written from docs/re/spec/srom-otp.md. The backplane, the OTP and the NVRAM
 * are reached only through the services of <bcm4360/srom.h>, called where the
 * specification's procedures call the corresponding functions.
 */
#include <bcm4360/srom.h>
#include <bcm4360/utils.h>
#include <bcm4360/hw.h>

/* srom-otp.md, "srom_var_init", step 2: the bus type handled here */
#define SROM_BUS_PCI		1

/* srom_var_init, results */
#define SROM_ERR		(-1)	/* no window, no image and no NVRAM sromrev */
#define SROM_ERR_IMAGE		(-2)	/* no memory for the buffers, unsupported revision */
#define BCME_BUFTOOSHORT	(-14)
#define BCME_NOMEM		(-27)

/* srom_var_init, steps 3 and 8: the image buffer and the work buffer */
#define SROM_IMG_BYTES		0x300
#define SROM_WORK_BYTES		0x1000

/* srom_var_init, step 4: the SROM window in BAR0 */
#define SROM_CCREV_OLD_MAX	31	/* ChipCommon revisions up to this one: */
#define SROM_WINDOW_OLD		0x1000	/* ... the old window */
#define SROM_WINDOW		0x3800	/* ChipCommon offset 0x800 */
#define CC_CAP_SROM		0x40000000	/* capabilities bit 30: SROM present */

/* srom-otp.md, "SROM image signatures and revision locations" */
#define SROM_WORDS_234		234	/* revision 11 */
#define SROM_WORDS_220		220	/* revisions 4, 5, 8, 9, 10 */
#define SROM_WORDS_64		64	/* revisions 1..3 */
#define SROM_SIG_234		0x0634
#define SROM_SIG_234_WORD	64
#define SROM_SIG_220		0x5372
#define SROM_SIG_220_WORD_A	32	/* revisions 4, 5 */
#define SROM_SIG_220_WORD_B	64	/* revisions 8, 9, 10 */
#define SROM_REV_WORD_234	233
#define SROM_REV_WORD_220	219
#define SROM_REV_WORD_230	229	/* OTP image whose word 219 is the signature */
#define SROM_REV_WORD_64	63
#define SROM_REV_BYTE		0xff	/* the revision is the low byte of its word */
#define SROM_REV1_CODE		0x10	/* "0x10" in a 64-word image means revision 1 */
#define SROM_REV1		1
#define SROM_REVS_ACCEPTED	0xf3e	/* bit `rev`: 1..5, 8..11 */
#define SROM_REV_SHIFT_MASK	31

/* srom-otp.md, "sprom_read_pci", step 4: the CRC of an image */
#define SROM_BLANK_WORD		0xffff
#define SROM_CRC8_INIT		0xff
#define SROM_CRC8_GOOD		0x9f	/* residue over an image including its CRC byte */

/* sprom_read_pci, steps 1 and 2: the chips that touch the chip control */
#define CHIP_4331		0x4331
#define CHIP_A9A7		0xa9a7
#define CHIP_4360		0x4360
#define CHIP_A9C4		0xa9c4
#define CHIP_4352		0x4352
#define CHIPREV_NO_SROM4360	3	/* first revision without srom4360 */

/* srom-otp.md, "otp_read_pci" */
#define OTP_REGION_HW		1	/* the hardware region: the SROM image */
#define OTP_READ_WORDS		0x180
#define OTP_CRC_BYTES_220	0x1b8
#define OTP_CRC_BYTES_230	0x1cc
#define OTP_CRC_BYTES_234	0x1d4

/* srom-otp.md, "srom_read", step 1 */
#define SROM_READ_MAX_BYTES	0x300

/* srom-otp.md, "initvars_flash_pci" */
#define FLASH_BYTES		0x10000
#define FLASH_ROOM		0x1000	/* the work buffer of srom_var_init */
#define DEVPATH_SIZE		16
#define CODED_SIZE		16
#define CODED_PROBE_NAME	"devid"
#define CODED_PROBE_LEN		5	/* strlen("devid") */

/* srom-otp.md, "varbuf_append" and "Conversion" */
#define VAR_VALUE_SEP		'='

/* srom-otp.md, "Conversion", the table flags */
#define SROM_F_MORE		0x001
#define SROM_F_NOFFS		0x002
#define SROM_F_PRHEX		0x004
#define SROM_F_PRSIGN		0x008
#define SROM_F_CCODE		0x010
#define SROM_F_ETHADDR		0x020
#define SROM_F_LEDDC		0x040
#define SROM_F_NOVAR		0x080
#define SROM_F_ARRAY		0x100

/* srom-otp.md, "Conversion": the revisions with paths and with arrays */
#define SROM_REV_FIRST_PATHS	4	/* rev > 3 */
#define SROM_REV_FIRST_ARRAYS	10	/* rev > 9 */
#define SROM_REV_11		11

/* srom-otp.md, "Conversion", (B): path layout of revision 11 */
#define SROM11_PATHS		3
#define SROM11_PATH_BASE	108
#define SROM11_PATH_WORDS	20

/* srom-otp.md, "Function-static state in .bss": the text of an array */
#define ARRAY_TEXT_SIZE		0x100

/* srom-otp.md, "Conversion", (A) 3: the MAC address in three words */
#define ETHER_ADDR_WORDS	3
#define ETHER_ADDR_LEN		6
#define ETHER_TEXT_SIZE		18	/* "xx:xx:xx:xx:xx:xx" and the NUL */

/* srom-otp.md, "Conversion", (A) 9: country code and LED duty cycle */
#define BYTE_BITS		8
#define BYTE_MASK		0xff
#define LEDDC_ON_SHIFT		8	/* low byte of the word to bits 8..15 */
#define LEDDC_OFF_SHIFT		24	/* high byte of the word to bits 24..31 */

#define U32_ALL_ONES		0xffffffffU
#define U32_BITS_MASK		31	/* 32-bit shifts count modulo 32 */

/*
 * srom-otp.md, "Conversion": one entry of a layout table. The tables below
 * hold the entries that apply to revision 11, in the object's order.
 */
struct srom_entry {
	const char *name;	/* "" for continuation entries and array elements */
	u16 off;		/* word in the image (per-path: relative to the path base) */
	u16 mask;		/* bits of the word that hold the value */
	u16 flags;		/* SROM_F_* */
};

/* the head entry of a variable */
#define S11_VAR(n, o, m, f)	{ (n), (o), (m), (f) }
/* the next more significant 16 bits of the variable before it (MORE) */
#define S11_HI(o)		{ "", (o), 0xffff, 0 }
/* an element of an array that has further elements */
#define S11_EL(o, m, f)		{ "", (o), (m), (f) | SROM_F_ARRAY }
/* the last element of an array */
#define S11_END(o, m, f)	{ "", (o), (m), (f) }

#define HEX			SROM_F_PRHEX
#define HEX_HI			(SROM_F_PRHEX | SROM_F_MORE)
#define HEX_ARR			(SROM_F_PRHEX | SROM_F_ARRAY)
#define UNS_ARR			SROM_F_ARRAY

/*
 * srom-otp.md, "The conversion table for SROM revision 11 (complete)", rows
 * 1..125 (the common table; the object's entry numbers in the comments).
 */
static const struct srom_entry srom11_common[] = {
	S11_VAR("devid", 48, 0xffff, SROM_F_NOVAR),			/* 0 */
	S11_VAR("boardrev", 65, 0xffff, HEX),				/* 3 */
	S11_VAR("boardflags", 66, 0xffff, HEX_HI), S11_HI(67),		/* 13, 14 */
	S11_VAR("boardflags2", 68, 0xffff, HEX_HI), S11_HI(69),		/* 19, 20 */
	S11_VAR("boardtype", 2, 0xffff, HEX),				/* 21 */
	S11_VAR("subvid", 3, 0xffff, HEX),				/* 22 */
	S11_VAR("boardflags3", 70, 0xffff, HEX_HI), S11_HI(71),		/* 374, 375 */
	S11_VAR("boardnum", 74, 0xffff, 0),				/* 376 */
	S11_VAR("macaddr", 72, 0xffff, SROM_F_ETHADDR),			/* 377 */
	S11_VAR("ccode", 75, 0xffff, SROM_F_CCODE),			/* 378 */
	S11_VAR("regrev", 76, 0x00ff, 0),				/* 379 */
	S11_VAR("ledbh0", 77, 0x00ff, SROM_F_NOFFS),			/* 380 */
	S11_VAR("ledbh1", 77, 0xff00, SROM_F_NOFFS),			/* 381 */
	S11_VAR("ledbh2", 78, 0x00ff, SROM_F_NOFFS),			/* 382 */
	S11_VAR("ledbh3", 78, 0xff00, SROM_F_NOFFS),			/* 383 */
	S11_VAR("leddc", 79, 0xffff, SROM_F_LEDDC | SROM_F_NOFFS),	/* 384 */
	S11_VAR("aa2g", 80, 0x00ff, 0),					/* 385 */
	S11_VAR("aa5g", 80, 0xff00, 0),					/* 386 */
	S11_VAR("agbg0", 81, 0xff00, 0),				/* 387 */
	S11_VAR("agbg1", 81, 0x00ff, 0),				/* 388 */
	S11_VAR("agbg2", 82, 0xff00, 0),				/* 389 */
	S11_VAR("aga0", 82, 0x00ff, 0),					/* 390 */
	S11_VAR("aga1", 83, 0xff00, 0),					/* 391 */
	S11_VAR("aga2", 83, 0x00ff, 0),					/* 392 */
	S11_VAR("txchain", 84, 0x000f, SROM_F_NOFFS),			/* 393 */
	S11_VAR("rxchain", 84, 0x00f0, SROM_F_NOFFS),			/* 394 */
	S11_VAR("antswitch", 84, 0xff00, SROM_F_NOFFS),			/* 395 */
	S11_VAR("tssiposslope2g", 85, 0x0001, 0),			/* 396 */
	S11_VAR("epagain2g", 85, 0x000e, 0),				/* 397 */
	S11_VAR("pdgain2g", 85, 0x01f0, 0),				/* 398 */
	S11_VAR("tworangetssi2g", 85, 0x0200, 0),			/* 399 */
	S11_VAR("papdcap2g", 85, 0x0400, 0),				/* 400 */
	S11_VAR("femctrl", 85, 0xf800, 0),				/* 401 */
	S11_VAR("tssiposslope5g", 86, 0x0001, 0),			/* 402 */
	S11_VAR("epagain5g", 86, 0x000e, 0),				/* 403 */
	S11_VAR("pdgain5g", 86, 0x01f0, 0),				/* 404 */
	S11_VAR("tworangetssi5g", 86, 0x0200, 0),			/* 405 */
	S11_VAR("papdcap5g", 86, 0x0400, 0),				/* 406 */
	S11_VAR("gainctrlsph", 86, 0xf800, 0),				/* 407 */
	S11_VAR("tempthresh", 87, 0xff00, 0),				/* 408 */
	S11_VAR("tempoffset", 87, 0x00ff, 0),				/* 409 */
	S11_VAR("rawtempsense", 88, 0x01ff, HEX),			/* 410 */
	S11_VAR("measpower", 88, 0xfe00, HEX),				/* 411 */
	S11_VAR("tempsense_slope", 89, 0x00ff, HEX),			/* 412 */
	S11_VAR("tempcorrx", 89, 0xfc00, HEX),				/* 413 */
	S11_VAR("tempsense_option", 89, 0x0300, HEX),			/* 414 */
	S11_VAR("xtalfreq", 90, 0xffff, 0),				/* 415 */
	S11_VAR("pa5gbw4080a1", 91, 0xffff, HEX_ARR),			/* 416..427 */
	S11_EL(94, 0xffff, HEX), S11_EL(103, 0xffff, HEX), S11_EL(106, 0xffff, HEX),
	S11_EL(148, 0xffff, HEX), S11_EL(149, 0xffff, HEX), S11_EL(150, 0xffff, HEX),
	S11_EL(151, 0xffff, HEX), S11_EL(152, 0xffff, HEX), S11_EL(153, 0xffff, HEX),
	S11_EL(154, 0xffff, HEX), S11_END(155, 0xffff, HEX),
	S11_VAR("phycal_tempdelta", 92, 0x00ff, 0),			/* 428 */
	S11_VAR("temps_period", 92, 0x0f00, 0),				/* 429 */
	S11_VAR("temps_hysteresis", 92, 0xf000, 0),			/* 430 */
	S11_VAR("measpower1", 93, 0x007f, HEX),				/* 431 */
	S11_VAR("measpower2", 93, 0x3f80, HEX),				/* 432 */
	S11_VAR("tssifloor2g", 95, 0x03ff, HEX),			/* 433 */
	S11_VAR("tssifloor5g", 96, 0x03ff, HEX_ARR),			/* 434..437 */
	S11_EL(97, 0x03ff, HEX), S11_EL(98, 0x03ff, HEX), S11_END(99, 0x03ff, HEX),
	S11_VAR("pdoffset2g40ma0", 100, 0x000f, 0),			/* 438 */
	S11_VAR("pdoffset2g40ma1", 100, 0x00f0, 0),			/* 439 */
	S11_VAR("pdoffset2g40ma2", 100, 0x0f00, 0),			/* 440 */
	S11_VAR("pdoffset2g40mvalid", 100, 0x8000, 0),			/* 441 */
	S11_VAR("pdoffset40ma0", 101, 0xffff, 0),			/* 442 */
	S11_VAR("pdoffset40ma1", 102, 0xffff, 0),			/* 443 */
	S11_VAR("pdoffset40ma2", 103, 0xffff, 0),			/* 444 */
	S11_VAR("pdoffset80ma0", 104, 0xffff, 0),			/* 445 */
	S11_VAR("pdoffset80ma1", 105, 0xffff, 0),			/* 446 */
	S11_VAR("pdoffset80ma2", 106, 0xffff, 0),			/* 447 */
	S11_VAR("subband5gver", 107, 0xffff, HEX),			/* 448 */
	S11_VAR("paparambwver", 200, 0xf000, 0),			/* 449 */
	S11_VAR("pa5gbw4080a0", 156, 0xffff, HEX_ARR),			/* 450..461 */
	S11_EL(157, 0xffff, HEX), S11_EL(158, 0xffff, HEX), S11_EL(159, 0xffff, HEX),
	S11_EL(160, 0xffff, HEX), S11_EL(161, 0xffff, HEX), S11_EL(162, 0xffff, HEX),
	S11_EL(163, 0xffff, HEX), S11_EL(164, 0xffff, HEX), S11_EL(165, 0xffff, HEX),
	S11_EL(166, 0xffff, HEX), S11_END(167, 0xffff, HEX),
	S11_VAR("pa5gbw40a0", 136, 0xffff, HEX_ARR),			/* 462..473 */
	S11_EL(137, 0xffff, HEX), S11_EL(138, 0xffff, HEX), S11_EL(139, 0xffff, HEX),
	S11_EL(140, 0xffff, HEX), S11_EL(141, 0xffff, HEX), S11_EL(142, 0xffff, HEX),
	S11_EL(143, 0xffff, HEX), S11_EL(144, 0xffff, HEX), S11_EL(145, 0xffff, HEX),
	S11_EL(146, 0xffff, HEX), S11_END(147, 0xffff, HEX),
	S11_VAR("pa5gbw80a0", 156, 0xffff, HEX_ARR),			/* 474..485 */
	S11_EL(157, 0xffff, HEX), S11_EL(158, 0xffff, HEX), S11_EL(159, 0xffff, HEX),
	S11_EL(160, 0xffff, HEX), S11_EL(161, 0xffff, HEX), S11_EL(162, 0xffff, HEX),
	S11_EL(163, 0xffff, HEX), S11_EL(164, 0xffff, HEX), S11_EL(165, 0xffff, HEX),
	S11_EL(166, 0xffff, HEX), S11_END(167, 0xffff, HEX),
	S11_VAR("pa2gccka0", 129, 0xffff, HEX_ARR),			/* 486..488 */
	S11_EL(130, 0xffff, HEX), S11_END(131, 0xffff, HEX),
	S11_VAR("cckbw202gpo", 168, 0xffff, 0),				/* 489 */
	S11_VAR("cckbw20ul2gpo", 169, 0xffff, 0),			/* 490 */
	S11_VAR("mcsbw202gpo", 170, 0xffff, SROM_F_MORE), S11_HI(171),	/* 491, 492 */
	S11_VAR("mcsbw402gpo", 172, 0xffff, SROM_F_MORE), S11_HI(173),	/* 493, 494 */
	S11_VAR("dot11agofdmhrbw202gpo", 174, 0xffff, 0),		/* 495 */
	S11_VAR("ofdmlrbw202gpo", 175, 0xffff, 0),			/* 496 */
	S11_VAR("mcsbw205glpo", 176, 0xffff, SROM_F_MORE), S11_HI(177),	/* 497, 498 */
	S11_VAR("mcsbw405glpo", 178, 0xffff, SROM_F_MORE), S11_HI(179),	/* 499, 500 */
	S11_VAR("mcsbw805glpo", 180, 0xffff, SROM_F_MORE), S11_HI(181),	/* 501, 502 */
	S11_VAR("mcsbw205gmpo", 184, 0xffff, SROM_F_MORE), S11_HI(185),	/* 503, 504 */
	S11_VAR("mcsbw405gmpo", 186, 0xffff, SROM_F_MORE), S11_HI(187),	/* 505, 506 */
	S11_VAR("mcsbw805gmpo", 188, 0xffff, SROM_F_MORE), S11_HI(189),	/* 507, 508 */
	S11_VAR("mcsbw205ghpo", 192, 0xffff, SROM_F_MORE), S11_HI(193),	/* 509, 510 */
	S11_VAR("mcsbw405ghpo", 194, 0xffff, SROM_F_MORE), S11_HI(195),	/* 511, 512 */
	S11_VAR("mcsbw805ghpo", 196, 0xffff, SROM_F_MORE), S11_HI(197),	/* 513, 514 */
	S11_VAR("mcslr5glpo", 200, 0x0fff, 0),				/* 515 */
	S11_VAR("mcslr5gmpo", 201, 0xffff, 0),				/* 516 */
	S11_VAR("mcslr5ghpo", 202, 0xffff, 0),				/* 517 */
	S11_VAR("sb20in40hrpo", 203, 0xffff, 0),			/* 518 */
	S11_VAR("sb20in80and160hr5glpo", 204, 0xffff, 0),		/* 519 */
	S11_VAR("sb40and80hr5glpo", 205, 0xffff, 0),			/* 520 */
	S11_VAR("sb20in80and160hr5gmpo", 206, 0xffff, 0),		/* 521 */
	S11_VAR("sb40and80hr5gmpo", 207, 0xffff, 0),			/* 522 */
	S11_VAR("sb20in80and160hr5ghpo", 208, 0xffff, 0),		/* 523 */
	S11_VAR("sb40and80hr5ghpo", 209, 0xffff, 0),			/* 524 */
	S11_VAR("sb20in40lrpo", 210, 0xffff, 0),			/* 525 */
	S11_VAR("sb20in80and160lr5glpo", 211, 0xffff, 0),		/* 526 */
	S11_VAR("sb40and80lr5glpo", 212, 0xffff, 0),			/* 527 */
	S11_VAR("sb20in80and160lr5gmpo", 213, 0xffff, 0),		/* 528 */
	S11_VAR("sb40and80lr5gmpo", 214, 0xffff, 0),			/* 529 */
	S11_VAR("sb20in80and160lr5ghpo", 215, 0xffff, 0),		/* 530 */
	S11_VAR("sb40and80lr5ghpo", 216, 0xffff, 0),			/* 531 */
	S11_VAR("dot11agduphrpo", 217, 0xffff, 0),			/* 532 */
	S11_VAR("dot11agduplrpo", 218, 0xffff, 0),			/* 533 */
	S11_VAR("sar2g", 221, 0x00ff, 0),				/* 534 */
	S11_VAR("sar5g", 221, 0xff00, 0),				/* 535 */
	S11_VAR("noiselvl2ga0", 222, 0x001f, 0),			/* 536 */
	S11_VAR("noiselvl2ga1", 222, 0x03e0, 0),			/* 537 */
	S11_VAR("noiselvl2ga2", 222, 0x7c00, 0),			/* 538 */
	S11_VAR("noiselvl5ga0", 223, 0x001f, UNS_ARR),			/* 539..542 */
	S11_EL(224, 0x001f, 0), S11_EL(225, 0x001f, 0), S11_END(226, 0x001f, 0),
	S11_VAR("noiselvl5ga1", 223, 0x03e0, UNS_ARR),			/* 543..546 */
	S11_EL(224, 0x03e0, 0), S11_EL(225, 0x03e0, 0), S11_END(226, 0x03e0, 0),
	S11_VAR("noiselvl5ga2", 223, 0x7c00, UNS_ARR),			/* 547..550 */
	S11_EL(224, 0x7c00, 0), S11_EL(225, 0x7c00, 0), S11_END(226, 0x7c00, 0),
	S11_VAR("rxgainerr2ga0", 227, 0x003f, 0),			/* 551 */
	S11_VAR("rxgainerr2ga1", 227, 0x07c0, 0),			/* 552 */
	S11_VAR("rxgainerr2ga2", 227, 0xf800, 0),			/* 553 */
	S11_VAR("rxgainerr5ga0", 228, 0x003f, UNS_ARR),			/* 554..557 */
	S11_EL(229, 0x003f, 0), S11_EL(230, 0x003f, 0), S11_END(231, 0x003f, 0),
	S11_VAR("rxgainerr5ga1", 228, 0x07c0, UNS_ARR),			/* 558..561 */
	S11_EL(229, 0x07c0, 0), S11_EL(230, 0x07c0, 0), S11_END(231, 0x07c0, 0),
	S11_VAR("rxgainerr5ga2", 228, 0xf800, UNS_ARR),			/* 562..565 */
	S11_EL(229, 0xf800, 0), S11_EL(230, 0xf800, 0), S11_END(231, 0xf800, 0),
	S11_VAR("rpcal2g", 182, 0xffff, 0),				/* 566 */
	S11_VAR("rpcal5gb0", 183, 0xffff, 0),				/* 567 */
	S11_VAR("rpcal5gb1", 190, 0xffff, 0),				/* 568 */
	S11_VAR("rpcal5gb2", 191, 0xffff, 0),				/* 569 */
	S11_VAR("rpcal5gb3", 198, 0xffff, 0),				/* 570 */
	S11_VAR("txidxcap2g", 212, 0x0ff0, 0),				/* 571 */
	S11_VAR("txidxcap5g", 214, 0x0ff0, 0),				/* 572 */
};

/*
 * srom-otp.md, "The conversion table for SROM revision 11 (complete)", rows
 * 126..141 (the per-path table; words relative to the path base).
 */
static const struct srom_entry srom11_path[] = {
	S11_VAR("maxp2ga", 0, 0x00ff, 0),				/* 40 */
	S11_VAR("pa2ga", 1, 0xffff, HEX_ARR),				/* 41..43 */
	S11_EL(2, 0xffff, HEX), S11_END(3, 0xffff, HEX),
	S11_VAR("rxgains5gmelnagaina", 4, 0x0007, 0),			/* 44 */
	S11_VAR("rxgains5gmtrisoa", 4, 0x0078, 0),			/* 45 */
	S11_VAR("rxgains5gmtrelnabypa", 4, 0x0080, 0),			/* 46 */
	S11_VAR("rxgains5ghelnagaina", 4, 0x0700, 0),			/* 47 */
	S11_VAR("rxgains5ghtrisoa", 4, 0x7800, 0),			/* 48 */
	S11_VAR("rxgains5ghtrelnabypa", 4, 0x8000, 0),			/* 49 */
	S11_VAR("rxgains2gelnagaina", 5, 0x0007, 0),			/* 50 */
	S11_VAR("rxgains2gtrisoa", 5, 0x0078, 0),			/* 51 */
	S11_VAR("rxgains2gtrelnabypa", 5, 0x0080, 0),			/* 52 */
	S11_VAR("rxgains5gelnagaina", 5, 0x0700, 0),			/* 53 */
	S11_VAR("rxgains5gtrisoa", 5, 0x7800, 0),			/* 54 */
	S11_VAR("rxgains5gtrelnabypa", 5, 0x8000, 0),			/* 55 */
	S11_VAR("maxp5ga", 6, 0x00ff, UNS_ARR),				/* 56..59 */
	S11_EL(6, 0xff00, 0), S11_EL(7, 0x00ff, 0), S11_END(7, 0xff00, 0),
	S11_VAR("pa5ga", 8, 0xffff, HEX_ARR),				/* 60..71 */
	S11_EL(9, 0xffff, HEX), S11_EL(10, 0xffff, HEX), S11_EL(11, 0xffff, HEX),
	S11_EL(12, 0xffff, HEX), S11_EL(13, 0xffff, HEX), S11_EL(14, 0xffff, HEX),
	S11_EL(15, 0xffff, HEX), S11_EL(16, 0xffff, HEX), S11_EL(17, 0xffff, HEX),
	S11_EL(18, 0xffff, HEX), S11_END(19, 0xffff, HEX),
};

#undef HEX
#undef HEX_HI
#undef HEX_ARR
#undef UNS_ARR
#undef S11_VAR
#undef S11_HI
#undef S11_EL
#undef S11_END

/* srom-otp.md, "The variable buffer ("varbuf")" */
struct srom_varbuf {
	char *base;		/* start of the work buffer */
	char *cur;		/* where the next string is written */
	u32 size;		/* bytes left */
};

/*
 * srom-otp.md, "Function-static state in .bss": an array variable being
 * built (the object keeps the common one in .bss and the per-path one in
 * locals; here both live in the state of one conversion).
 */
struct srom_array_text {
	const char *name;	/* name of the array (its first entry) */
	u32 pos;		/* characters in `text`, with the trailing comma */
	bool active;		/* elements are being collected */
	char text[ARRAY_TEXT_SIZE];
};

/* srom-otp.md, "Conversion of an image into variables": one conversion */
struct srom_conv {
	struct srom_varbuf vb;
	const u16 *img;
	u32 rev;
	struct srom_array_text common;
	struct srom_array_text path;
};

/*
 * srom-otp.md, "varbuf_append" step 4 and "initvars_flash_pci" step 5:
 * strlen, without a call to the platform (it runs for every string of the
 * buffer at every append)
 */
static u32 srom_str_len(const char *s)
{
	const char *p = s;

	while (*p != '\0')
		p++;
	return (u32)(p - s);
}

/* srom-otp.md, "varbuf_append", step 4: strchr(s, '=') */
static const char *srom_str_eq(const char *s)
{
	for (; *s != '\0'; s++)
		if (*s == VAR_VALUE_SEP)
			return s;
	return NULL;
}

/*
 * srom-otp.md, "varbuf_append" step 4 and "initvars_flash_pci" step 5:
 * whether the first `len` bytes of `s` and `t` are equal (memcmp == 0)
 */
static bool srom_same_bytes(const char *s, const char *t, u32 len)
{
	u32 i;

	for (i = 0; i < len; i++)
		if (s[i] != t[i])
			return false;
	return true;
}

/* srom-otp.md, "varbuf_append", step 4: the memmove of `n` bytes down to `dst` */
static void srom_move_down(char *dst, const char *src, u32 n)
{
	u32 i;

	for (i = 0; i < n; i++)
		dst[i] = src[i];
}

/* srom-otp.md, "varbuf_append", step 1: whether anything can be appended */
static bool srom_varbuf_room(const struct srom_varbuf *b)
{
	return b->size > 1;
}

/*
 * srom-otp.md, "varbuf_append", steps 3..5: the text of length `n` that the
 * kernel's snprintf has just written at b->cur becomes the last string of the
 * buffer; an earlier string of the same name is removed. Result: the bytes
 * appended (n + 1), or 0.
 */
static u32 srom_varbuf_take(struct srom_varbuf *b, s32 n)
{
	const char *eq;
	char *s;
	u32 len, k;

	/* step 3 (the kernel's snprintf gives no negative length but -1) */
	if (n < 0 || n > (s32)(b->size - 2)) {
		b->size = 0;
		return 0;
	}

	/* step 4: only the first string of the same name is removed */
	eq = srom_str_eq(b->cur);
	if (eq) {
		len = (u32)(eq - b->cur);
		for (s = b->base; s < b->cur; s += srom_str_len(s) + 1) {
			if (!srom_same_bytes(s, b->cur, len) || s[len] != VAR_VALUE_SEP)
				continue;
			k = srom_str_len(s) + 1;
			srom_move_down(s, s + k, (u32)(b->cur + n + 1 - (s + k)));
			b->cur -= k;
			b->size += k;
			break;
		}
	}

	/* step 5 */
	b->cur += n + 1;
	b->size -= n + 1;
	return n + 1;
}

/*
 * srom-otp.md, "varbuf_append": format a string at the end of the buffer
 * with the kernel's snprintf (steps 1, 2) and append it (steps 3..5). The
 * result of the procedure (srom_varbuf_take) is used by no caller.
 */
#define srom_varbuf_append(b, ...)						\
	do {									\
		if (srom_varbuf_room(b))					\
			srom_varbuf_take((b), hw_snprintf((b)->cur, (b)->size,	\
							  __VA_ARGS__));	\
	} while (0)

/* srom-otp.md, "Conversion": number of the lowest set bit of `mask` (0 for 0) */
static u32 srom_mask_lo(u32 mask)
{
	u32 lo = 0;

	if (mask == 0)
		return 0;
	while (!(mask & 1)) {
		mask >>= 1;
		lo++;
	}
	return lo;
}

/* srom-otp.md, "Conversion": highest set bit - lowest set bit + 1 (0 for 0) */
static u32 srom_mask_width(u32 mask)
{
	u32 hi = U32_BITS_MASK;

	if (mask == 0)
		return 0;
	while (!(mask & (1U << hi)))
		hi--;
	return hi - srom_mask_lo(mask) + 1;
}

/* srom-otp.md, "Conversion": value = (word[off] & mask) >> lo */
static u32 srom_field(const u16 *img, u32 off, u32 mask)
{
	return (img[off] & mask) >> srom_mask_lo(mask);
}

/* srom-otp.md, "Conversion", (A) 6: all ones over `width` bits (shift modulo 32) */
static u32 srom_all_ones(u32 width)
{
	return (1U << (width & U32_BITS_MASK)) - 1;
}

/* srom-otp.md, "Conversion", (A) 8 and 9: PRSIGN and bit width-1 of value set */
static bool srom_is_negative(u32 flags, u32 value, u32 width)
{
	return (flags & SROM_F_PRSIGN) && width > 0 && ((value >> (width - 1)) & 1);
}

/* srom-otp.md, "Conversion", (A) 8 and 9: value | (-1 << width) */
static s32 srom_sign_extend(u32 value, u32 width)
{
	return (s32)(value | (U32_ALL_ONES << (width & U32_BITS_MASK)));
}

/* srom-otp.md, "Conversion", (A) 9: the LED duty cycle, 0xHHLL -> 0xHH00LL00 */
static s32 srom_leddc(u32 value)
{
	return (s32)(((value & BYTE_MASK) << LEDDC_ON_SHIFT) |
		     ((value >> BYTE_BITS) << LEDDC_OFF_SHIFT));
}

/* srom-otp.md, "Function-static state in .bss": no array is being built */
static void srom_array_init(struct srom_array_text *a)
{
	a->name = NULL;
	a->pos = 0;
	a->active = false;
}

/* srom-otp.md, "Conversion", (A) 4 and (B) 2: start an array named `name` */
static void srom_array_start(struct srom_array_text *a, const char *name)
{
	a->pos = 0;
	a->name = name;
	hw_memset(a->text, 0, sizeof(a->text));
	a->active = true;
}

/*
 * srom-otp.md, "Conversion", (A) 8 and (B) 5: write the text of one element
 * ("0x%x,", "%d," or "%u,") at the end of the array text; result the length
 * snprintf gives. With no room left at all the result is 0 (the "n <= 0"
 * case of the specification).
 */
static s32 srom_array_element(struct srom_array_text *a, u32 flags, u32 value, u32 width)
{
	char *p;
	size_t room;

	if (a->pos >= ARRAY_TEXT_SIZE)
		return 0;
	p = a->text + a->pos;
	room = ARRAY_TEXT_SIZE - a->pos;
	if (flags & SROM_F_PRHEX)
		return hw_snprintf(p, room, "0x%x,", value);
	if (srom_is_negative(flags, value, width))
		return hw_snprintf(p, room, "%d,", srom_sign_extend(value, width));
	return hw_snprintf(p, room, "%u,", value);
}

/*
 * srom-otp.md, "Conversion", (A) 8 and (B) 5: the array is complete: the
 * flag goes off and the last comma becomes the end of the text.
 */
static void srom_array_finish(struct srom_array_text *a)
{
	u32 end = a->pos < ARRAY_TEXT_SIZE ? a->pos : ARRAY_TEXT_SIZE;

	a->active = false;
	a->text[end - 1] = '\0';
}

/*
 * srom-otp.md, "Conversion", (A) 7 and (A) 8 "n <= 0": the entry to go on
 * with when the rest of an array is skipped from entry `last` on: past the
 * first entry without ARRAY, and the entry after that is not visited either.
 */
static u32 srom_array_skip(const struct srom_entry *t, u32 count, u32 last)
{
	u32 j = last;

	while (j < count && (t[j].flags & SROM_F_ARRAY))
		j++;
	return j + 2;
}

/* srom-otp.md, "Conversion", (A) 3: bcm_ether_ntoa */
static void srom_ether_ntoa(const u8 *ea, char *text)
{
	hw_snprintf(text, ETHER_TEXT_SIZE, "%02x:%02x:%02x:%02x:%02x:%02x",
		    (u32)ea[0], (u32)ea[1], (u32)ea[2], (u32)ea[3], (u32)ea[4], (u32)ea[5]);
}

/*
 * srom-otp.md, "Conversion", (A) 3: the MAC address, for k = 0..2 the high
 * byte of word off + k, then its low byte.
 */
static void srom_append_ethaddr(struct srom_conv *c, const struct srom_entry *e)
{
	u8 ea[ETHER_ADDR_LEN];
	char text[ETHER_TEXT_SIZE];
	u32 k, w;

	for (k = 0; k < ETHER_ADDR_WORDS; k++) {
		w = c->img[e->off + k];
		ea[2 * k] = (u8)(w >> BYTE_BITS);
		ea[2 * k + 1] = (u8)(w & BYTE_MASK);
	}
	srom_ether_ntoa(ea, text);
	srom_varbuf_append(&c->vb, "%s=%s", e->name, text);
}

/*
 * srom-otp.md, "Conversion", (A) 9: a common variable that is not part of an
 * array, formatted by the flags of its head entry.
 */
static void srom_append_common_scalar(struct srom_conv *c, const struct srom_entry *e,
				      u32 value, u32 width)
{
	u32 flags = e->flags;

	if (flags & SROM_F_CCODE) {
		if (value == 0)
			srom_varbuf_append(&c->vb, "ccode=");
		else
			srom_varbuf_append(&c->vb, "ccode=%c%c", (int)(value >> BYTE_BITS),
					   (int)(value & BYTE_MASK));
	} else if (flags & SROM_F_LEDDC) {
		srom_varbuf_append(&c->vb, "leddc=%d", srom_leddc(value));
	} else if (flags & SROM_F_PRHEX) {
		srom_varbuf_append(&c->vb, "%s=0x%x", e->name, value);
	} else if (srom_is_negative(flags, value, width)) {
		srom_varbuf_append(&c->vb, "%s=%d", e->name, srom_sign_extend(value, width));
	} else {
		srom_varbuf_append(&c->vb, "%s=%u", e->name, value);
	}
}

/*
 * srom-otp.md, "Conversion", (A) 8: the value of entries e..last is an
 * element of the common array being built. Result: the entry to go on with.
 */
static u32 srom_common_element(struct srom_conv *c, const struct srom_entry *t, u32 count,
			       u32 i, u32 last, u32 value, u32 width)
{
	struct srom_array_text *a = &c->common;
	s32 n;

	n = srom_array_element(a, t[i].flags, value, width);
	if (n <= 0) {
		a->active = false;
		return srom_array_skip(t, count, last);
	}
	a->pos += n;
	if (t[last].flags & SROM_F_ARRAY)
		return last + 1;
	srom_array_finish(a);
	srom_varbuf_append(&c->vb, "%s=%s", a->name, a->text);
	return last + 1;
}

/*
 * srom-otp.md, "Conversion", (A): one variable of the common table from
 * entry `i` on. Result: the entry to go on with. (Step 1, the revision mask:
 * the table holds only the entries of revision 11.)
 */
static u32 srom_common_entry(struct srom_conv *c, u32 i)
{
	const struct srom_entry *t = srom11_common;
	const u32 count = ARRAY_SIZE(srom11_common);
	const struct srom_entry *e = &t[i];
	u32 value, width, last;

	/* step 2 */
	if (e->flags & SROM_F_NOVAR)
		return i + 1;

	/* step 3 */
	if (e->flags & SROM_F_ETHADDR) {
		srom_append_ethaddr(c, e);
		return i + 1;
	}

	/* step 4 */
	if (c->rev >= SROM_REV_FIRST_ARRAYS && (e->flags & SROM_F_ARRAY) && !c->common.active)
		srom_array_start(&c->common, e->name);

	/* step 5: the continuation entries give the more significant bits */
	value = srom_field(c->img, e->off, e->mask);
	width = srom_mask_width(e->mask);
	for (last = i; (t[last].flags & SROM_F_MORE) && last + 1 < count; ) {
		last++;
		if (t[last].off != 0) {
			value += srom_field(c->img, t[last].off, t[last].mask) << width;
			width += srom_mask_width(t[last].mask);
		}
	}

	/* step 6 */
	if ((e->flags & SROM_F_NOFFS) && value == srom_all_ones(width))
		return last + 1;

	/* step 7: before revision 10 an array gives its first element as a scalar */
	if (c->rev < SROM_REV_FIRST_ARRAYS && (t[last].flags & SROM_F_ARRAY)) {
		srom_append_common_scalar(c, e, value, width);
		return srom_array_skip(t, count, last);
	}

	/* step 8 */
	if (c->common.active)
		return srom_common_element(c, t, count, i, last, value, width);

	/* steps 9, 10 */
	srom_append_common_scalar(c, e, value, width);
	return last + 1;
}

/* srom-otp.md, "Conversion", (A): the walk of the common table */
static void srom_walk_common(struct srom_conv *c)
{
	u32 i = 0;

	while (i < ARRAY_SIZE(srom11_common))
		i = srom_common_entry(c, i);
}

/*
 * srom-otp.md, "Conversion", (B) 5: the value of entry `i` is an element of
 * the per-path array being built. Result: the entry to go on with.
 */
static u32 srom_path_element(struct srom_conv *c, u32 p, u32 i, u32 value, u32 width)
{
	const struct srom_entry *t = srom11_path;
	struct srom_array_text *a = &c->path;
	s32 n;

	n = srom_array_element(a, t[i].flags, value, width);
	if (n <= 0) {
		a->active = false;
		return srom_array_skip(t, ARRAY_SIZE(srom11_path), i);
	}
	a->pos += n;
	if (t[i].flags & SROM_F_ARRAY)
		return i + 1;
	srom_array_finish(a);
	srom_varbuf_append(&c->vb, "%s%d=%s", a->name, (s32)p, a->text);
	return i + 1;
}

/*
 * srom-otp.md, "Conversion", (B): entry `i` of the per-path table for path
 * `p`, whose words start at `base`. Result: the entry to go on with.
 */
static u32 srom_path_entry(struct srom_conv *c, u32 p, u32 base, u32 i)
{
	const struct srom_entry *e = &srom11_path[i];
	u32 value, width;

	/* step 1 */
	if (e->flags & SROM_F_NOVAR)
		return i + 1;

	/* step 2 */
	if (c->rev >= SROM_REV_FIRST_ARRAYS && !c->path.active && (e->flags & SROM_F_ARRAY))
		srom_array_start(&c->path, e->name);

	/* step 3 */
	value = srom_field(c->img, base + e->off, e->mask);
	width = srom_mask_width(e->mask);

	/* step 4 */
	if ((e->flags & SROM_F_NOFFS) && value == srom_all_ones(width))
		return i + 1;

	/* step 5 */
	if (c->path.active)
		return srom_path_element(c, p, i, value, width);

	/* step 6: per-path scalars are never printed signed */
	if (e->flags & SROM_F_PRHEX)
		srom_varbuf_append(&c->vb, "%s%d=0x%x", e->name, (s32)p, value);
	else
		srom_varbuf_append(&c->vb, "%s%d=%d", e->name, (s32)p, (s32)value);
	return i + 1;
}

/* srom-otp.md, "Conversion", (B): the per-path walk of revision 11 */
static void srom_walk_paths(struct srom_conv *c)
{
	u32 p, i, base;

	for (p = 0; p < SROM11_PATHS; p++) {
		base = SROM11_PATH_BASE + p * SROM11_PATH_WORDS;
		i = 0;
		while (i < ARRAY_SIZE(srom11_path))
			i = srom_path_entry(c, p, base, i);
	}
}

/*
 * srom-otp.md, "Conversion of an image into variables (steps of 10)": the
 * variables of the image `img` of revision `rev` (rev & 31 == 11) in the
 * work buffer `buf`; result the end pointer (one past the final extra NUL).
 */
static char *srom_convert(const u16 *img, u32 rev, char *buf)
{
	struct srom_conv c;

	/* field by field: a whole-structure initializer would call memset */
	c.vb.base = buf;
	c.vb.cur = buf;
	c.vb.size = SROM_WORK_BYTES;
	c.img = img;
	c.rev = rev;
	srom_array_init(&c.common);
	srom_array_init(&c.path);

	srom_varbuf_append(&c.vb, "sromrev=%d", (s32)rev);
	srom_walk_common(&c);
	if (rev >= SROM_REV_FIRST_PATHS)
		srom_walk_paths(&c);
	*c.vb.cur = '\0';
	return c.vb.cur + 1;
}

/* srom-otp.md, "sprom_read_pci", step 4: hndcrc8 over `nbytes` bytes of the image */
static u8 srom_crc(const u16 *words, u32 nbytes)
{
	u8 crc = SROM_CRC8_INIT;
	u8 pair[2];
	u32 i;

	/* word n is at byte 2n, little endian */
	for (i = 0; i < nbytes / 2; i++) {
		pair[0] = (u8)(words[i] & BYTE_MASK);
		pair[1] = (u8)(words[i] >> BYTE_BITS);
		crc = bcm4360_crc8(pair, sizeof(pair), crc);
	}
	return crc;
}

/* srom-otp.md, "sprom_read_pci", step 2: chips 0x4331 and 0xa9a7 */
static bool srom_chip_is_4331(u32 chip)
{
	return chip == CHIP_4331 || chip == CHIP_A9A7;
}

/* srom-otp.md, "sprom_read_pci", step 2: chips 0xa9c4, 0x4360 and 0x4352 */
static bool srom_chip_is_4360(u32 chip)
{
	return chip == CHIP_A9C4 || chip == CHIP_4360 || chip == CHIP_4352;
}

/*
 * srom-otp.md, "sprom_read_pci": `nwords` words from word `wordoff` of the
 * SROM window into `buf`. Result 0 = read (and CRC good if checked), 1 =
 * blank or bad CRC. The chips 0x4331/0xa9a7 (si_chipcontrl_epa4331) are
 * not handled by this code and never get here.
 */
static s32 srom_sprom_read_pci(const struct bcm4360_srom_card *card, void *window, u32 wordoff,
			       u16 *buf, u32 nwords, bool check_crc)
{
	bool keep = srom_chip_is_4331(card->chip) || srom_chip_is_4360(card->chip);
	u32 saved = 0;
	s32 r = 0;
	u32 i;

	/* step 1 */
	if (keep)
		saved = bcm4360_srom_chipcontrol_read(card->ctx);

	/* step 2 */
	if (srom_chip_is_4360(card->chip) && card->chiprev < CHIPREV_NO_SROM4360)
		bcm4360_srom_chipcontrol_srom4360(card->ctx, true);

	/* step 3 */
	for (i = 0; i < nwords; i++)
		buf[i] = hw_read16(window, 2 * (wordoff + i));

	/* step 4 */
	if (check_crc)
		r = buf[0] == SROM_BLANK_WORD || srom_crc(buf, 2 * nwords) != SROM_CRC8_GOOD;

	/* step 5 */
	if (keep)
		bcm4360_srom_chipcontrol_restore(card->ctx, saved);
	return r;
}

/*
 * srom-otp.md, "otp_read_pci": the SROM image in the OTP's hardware region,
 * `nbytes` bytes of it into `buf`. Result 0 = good CRC, 1 = blank or bad
 * CRC, -1 = no memory, or the result of otp_read_region.
 */
static s32 srom_otp_read_pci(const struct bcm4360_srom_card *card, u16 *buf, u32 nbytes)
{
	u32 wlen = OTP_READ_WORDS;
	u16 *tmp;
	s32 r;

	/* step 1 */
	tmp = osl_malloc(card->osh, SROM_IMG_BYTES);
	if (!tmp)
		return SROM_ERR;
	hw_memset(tmp, 0, SROM_IMG_BYTES);

	/* step 2 */
	r = bcm4360_srom_otp_read_region(card->ctx, OTP_REGION_HW, tmp, &wlen);
	if (r != 0) {
		osl_mfree(card->osh, tmp, SROM_IMG_BYTES);
		return r;
	}

	/* step 3 */
	hw_memcpy(buf, tmp, nbytes);

	/* steps 4, 5 */
	if (tmp[0] == SROM_BLANK_WORD)
		r = 1;
	else if (srom_crc(tmp, OTP_CRC_BYTES_220) == SROM_CRC8_GOOD ||
		 srom_crc(tmp, OTP_CRC_BYTES_230) == SROM_CRC8_GOOD ||
		 srom_crc(tmp, OTP_CRC_BYTES_234) == SROM_CRC8_GOOD)
		r = 0;
	else
		r = 1;
	osl_mfree(card->osh, tmp, SROM_IMG_BYTES);
	return r;
}

/*
 * srom-otp.md, "srom_var_init", step 4 (and "srom_read", step 3): the SROM
 * window in BAR0, NULL if the capabilities say there is no SROM.
 */
static void *srom_window(const struct bcm4360_srom_card *card)
{
	u8 *regs = card->regs;

	if (card->ccrev <= SROM_CCREV_OLD_MAX)
		return regs + SROM_WINDOW_OLD;
	if (card->cccaps & CC_CAP_SROM)
		return regs + SROM_WINDOW;
	return NULL;
}

/* srom-otp.md, "srom_var_init": the revision byte in word `n` of the image */
static u32 srom_rev_byte(const u16 *img, u32 n)
{
	return img[n] & SROM_REV_BYTE;
}

/*
 * srom-otp.md, "srom_var_init", step 5 with an SROM: the three read attempts
 * (234, 220, 64 words). false: no image (step 6).
 */
static bool srom_image_from_sprom(const struct bcm4360_srom_card *card, void *window,
				  u16 *img, u32 *rev)
{
	/* step 5.1 */
	if (srom_sprom_read_pci(card, window, 0, img, SROM_WORDS_234, true) == 0) {
		*rev = img[SROM_SIG_234_WORD] == SROM_SIG_234 ?
		       srom_rev_byte(img, SROM_REV_WORD_234) : 0;
		return true;
	}

	/* step 5.2 */
	if (srom_sprom_read_pci(card, window, 0, img, SROM_WORDS_220, true) == 0) {
		*rev = img[SROM_SIG_220_WORD_A] == SROM_SIG_220 ||
		       img[SROM_SIG_220_WORD_B] == SROM_SIG_220 ?
		       srom_rev_byte(img, SROM_REV_WORD_220) : 0;
		return true;
	}

	/* step 5.3 */
	if (srom_sprom_read_pci(card, window, 0, img, SROM_WORDS_64, true) == 0) {
		*rev = srom_rev_byte(img, SROM_REV_WORD_64);
		if (*rev == SROM_REV1_CODE)
			*rev = SROM_REV1;
		return true;
	}

	/* step 5.4 */
	return false;
}

/*
 * srom-otp.md, "srom_var_init", step 5 without an SROM: the image in the
 * OTP. false: no image (step 6).
 */
static bool srom_image_from_otp(const struct bcm4360_srom_card *card, u16 *img, u32 *rev)
{
	if (srom_otp_read_pci(card, img, SROM_IMG_BYTES) != 0)
		return false;
	if (img[SROM_SIG_234_WORD] == SROM_SIG_234)
		*rev = srom_rev_byte(img, SROM_REV_WORD_234);
	else if (img[SROM_REV_WORD_220] == SROM_SIG_220)
		*rev = srom_rev_byte(img, SROM_REV_WORD_230);
	else
		*rev = srom_rev_byte(img, SROM_REV_WORD_220);
	return true;
}

/*
 * srom-otp.md, "initvars_table": a fresh buffer of exactly the used length
 * end - start in *vars / *count (none for 0 or 1 bytes).
 */
static s32 srom_initvars_table(void *osh, char *start, char *end, char **vars, u32 *count)
{
	s32 c = (s32)(end - start);
	char *v;

	/* step 2 */
	if (c <= 1) {
		*vars = NULL;
		*count = 0;
		return 0;
	}

	/* steps 3, 4 */
	v = osl_malloc(osh, (u32)c);
	if (!v)
		return BCME_NOMEM;
	hw_memcpy(v, start, (u32)c);
	*vars = v;
	*count = (u32)c;
	return 0;
}

/* srom-otp.md, "initvars_flash_pci", step 5: the device prefix of string `s` */
static u32 srom_flash_skip(const char *s, const char *devpath, u32 dlen,
			   const char *coded, u32 plen)
{
	if (srom_same_bytes(s, devpath, dlen))
		return dlen;
	if (plen != 0 && srom_same_bytes(s, coded, plen))
		return plen;
	return U32_ALL_ONES;	/* not a variable of this card */
}

/*
 * srom-otp.md, "initvars_flash_pci", steps 5 and 6: the variables of this
 * card in `flash`, without their prefix, at *cur; *cur is advanced past the
 * final extra NUL.
 */
static s32 srom_flash_copy(const char *flash, const char *devpath, const char *coded,
			   u32 plen, char **cur)
{
	u32 dlen = srom_str_len(devpath);
	u32 room = FLASH_ROOM;
	char *wp = *cur;
	const char *s;
	u32 skip, n;

	/* step 5 */
	for (s = flash; *s != '\0'; s += srom_str_len(s) + 1) {
		skip = srom_flash_skip(s, devpath, dlen, coded, plen);
		if (skip == U32_ALL_ONES)
			continue;
		n = srom_str_len(s) + 1 - skip;
		if (room < n)
			return BCME_BUFTOOSHORT;
		hw_memcpy(wp, s + skip, n);
		room -= n;
		wp += n;
	}

	/* step 6 */
	if (room == 0)
		return BCME_BUFTOOSHORT;
	*wp = '\0';
	*cur = wp + 1;
	return 0;
}

/*
 * srom-otp.md, "initvars_flash_pci": this card's variables of the platform's
 * NVRAM at *cur. Result 0, -27, -14 or the result of nvram_getall.
 */
static s32 srom_initvars_flash_pci(const struct bcm4360_srom_card *card, char **cur)
{
	char devpath[DEVPATH_SIZE];
	char coded[CODED_SIZE];
	char *flash;
	u32 plen = 0;
	s32 r;

	/* step 1 */
	hw_memset(coded, 0, sizeof(coded));
	flash = osl_malloc(card->osh, FLASH_BYTES);
	if (!flash)
		return BCME_NOMEM;

	/* step 2 */
	r = bcm4360_srom_nvram_getall(card->ctx, flash, FLASH_BYTES);
	if (r != 0)
		goto out;

	/* step 3 */
	devpath[0] = '\0';
	bcm4360_srom_devpath(card->ctx, devpath, DEVPATH_SIZE);

	/* step 4: "<i>:devid" -> "<i>:" */
	bcm4360_srom_coded_devpathvar(card->ctx, coded, CODED_SIZE, CODED_PROBE_NAME);
	if (srom_str_len(coded) > CODED_PROBE_LEN) {
		plen = srom_str_len(coded) - CODED_PROBE_LEN;
		coded[plen] = '\0';
	}

	/* steps 5, 6 */
	r = srom_flash_copy(flash, devpath, coded, plen, cur);

out:
	/* step 7 */
	osl_mfree(card->osh, flash, FLASH_BYTES);
	return r;
}

/*
 * srom-otp.md, "srom_var_init", step 6: no image; the revision from the
 * device-path variable "sromrev" of the NVRAM. Result 1: found (*rev set,
 * flash path); otherwise the result of srom_var_init (the image is freed).
 */
static s32 srom_rev_from_nvram(const struct bcm4360_srom_card *card, u16 *img, u32 *rev)
{
	const char *v;

	v = bcm4360_srom_getdevpathvar(card->ctx, "sromrev");
	if (v) {
		*rev = (u32)bcm4360_strtoul(v, NULL, 0) & SROM_REV_BYTE;
		return 1;
	}
	v = bcm4360_srom_getnvramflvar(card->ctx, "sromrev");
	osl_mfree(card->osh, img, SROM_IMG_BYTES);
	return v ? 0 : SROM_ERR;
}

/* srom-otp.md, "srom_var_init", step 7: whether the object accepts revision `rev` */
static bool srom_rev_accepted(u32 rev)
{
	return (SROM_REVS_ACCEPTED >> (rev & SROM_REV_SHIFT_MASK)) & 1;
}

/*
 * srom-otp.md, "srom_var_init", steps 8..10: the variables, from the NVRAM
 * (`flash`) or converted from the image; both buffers are freed.
 */
static s32 srom_build_vars(const struct bcm4360_srom_card *card, u16 *img, u32 rev, bool flash,
			   char **vars, u32 *count)
{
	char *buf, *end;
	s32 r;

	/* step 8 */
	buf = osl_malloc(card->osh, SROM_WORK_BYTES);
	if (!buf) {
		osl_mfree(card->osh, img, SROM_IMG_BYTES);
		return SROM_ERR_IMAGE;
	}

	if (flash) {
		/* step 9 */
		end = buf;
		r = srom_initvars_flash_pci(card, &end);
		if (r == 0)
			r = srom_initvars_table(card->osh, buf, end, vars, count);
	} else {
		/* step 10 */
		end = srom_convert(img, rev, buf);
		r = srom_initvars_table(card->osh, buf, end, vars, count);
	}
	osl_mfree(card->osh, buf, SROM_WORK_BYTES);
	osl_mfree(card->osh, img, SROM_IMG_BYTES);
	return r;
}

/*
 * srom-otp.md, "srom_var_init", for bus type 1 (PCI). Images of revisions
 * other than 11 (rev & 31 != 11), other bus types and the chips 0x4331/0xa9a7
 * give BCM4360_SROM_UNHANDLED.
 */
s32 bcm4360_srom_var_init(const struct bcm4360_srom_card *card, char **vars, u32 *count)
{
	bool flash = false;
	void *window;
	u32 rev = 0;
	bool found;
	u16 *img;
	s32 r;

	/* step 1 */
	if (!count || !vars)
		return 0;
	*vars = NULL;
	*count = 0;

	/* step 2 */
	if (card->bustype != SROM_BUS_PCI || srom_chip_is_4331(card->chip))
		return BCM4360_SROM_UNHANDLED;

	/* step 3 */
	if (!card->regs)
		return SROM_ERR;
	img = osl_malloc(card->osh, SROM_IMG_BYTES);
	if (!img)
		return SROM_ERR_IMAGE;

	/* step 4 */
	window = srom_window(card);

	/* step 5 */
	if (bcm4360_srom_sprom_available(card->ctx))
		found = srom_image_from_sprom(card, window, img, &rev);
	else
		found = srom_image_from_otp(card, img, &rev);

	/* step 6 */
	if (!found) {
		r = srom_rev_from_nvram(card, img, &rev);
		if (r != 1)
			return r;
		flash = true;
	}

	/* step 7 */
	if (!srom_rev_accepted(rev)) {
		osl_mfree(card->osh, img, SROM_IMG_BYTES);
		return SROM_ERR_IMAGE;
	}

	/* the conversion here is that of revision 11 */
	if (!flash && (rev & SROM_REV_SHIFT_MASK) != SROM_REV_11) {
		osl_mfree(card->osh, img, SROM_IMG_BYTES);
		return BCM4360_SROM_UNHANDLED;
	}

	/* steps 8..10 */
	return srom_build_vars(card, img, rev, flash, vars, count);
}

/*
 * srom-otp.md, "srom_read", for bus type 1 (PCI): `nbytes` bytes of the SROM
 * from byte `byteoff` (of the OTP image from its start if there is no SROM).
 * Result 0 = success, 1 = failure.
 */
s32 bcm4360_srom_read(const struct bcm4360_srom_card *card, u32 byteoff, u32 nbytes,
		      u16 *buf, bool check_crc)
{
	void *window;

	/* step 1 */
	if ((byteoff & 1) || (nbytes & 1) || byteoff + nbytes > SROM_READ_MAX_BYTES)
		return 1;

	/* steps 3..5: only PCI is handled here */
	if (card->bustype != SROM_BUS_PCI || srom_chip_is_4331(card->chip))
		return BCM4360_SROM_UNHANDLED;

	/* step 3 */
	if (!card->regs)
		return 1;
	if (!bcm4360_srom_sprom_available(card->ctx))
		return srom_otp_read_pci(card, buf, nbytes) != 0;
	window = srom_window(card);
	if (!window)
		return 1;

	/* step 2 */
	return srom_sprom_read_pci(card, window, byteoff / 2, buf, nbytes / 2, check_crc) != 0;
}

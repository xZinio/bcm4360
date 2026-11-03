/* SPDX-License-Identifier: ISC */
/*
 * The platform interface: everything the hardware layer needs from its
 * environment. There are three environments:
 *
 *  - the emulator (BCM4360_EMU): the functions below resolve to the imports
 *    of Broadcom's object (osl_readw() and friends), which the emulator
 *    provides for both programs alike, so that both can be compared;
 *  - the hybrid module (BCM4360_OSL, hybrid/README.md): the open code runs
 *    inside bcm4360.ko next to Broadcom's object and uses the same OS layer
 *    as the object, src/shared/linux_osl.c, so that it runs exactly as in
 *    the emulator;
 *  - a driver of its own on top of the kernel's bus (not written yet).
 *
 * The register spaces and the notation D11(), CC(), PCICFG() ... are those of
 * docs/re/spec/access.md.
 */
#ifndef BCM4360_HW_H
#define BCM4360_HW_H

#include <bcm4360/types.h>

/*
 * A card. The platform fills this in before it calls into the hardware layer.
 */
struct bcm4360_hw {
	void *bar0;		/* mapped BAR0 (32 kB) */
	void *bar1;		/* mapped BAR1, memory of the on-chip ARM */
	u32 bar1_size;
	void *pdev;		/* the platform's own handle (PCI device) */
	void *d11;		/* register window of the 802.11 core: bar0 */
	void *cc;		/* fixed window of ChipCommon: bar0 + 0x3000 */
	void *pcie;		/* fixed window of the PCIe core: bar0 + 0x2000 */
	void *wrap;		/* second movable window: bar0 + 0x1000 */
};

#if defined(BCM4360_EMU) || defined(BCM4360_OSL)

u8 osl_readb(volatile u8 *r);
u16 osl_readw(volatile u16 *r);
u32 osl_readl(volatile u32 *r);
void osl_writeb(u8 v, volatile u8 *r);
void osl_writew(u16 v, volatile u16 *r);
void osl_writel(u32 v, volatile u32 *r);
u32 osl_pci_read_config(void *osh, unsigned int offset, unsigned int size);
void osl_pci_write_config(void *osh, unsigned int offset, unsigned int size,
			  unsigned int val);
void osl_delay(unsigned int usec);
void *osl_malloc(void *osh, unsigned int size);
void osl_mfree(void *osh, void *addr, unsigned int size);
void *osl_memset(void *d, int c, size_t n);
void *osl_memcpy(void *d, const void *s, size_t n);
int osl_memcmp(const void *a, const void *b, size_t n);
int osl_strcmp(const char *a, const char *b);
int osl_strncmp(const char *a, const char *b, unsigned int n);
int osl_strlen(const char *s);
char *osl_strncpy(char *d, const char *s, unsigned int n);
int osl_snprintf(char *buf, size_t n, const char *fmt, ...)
	__attribute__((format(printf, 3, 4)));

/*
 * Files of the host (Broadcom's OS layer, src/shared/linux_osl.c): open by
 * name (NULL if there is no such file), read the next block (bytes read, 0 at
 * the end), close.
 */
void *osl_os_open_image(char *filename);
int osl_os_get_image_block(char *buf, int len, void *image);
void osl_os_close_image(void *image);

/*
 * Packets and DMA memory (the DMA engine, docs/re/spec/bmac-data.md): the
 * functions of Broadcom's OS layer src/shared/linux_osl.c, with its
 * prototypes. A packet is an opaque handle (struct sk_buff * on Linux).
 */
void *osl_pktget(void *osh, unsigned int len);
void osl_pktfree(void *osh, void *pkt, bool send);
u8 *osl_pktdata(void *osh, void *pkt);
unsigned int osl_pktlen(void *osh, void *pkt);
void *osl_pktnext(void *osh, void *pkt);
void osl_pktsetnext(void *pkt, void *next);
void osl_pktsetlen(void *osh, void *pkt, unsigned int len);
u8 *osl_pktpull(void *osh, void *pkt, int bytes);
void *osl_dma_alloc_consistent(void *osh, unsigned int size, u16 align_bits,
			       unsigned int *alloced, unsigned long *pap);
void osl_dma_free_consistent(void *osh, void *va, unsigned int size, unsigned long pa);
unsigned int osl_dma_map(void *osh, void *va, unsigned int size, int direction, void *pkt,
			 void *dmah);
void osl_dma_unmap(void *osh, unsigned int pa, unsigned int size, int direction);

static inline u8 hw_read8(void *base, u32 off)
{
	return osl_readb((volatile u8 *)base + off);
}

static inline u16 hw_read16(void *base, u32 off)
{
	return osl_readw((volatile u16 *)((u8 *)base + off));
}

static inline u32 hw_read32(void *base, u32 off)
{
	return osl_readl((volatile u32 *)((u8 *)base + off));
}

static inline void hw_write8(void *base, u32 off, u8 v)
{
	osl_writeb(v, (volatile u8 *)base + off);
}

static inline void hw_write16(void *base, u32 off, u16 v)
{
	osl_writew(v, (volatile u16 *)((u8 *)base + off));
}

static inline void hw_write32(void *base, u32 off, u32 v)
{
	osl_writel(v, (volatile u32 *)((u8 *)base + off));
}

static inline u32 hw_pcicfg_read(struct bcm4360_hw *hw, u32 off, u32 size)
{
	return osl_pci_read_config(hw->pdev, off, size);
}

static inline void hw_pcicfg_write(struct bcm4360_hw *hw, u32 off, u32 size, u32 v)
{
	osl_pci_write_config(hw->pdev, off, size, v);
}

/* busy wait */
static inline void hw_udelay(u32 usec)
{
	osl_delay(usec);
}

/* zeroed memory, may be called in atomic context */
static inline void *hw_zalloc(u32 size)
{
	void *p = osl_malloc(NULL, size);

	if (p)
		osl_memset(p, 0, size);
	return p;
}

static inline void hw_free(void *p, u32 size)
{
	osl_mfree(NULL, p, size);
}

/*
 * Variables that describe the board ("boardflags", "pa2ga0", ...), made from
 * the SROM: the value of a variable as text - a number in C notation, or
 * several of them separated by commas - or NULL if there is no such variable.
 */
const char *hw_getvar(struct bcm4360_hw *hw, const char *name);

/*
 * Data of Broadcom that the driver needs and does not contain: tables of the
 * PHY and of the radio, microcode, initial values. A driver loads it like
 * firmware (tools/re/fwcut.py cuts it out of Broadcom's object), in the
 * tests it is handed over from the memory of the object. `name` is the name
 * the specifications use ("acphy_mcs_tbl_rev0", "chan_tuning_2069rev4");
 * *size: bytes. The layout is the one the specifications describe, numbers
 * are little endian. NULL if there is no such data.
 */
const void *hw_fw_data(struct bcm4360_hw *hw, const char *name, u32 *size);

#define hw_memset(d, c, n)	osl_memset((d), (c), (n))
#define hw_memcpy(d, s, n)	osl_memcpy((d), (s), (n))
#define hw_memcmp(a, b, n)	osl_memcmp((a), (b), (n))
#define hw_strcmp(a, b)		osl_strcmp((a), (b))
#define hw_strncmp(a, b, n)	osl_strncmp((a), (b), (n))
#define hw_strlen(s)		osl_strlen(s)
/* strncpy of the OS layer (on Linux 7.2 and later: strscpy_pad) */
#define hw_strncpy(d, s, n)	osl_strncpy((d), (s), (n))
/* the kernel's snprintf: at most n - 1 characters and a NUL, result the
 * length the whole text would have; %c of 0 stores a zero byte */
#define hw_snprintf		osl_snprintf

#else
#error "no platform: define BCM4360_EMU (emulator) or BCM4360_OSL (hybrid module)"
#endif

/* register access relative to the windows of the card */
#define d11_read16(hw, off)		hw_read16((hw)->d11, (off))
#define d11_read32(hw, off)		hw_read32((hw)->d11, (off))
#define d11_write16(hw, off, v)		hw_write16((hw)->d11, (off), (v))
#define d11_write32(hw, off, v)		hw_write32((hw)->d11, (off), (v))
#define cc_read32(hw, off)		hw_read32((hw)->cc, (off))
#define cc_write32(hw, off, v)		hw_write32((hw)->cc, (off), (v))

#endif

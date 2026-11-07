/*
 * The DMA engine of one FIFO of the 802.11 core, 64-bit descriptors: attach
 * and probe, ring allocation, the channel state machine (init, reset,
 * suspend), transmit post and reclaim, receive fill, take and reclaim, and
 * the accessors of the function table.
 *
 * Written from docs/re/spec/bmac-data.md, section 1 (Overview, Data D1-D8,
 * Procedures P1-P53), for the 802.11 core revision 42 of the BCM4360 on PCIe.
 * Interface: open/include/bcm4360/dma.h.
 *
 * The 32-bit engine (D5) is not implemented: attach returns NULL when the
 * core lacks the "64 bit DMA" status flag.
 */
#include <bcm4360/dma.h>
#include <bcm4360/hw.h>

/* D2: registers of one channel (offsets from txregs / rxregs) */
#define DMA64_CONTROL		0x00	/* XC / RC */
#define DMA64_PTR		0x04	/* XP / RP: descriptor after the last one posted */
#define DMA64_ADDRLOW		0x08	/* XB / RB, low 32 bits of the ring base */
#define DMA64_ADDRHIGH		0x0c	/* XB / RB, high 32 bits of the ring base */
#define DMA64_STATUS0		0x10	/* XS0 / RS0 */
#define DMA64_STATUS1		0x14	/* XS1 / RS1 */

/* D2: control register bits (transmit and receive) */
#define CTRL_ENABLE		0x00000001	/* bit 0 */
#define XC_SUSPEND		0x00000002	/* bit 1: suspend request */
#define XC_LOOPBACK		0x00000004	/* bit 2 */
#define XC_MULTIOUTSTDRD_SHIFT	6		/* bits 6..7: outstanding reads */
#define XC_MULTIOUTSTDRD_MASK	0x000000c0
#define RC_OFFSET_SHIFT		1		/* bits 1..7: receive offset (bytes) */
#define RC_OVERFLOW_CONT	0x00000400	/* bit 10 */
#define CTRL_PARITY_DIS		0x00000800	/* bit 11 */
#define RC_GLOM			0x00004000	/* bit 14 */
#define CTRL_ADDREXT_SHIFT	16		/* bits 16..17: address extension */
#define CTRL_ADDREXT_MASK	0x00030000
#define CTRL_BURSTLEN_SHIFT	18		/* bits 18..20 */
#define CTRL_BURSTLEN_MASK	0x001c0000
#define CTRL_PREFETCHCTL_SHIFT	21		/* bits 21..23 */
#define CTRL_PREFETCHCTL_MASK	0x00e00000
#define CTRL_PREFETCHTHR_SHIFT	24		/* bits 24..25 */
#define CTRL_PREFETCHTHR_MASK	0x03000000
#define CTRL_CARD_GONE		0xffffffff	/* a control register reading all ones */

/* D2: status 0 state field (bits 28..31) and status 1 error field */
#define STATUS_STATE_SHIFT	28
#define STATE_DISABLED		0
#define STATE_ACTIVE		1
#define STATE_IDLE		2
#define STATE_STOPPED		3
#define STATUS1_ERR_SHIFT	28

/* D6: the 64-bit descriptor */
struct dma64_desc {
	u32 ctrl1;
	u32 ctrl2;
	u32 addrlow;
	u32 addrhigh;
};

#define DESC_SHIFT		4		/* 16 bytes per descriptor */
#define INDEX_MASK16		0xffff		/* indices are kept to 16 bits */
#define D64_CTRL1_SOF		0x80000000	/* first descriptor of a frame */
#define D64_CTRL1_EOF		0x40000000	/* last descriptor of a frame */
#define D64_CTRL1_IOC		0x20000000	/* interrupt on completion */
#define D64_CTRL1_EOT		0x10000000	/* last descriptor of the ring */
#define D64_CTRL2_BC_MASK	0x00007fff	/* buffer byte count */
#define D64_CTRL2_AE_SHIFT	16		/* address extension (bits 16..17) */
#define D64_CTRL2_PARITY	0x00040000	/* bit 18 */
#define DESC_RECLAIMED		0xdeadbeef	/* addrlow/addrhigh of a reclaimed descriptor */

/* Bits 31..30 of a 32-bit bus address (address extension, P11, P52) */
#define PCI32ADDR_HIGH		0xc0000000
#define PCI32ADDR_HIGH_SHIFT	30
#define PCI32ADDR_LOW_MASK	0x3fffffff

/* D4: control flags */
#define DMA_CTRL_PARITY		0x01	/* descriptors carry a parity bit */
#define DMA_CTRL_RX_OVF_CONT	0x02	/* receive overflow continue */
#define DMA_CTRL_RX_CHAIN	0x04	/* multi-buffer frames passed up as a chain */
#define DMA_CTRL_TX_KEEP	0x08	/* transmit reclaim does not free the packets */
#define DMA_CTRL_RX_ALIGN16	0x10	/* receive buffers aligned to 16 bytes */
#define RX_ALIGN_16		16	/* alignment of receive buffers with that flag */

/* Directions of osl_dma_map/unmap and of the ring functions (P3, P11) */
#define DMA_TX			1
#define DMA_RX			2

/* D3: transmit reclaim ranges */
#define DMA_RANGE_ALL		1
#define DMA_RANGE_TRANSFERED	3

/* P1: attach */
#define DMA_ENGINE_NAME_LEN	8
#define SFLAG_DMA64		0x1000		/* core status flag "64 bit DMA" */
#define BUSTYPE_SI		0		/* backplane (no host bus) */
#define BUSTYPE_PCI		1
#define PCIE_CORE_ID		0x820
#define PCIE2_CORE_ID		0x83c
#define CORE_ID_0x829		0x829		/* no address extension in revs 1, 2 */
#define CORE_ID_0x834		0x834		/* no address extension in revs 0, 1 */
#define RXEXTHDRROOM_DEFAULT	0xcc		/* rxextheadroom -1 */
#define RXBUFSIZE_EXTRA_MIN	0xcc		/* above: the head room is part of the size */
#define NRXPOST_MASK		0xffff		/* nrxpost: 16 bits used */
#define RXOFFSET_MASK		0xff		/* rxoffset: 8 bits used */
#define PROBE_ALL_ONES		0xffffffff	/* mask probe of the ring base */
#define PROBE_LOW_BITS		0x00000fff
#define CD_MASK_8K		0x1fff		/* mask when the low 12 bits are not kept */
#define CD_MASK_LOW		0xf		/* ORed to the pointer register otherwise */
#define PROBE_ALIGN		0x00000ff0	/* alignment probe of the ring base */
#define DMADESC_ALIGN_16	4		/* log2 of the ring alignment */
#define DMADESC_ALIGN_4K	0xc
#define DMADESC_ALIGN_8K	0xd
#define ALIGN_8K_MIN_DESCS	255		/* more descriptors: 8 KB alignment */
#define DDOFFSET_PCIE64		0x80000000	/* high word: the host's 64-bit space */
#define DDOFFSET_PCI_CHIP	0x80000000	/* low word for the chips of P1 step 9 */
#define DDOFFSET_PCI		0x40000000	/* low word otherwise */
#define PCI_DMA_WINDOW		0x40000000	/* 1 GB PCI window (P1 step 14) */

/* P3: rings must not cross this boundary */
#define RING_BOUNDARY_8K	0x2000
#define RING_BOUNDARY_64K	0x10000

/* P2: address widths */
#define DMA_ADDRWIDTH_30	30
#define DMA_ADDRWIDTH_32	32
#define DMA_ADDRWIDTH_64	64

/* P7: extra delay when the transmit channel does not report "disabled" */
#define TXRESET_EXTRA_US	300

/* "Poll": counter 10009, step 10 (us), stop at 9 */
#define POLL_COUNT_START	10009
#define POLL_COUNT_END		9
#define POLL_STEP		10

/* P51: parameter ids */
#define PARAM_TXMULTIOUTSTDRD	0
#define PARAM_TXPREFETCHCTL	1
#define PARAM_TXPREFETCHTHRESH	2
#define PARAM_TXBURSTLEN	3
#define PARAM_RXPREFETCHCTL	0x100
#define PARAM_RXPREFETCHTHRESH	0x101
#define PARAM_RXBURSTLEN	0x102

/*
 * D1: the engine. The public part comes first (D8); the rest follows D1 in
 * order. The OS DMA handles of the rings and the per-descriptor map handles
 * are not kept: they are never allocated on this build (P1 notes, P4).
 */
struct bcm4360_dma {
	struct bcm4360_dma_pub pub;
	u32 *msg_level;
	u32 msg_level_default;		/* used when attach gets no msg_level */
	char name[DMA_ENGINE_NAME_LEN];
	void *osh;
	void *sih;			/* bus->ctx */
	bool dma64;
	bool addrext;
	void *txregs;
	void *rxregs;
	volatile struct dma64_desc *txd;
	volatile struct dma64_desc *rxd;
	u16 dmadesc_align;
	u16 ntxd;
	u16 txin;
	u16 txout;
	void **txp;
	unsigned long txdpa;
	unsigned long txdpaorig;
	u16 txdalign;
	u32 txdalloc;
	u32 xmtptrbase;
	u16 nrxd;
	u16 rxin;
	u16 rxout;
	void **rxp;
	unsigned long rxdpa;
	unsigned long rxdpaorig;
	u16 rxdalign;
	u32 rxdalloc;
	u32 rcvptrbase;
	u16 rxbufsize;
	u32 rxextrahdrroom;
	u32 nrxpost;
	u32 rxoffset;
	u32 ddoffsetlow;
	u32 ddoffsethigh;
	u32 dataoffsetlow;
	u32 dataoffsethigh;
	bool aligndesc_4k;
	u8 rxburstlen;
	u8 txburstlen;
	u8 txmultioutstdrd;
	u8 txprefetchctl;
	u8 txprefetchthresh;
	u8 rxprefetchctl;
	u8 rxprefetchthresh;
	void *pktpool;
	u32 dma_avoidance_cnt;
	u32 xs0_cd_mask;
	u32 xs1_ad_mask;
	u32 rs0_cd_mask;
	u16 rs0cd;
	u16 xs0cd;
};

_Static_assert(__builtin_offsetof(struct bcm4360_dma, pub) == 0,
	       "the engine must begin with struct bcm4360_dma_pub");

/* docs/re/spec/bmac-data.md, D6: NEXT, PREV, ACTIVE of a ring of n entries */
static inline u16 ring_next(u32 i, u32 n)
{
	return (i + 1) & (n - 1);
}

static inline u16 ring_prev(u32 i, u32 n)
{
	return (i - 1) & (n - 1);
}

static inline u16 ring_active(u32 in, u32 out, u32 n)
{
	return (out - in) & (n - 1);
}

/* docs/re/spec/bmac-data.md, D6: status/pointer value -> descriptor index */
static inline u16 dma_index(u32 value, u32 mask, u32 base)
{
	return (u16)((((value & mask) - base) & mask) >> DESC_SHIFT);
}

/* docs/re/spec/bmac-data.md, D2: state field of status 0 */
static inline u32 dma_state(u32 status0)
{
	return status0 >> STATUS_STATE_SHIFT;
}

static inline u32 dma_min(u32 a, u32 b)
{
	return a < b ? a : b;
}

/* docs/re/spec/bmac-data.md, D6 and P23: txavail = ntxd - 1 - ACTIVE */
static void dma_txavail_update(struct bcm4360_dma *di)
{
	di->pub.txavail = di->ntxd - 1 - ring_active(di->txin, di->txout, di->ntxd);
}

/*
 * docs/re/spec/bmac-data.md, "Poll" (section Procedures): read the status
 * register until its state is one of `states` (a bit per state) or the
 * counter runs out (at most 1001 reads and 1000 delays of 10 us). Result:
 * the last value read.
 */
static u32 dma_poll_state(void *regs, u32 states)
{
	u32 countdown = POLL_COUNT_START;
	u32 v;

	for (;;) {
		v = hw_read32(regs, DMA64_STATUS0);
		if ((BIT(dma_state(v)) & states) || countdown == POLL_COUNT_END)
			break;
		countdown -= POLL_STEP;
		hw_udelay(POLL_STEP);
	}
	return v;
}

/* docs/re/spec/bmac-data.md, P53 dma64_addrext: the address-extension probe */
static bool dma64_addrext(void *regs)
{
	u32 v, w;

	v = hw_read32(regs, DMA64_CONTROL);
	hw_write32(regs, DMA64_CONTROL, v | CTRL_ADDREXT_MASK);
	w = hw_read32(regs, DMA64_CONTROL);
	v = hw_read32(regs, DMA64_CONTROL);
	hw_write32(regs, DMA64_CONTROL, v & ~CTRL_ADDREXT_MASK);
	return (w & CTRL_ADDREXT_MASK) == CTRL_ADDREXT_MASK;
}

/* docs/re/spec/bmac-data.md, P52 dma64_dd_upd: write one descriptor */
static void dma64_dd_upd(struct bcm4360_dma *di, volatile struct dma64_desc *ring,
			 u32 pa, u32 index, u32 flags, u32 bufcount)
{
	volatile struct dma64_desc *d = &ring[index];
	u32 ctrl2 = bufcount & D64_CTRL2_BC_MASK;
	u32 addrlow, par;

	if (!di->dataoffsetlow || !(pa & PCI32ADDR_HIGH)) {
		addrlow = pa + di->dataoffsetlow;
	} else {
		ctrl2 |= (pa >> PCI32ADDR_HIGH_SHIFT) << D64_CTRL2_AE_SHIFT;
		addrlow = (pa & PCI32ADDR_LOW_MASK) + di->dataoffsetlow;
	}

	d->addrlow = addrlow;
	d->addrhigh = di->dataoffsethigh;
	d->ctrl1 = flags;
	d->ctrl2 = ctrl2;

	if (di->pub.dmactrlflags & DMA_CTRL_PARITY) {
		par = addrlow ^ di->dataoffsethigh ^ flags ^ ctrl2;
		par ^= par >> 16;
		par ^= par >> 8;
		par ^= par >> 4;
		par ^= par >> 2;
		par ^= par >> 1;
		if (par & 1)
			d->ctrl2 = ctrl2 | D64_CTRL2_PARITY;
	}
}

/* docs/re/spec/bmac-data.md, P11 dma_ddtable_init: the ring base registers */
static void dma_ddtable_init(struct bcm4360_dma *di, u32 direction, unsigned long pa)
{
	void *regs = direction == DMA_TX ? di->txregs : di->rxregs;
	u32 pa32 = (u32)pa;
	u32 v;

	if (!di->aligndesc_4k) {
		if (direction == DMA_TX)
			di->xmtptrbase = pa32;
		else
			di->rcvptrbase = pa32;
	}

	if (!di->ddoffsetlow || !(pa32 & PCI32ADDR_HIGH)) {
		hw_write32(regs, DMA64_ADDRLOW, pa32 + di->ddoffsetlow);
		hw_write32(regs, DMA64_ADDRHIGH, di->ddoffsethigh);
		return;
	}

	hw_write32(regs, DMA64_ADDRLOW, (pa32 & PCI32ADDR_LOW_MASK) + di->ddoffsetlow);
	hw_write32(regs, DMA64_ADDRHIGH, di->ddoffsethigh);
	v = hw_read32(regs, DMA64_CONTROL);
	v = (v & ~CTRL_ADDREXT_MASK) |
	    (((pa32 >> PCI32ADDR_HIGH_SHIFT) << CTRL_ADDREXT_SHIFT) & CTRL_ADDREXT_MASK);
	hw_write32(regs, DMA64_CONTROL, v);
}

/*
 * docs/re/spec/bmac-data.md, P4 dma_ringalloc: coherent memory for a ring
 * that does not cross a `boundary`-aligned line.
 */
static void *dma_ringalloc(void *osh, u32 boundary, u32 size, u16 *alignbits,
			   unsigned int *alloced, unsigned long *pa)
{
	void *va;
	u32 align, start, s;
	u16 bits;

	va = osl_dma_alloc_consistent(osh, size, *alignbits, alloced, pa);
	if (!va)
		return NULL;

	align = 1U << *alignbits;
	start = ((u32)*pa + align - 1) & ~(align - 1);
	if (!(((start + size - 1) ^ start) & boundary))
		return va;

	/* floor(log2(size)): right shifts until size becomes 0, minus one */
	for (bits = 0, s = size; s; s >>= 1)
		bits++;
	*alignbits = bits - 1;
	osl_dma_free_consistent(osh, va, size, *pa);
	return osl_dma_alloc_consistent(osh, size, *alignbits, alloced, pa);
}

/* docs/re/spec/bmac-data.md, P3 dma_alloc: allocate the ring of one direction */
static bool dma_alloc(struct bcm4360_dma *di, u32 direction)
{
	u16 alignbits = di->dmadesc_align;
	unsigned int alloced = 0;
	unsigned long dpaorig, dpa, align;
	u32 mask, size, boundary;
	u16 dalign;
	void *va;

	if (direction == DMA_TX) {
		mask = di->xs0_cd_mask;
		size = di->ntxd * (u32)sizeof(struct dma64_desc);
	} else {
		mask = di->rs0_cd_mask;
		size = di->nrxd * (u32)sizeof(struct dma64_desc);
	}
	boundary = mask == CD_MASK_8K ? RING_BOUNDARY_8K : RING_BOUNDARY_64K;

	va = dma_ringalloc(di->osh, boundary, size, &alignbits, &alloced, &dpaorig);
	if (!va)
		return false;

	align = 1UL << alignbits;
	dpa = (dpaorig + align - 1) & ~(align - 1);
	dalign = (u16)(dpa - dpaorig);

	if (direction == DMA_TX) {
		di->txdpaorig = dpaorig;
		di->txdpa = dpa;
		di->txdalign = dalign;
		di->txd = (volatile struct dma64_desc *)((u8 *)va + dalign);
		di->txdalloc = alloced;
	} else {
		di->rxdpaorig = dpaorig;
		di->rxdpa = dpa;
		di->rxdalign = dalign;
		di->rxd = (volatile struct dma64_desc *)((u8 *)va + dalign);
		di->rxdalloc = alloced;
	}
	return true;
}

/*
 * docs/re/spec/bmac-data.md, P1 step 7/8: probe the current-descriptor mask
 * of one channel (write all ones to the ring base, read it back).
 */
static u32 dma64_probe_mask(void *regs)
{
	u32 v;

	hw_write32(regs, DMA64_ADDRLOW, PROBE_ALL_ONES);
	v = hw_read32(regs, DMA64_ADDRLOW);
	if (!(v & PROBE_LOW_BITS))
		return CD_MASK_8K;
	return hw_read32(regs, DMA64_PTR) | CD_MASK_LOW;
}

/*
 * docs/re/spec/bmac-data.md, P1 step 9: the chip ids whose buffers and rings
 * use the PCI window at 0x80000000 (the others: 0x40000000)
 */
static bool dma_pci_chip_high(u32 chip)
{
	switch (chip) {
	case 0x10f6:
	case 0x4322:
	case 0xa8d5:
	case 0xa8df:
	case 0xa867:
	case 0xa868:
	case 0xa8d6:
		return true;
	default:
		return false;
	}
}

static bool dma_bus_pcie(const struct bcm4360_dma_bus *bus)
{
	return bus->buscoreid == PCIE2_CORE_ID || bus->buscoreid == PCIE_CORE_ID;
}

/* docs/re/spec/bmac-data.md, P1 step 10: cores without address extension */
static bool dma_core_no_addrext(void *ctx)
{
	u32 rev;

	if (bcm4360_dma_env_coreid(ctx) == CORE_ID_0x829) {
		rev = bcm4360_dma_env_corerev(ctx);
		if (rev == 1 || rev == 2)
			return true;
	}
	if (bcm4360_dma_env_coreid(ctx) == CORE_ID_0x834) {
		rev = bcm4360_dma_env_corerev(ctx);
		if (rev == 0 || rev == 1)
			return true;
	}
	return false;
}

/* docs/re/spec/bmac-data.md, P1 steps 7 and 8: probes of the channels */
static void dma_attach_probe_channels(struct bcm4360_dma *di)
{
	u32 mask;

	if (di->rxregs) {
		mask = dma64_probe_mask(di->rxregs);
		di->rs0_cd_mask = mask;
		di->rxburstlen = (hw_read32(di->rxregs, DMA64_CONTROL) & CTRL_BURSTLEN_MASK) >>
				 CTRL_BURSTLEN_SHIFT;
		di->rxprefetchctl = (hw_read32(di->rxregs, DMA64_CONTROL) &
				     CTRL_PREFETCHCTL_MASK) >> CTRL_PREFETCHCTL_SHIFT;
		di->rxprefetchthresh = (hw_read32(di->rxregs, DMA64_CONTROL) &
					CTRL_PREFETCHTHR_MASK) >> CTRL_PREFETCHTHR_SHIFT;
	}

	if (di->txregs) {
		mask = dma64_probe_mask(di->txregs);
		di->xs0_cd_mask = mask;
		di->xs1_ad_mask = mask;
		di->txburstlen = (hw_read32(di->txregs, DMA64_CONTROL) & CTRL_BURSTLEN_MASK) >>
				 CTRL_BURSTLEN_SHIFT;
		di->txmultioutstdrd = (hw_read32(di->txregs, DMA64_CONTROL) &
				       XC_MULTIOUTSTDRD_MASK) >> XC_MULTIOUTSTDRD_SHIFT;
		di->txprefetchctl = (hw_read32(di->txregs, DMA64_CONTROL) &
				     CTRL_PREFETCHCTL_MASK) >> CTRL_PREFETCHCTL_SHIFT;
		di->txprefetchthresh = (hw_read32(di->txregs, DMA64_CONTROL) &
					CTRL_PREFETCHTHR_MASK) >> CTRL_PREFETCHTHR_SHIFT;
	}
}

/* docs/re/spec/bmac-data.md, P1 step 9: address offsets of rings and buffers */
static void dma_attach_offsets(struct bcm4360_dma *di, const struct bcm4360_dma_bus *bus)
{
	di->ddoffsetlow = 0;
	di->dataoffsetlow = 0;
	if (bus->bustype != BUSTYPE_PCI)
		return;

	if (dma_bus_pcie(bus) && di->dma64) {
		di->ddoffsetlow = 0;
		di->ddoffsethigh = DDOFFSET_PCIE64;
	} else {
		di->ddoffsetlow = dma_pci_chip_high(bus->chip) ? DDOFFSET_PCI_CHIP : DDOFFSET_PCI;
		di->ddoffsethigh = 0;
	}
	di->dataoffsetlow = di->ddoffsetlow;
	di->dataoffsethigh = di->ddoffsethigh;
}

/* docs/re/spec/bmac-data.md, P1 steps 10 and 11: address extension, alignment */
static void dma_attach_addrext_align(struct bcm4360_dma *di)
{
	void *regs = di->txregs ? di->txregs : di->rxregs;
	u32 v;

	if (dma_core_no_addrext(di->sih)) {
		di->addrext = false;
	} else if (!regs) {
		di->addrext = false;
	} else {
		/* the probe runs, its result is not used (P1 step 10) */
		dma64_addrext(regs);
		di->addrext = true;
	}

	if (regs) {
		hw_write32(regs, DMA64_ADDRLOW, PROBE_ALIGN);
		v = hw_read32(regs, DMA64_ADDRLOW);
		di->aligndesc_4k = v == 0;
	} else {
		di->aligndesc_4k = true;
	}

	if (!di->aligndesc_4k)
		di->dmadesc_align = DMADESC_ALIGN_16;
	else if (di->nrxd > ALIGN_8K_MIN_DESCS || di->ntxd > ALIGN_8K_MIN_DESCS)
		di->dmadesc_align = DMADESC_ALIGN_8K;
	else
		di->dmadesc_align = DMADESC_ALIGN_4K;
}

/* docs/re/spec/bmac-data.md, P1 step 12: zeroed packet pointer array */
static void **dma_alloc_pktptrs(void *osh, u32 n)
{
	void **a = osl_malloc(osh, n * (u32)sizeof(void *));

	if (a)
		osl_memset(a, 0, n * sizeof(void *));
	return a;
}

/* docs/re/spec/bmac-data.md, P1 dma_attach */
struct bcm4360_dma *bcm4360_dma_attach(void *osh, const char *name,
				       const struct bcm4360_dma_bus *bus, void *txregs,
				       void *rxregs, u32 ntxd, u32 nrxd, u32 rxbufsize,
				       s32 rxextheadroom, u32 nrxpost, u32 rxoffset,
				       u32 *msg_level)
{
	struct bcm4360_dma *di;
	u32 flags, i;

	/* step 1 */
	di = osl_malloc(osh, sizeof(*di));
	if (!di)
		return NULL;
	osl_memset(di, 0, sizeof(*di));

	/* step 2 */
	di->msg_level = msg_level ? msg_level : &di->msg_level_default;

	/* step 3 */
	flags = bcm4360_dma_env_core_sflags(bus->ctx, 0, 0);
	di->dma64 = (flags & SFLAG_DMA64) != 0;
	if (!di->dma64) {
		/* the 32-bit engine (D5) is not implemented */
		osl_mfree(osh, di, sizeof(*di));
		return NULL;
	}
	di->rxregs = rxregs;
	di->txregs = txregs;

	/* step 4 (through the table in the specification) */
	bcm4360_dma_ctrlflags(di, DMA_CTRL_PARITY | DMA_CTRL_RX_OVF_CONT, 0);

	/* step 5 */
	for (i = 0; name && i < DMA_ENGINE_NAME_LEN && name[i]; i++)
		di->name[i] = name[i];
	di->name[DMA_ENGINE_NAME_LEN - 1] = '\0';
	di->sih = bus->ctx;
	di->ntxd = (u16)ntxd;
	di->nrxd = (u16)nrxd;
	di->osh = osh;

	/* step 6 */
	di->rxextrahdrroom = rxextheadroom == -1 ? RXEXTHDRROOM_DEFAULT : (u32)rxextheadroom;
	if (rxbufsize > RXBUFSIZE_EXTRA_MIN)
		di->rxbufsize = (u16)(rxbufsize - di->rxextrahdrroom);
	else
		di->rxbufsize = (u16)rxbufsize;
	di->nrxpost = nrxpost & NRXPOST_MASK;
	di->rxoffset = rxoffset & RXOFFSET_MASK;

	/* steps 7 to 11 */
	dma_attach_probe_channels(di);
	dma_attach_offsets(di, bus);
	dma_attach_addrext_align(di);

	/* step 12 */
	if (di->ntxd) {
		di->txp = dma_alloc_pktptrs(osh, di->ntxd);
		if (!di->txp)
			goto fail;
	}
	if (di->nrxd) {
		di->rxp = dma_alloc_pktptrs(osh, di->nrxd);
		if (!di->rxp)
			goto fail;
	}

	/* step 13 */
	if (di->ntxd && !dma_alloc(di, DMA_TX))
		goto fail;
	if (di->nrxd && !dma_alloc(di, DMA_RX))
		goto fail;

	/* step 14 */
	if (!di->ddoffsetlow || di->addrext)
		return di;
	if (di->txdpa <= PCI_DMA_WINDOW && di->rxdpa <= PCI_DMA_WINDOW)
		return di;

fail:
	/* step 15 */
	bcm4360_dma_detach(di);
	return NULL;
}

/* docs/re/spec/bmac-data.md, P2 dma_addrwidth */
u32 bcm4360_dma_addrwidth(const struct bcm4360_dma_bus *bus, void *dmaregs)
{
	u32 flags = bcm4360_dma_env_core_sflags(bus->ctx, 0, 0);

	if (flags & SFLAG_DMA64) {
		if (bcm4360_dma_env_backplane64(bus->ctx) &&
		    (bus->bustype == BUSTYPE_SI ||
		     (bus->bustype == BUSTYPE_PCI && dma_bus_pcie(bus))))
			return DMA_ADDRWIDTH_64;
		return DMA_ADDRWIDTH_32;
	}

	if (bus->bustype != BUSTYPE_SI &&
	    !(bus->bustype == BUSTYPE_PCI && dma_bus_pcie(bus)) &&
	    !dma64_addrext(dmaregs))
		return DMA_ADDRWIDTH_30;
	return DMA_ADDRWIDTH_32;
}

/* docs/re/spec/bmac-data.md, P5 dma_detach (slot 0x000) */
void bcm4360_dma_detach(struct bcm4360_dma *di)
{
	void *osh = di->osh;

	if (di->txd)
		osl_dma_free_consistent(osh, (u8 *)di->txd - di->txdalign, di->txdalloc,
					di->txdpaorig);
	if (di->rxd)
		osl_dma_free_consistent(osh, (u8 *)di->rxd - di->rxdalign, di->rxdalloc,
					di->rxdpaorig);
	if (di->txp)
		osl_mfree(osh, di->txp, di->ntxd * (u32)sizeof(void *));
	if (di->rxp)
		osl_mfree(osh, di->rxp, di->nrxd * (u32)sizeof(void *));
	osl_mfree(osh, di, sizeof(*di));
}

/* docs/re/spec/bmac-data.md, P6 dma64_txinit (slot 0x008) */
void bcm4360_dma_txinit(struct bcm4360_dma *di)
{
	u32 v;

	if (!di->ntxd)
		return;

	/* step 1 */
	di->xs0cd = 0;
	di->txout = 0;
	di->txin = 0;
	di->pub.txavail = di->ntxd - 1;
	osl_memset((void *)di->txd, 0, di->ntxd * sizeof(struct dma64_desc));

	/* step 2: burst, outstanding reads, prefetch (fields not masked) */
	v = hw_read32(di->txregs, DMA64_CONTROL);
	v = (v & ~(CTRL_BURSTLEN_MASK | XC_MULTIOUTSTDRD_MASK)) |
	    (u32)di->txburstlen << CTRL_BURSTLEN_SHIFT |
	    (u32)di->txmultioutstdrd << XC_MULTIOUTSTDRD_SHIFT;
	v = (v & ~CTRL_PREFETCHCTL_MASK) | (u32)di->txprefetchctl << CTRL_PREFETCHCTL_SHIFT;
	v = (v & ~CTRL_PREFETCHTHR_MASK) | (u32)di->txprefetchthresh << CTRL_PREFETCHTHR_SHIFT;
	hw_write32(di->txregs, DMA64_CONTROL, v);

	/* step 3 */
	if (!di->aligndesc_4k)
		dma_ddtable_init(di, DMA_TX, di->txdpa);

	/* step 4 */
	v = hw_read32(di->txregs, DMA64_CONTROL) | CTRL_ENABLE;
	if (!(di->pub.dmactrlflags & DMA_CTRL_PARITY))
		v |= CTRL_PARITY_DIS;
	hw_write32(di->txregs, DMA64_CONTROL, v);

	/* step 5 */
	if (di->aligndesc_4k)
		dma_ddtable_init(di, DMA_TX, di->txdpa);
}

/* docs/re/spec/bmac-data.md, P7 dma64_txreset (slot 0x010) */
bool bcm4360_dma_txreset(struct bcm4360_dma *di)
{
	u32 v;

	if (!di->ntxd)
		return true;

	hw_write32(di->txregs, DMA64_CONTROL, XC_SUSPEND);
	dma_poll_state(di->txregs,
		       BIT(STATE_DISABLED) | BIT(STATE_IDLE) | BIT(STATE_STOPPED));
	hw_write32(di->txregs, DMA64_CONTROL, 0);
	v = dma_poll_state(di->txregs, BIT(STATE_DISABLED));
	if (dma_state(v) != STATE_DISABLED)
		hw_udelay(TXRESET_EXTRA_US);
	return dma_state(v) == STATE_DISABLED;
}

/* docs/re/spec/bmac-data.md, P12 dma64_txenabled (slot 0x018) */
bool bcm4360_dma_txenabled(struct bcm4360_dma *di)
{
	u32 v = hw_read32(di->txregs, DMA64_CONTROL);

	return v != CTRL_CARD_GONE && (v & CTRL_ENABLE);
}

/* docs/re/spec/bmac-data.md, P13 dma64_txsuspend (slot 0x020) */
void bcm4360_dma_txsuspend(struct bcm4360_dma *di)
{
	if (!di->ntxd)
		return;
	hw_write32(di->txregs, DMA64_CONTROL,
		   hw_read32(di->txregs, DMA64_CONTROL) | XC_SUSPEND);
}

/* docs/re/spec/bmac-data.md, P14 dma64_txresume (slot 0x028) */
void bcm4360_dma_txresume(struct bcm4360_dma *di)
{
	if (!di->ntxd)
		return;
	hw_write32(di->txregs, DMA64_CONTROL,
		   hw_read32(di->txregs, DMA64_CONTROL) & ~XC_SUSPEND);
}

/* docs/re/spec/bmac-data.md, P15 dma64_txsuspended (slot 0x030) */
bool bcm4360_dma_txsuspended(struct bcm4360_dma *di)
{
	if (!di->ntxd)
		return true;
	return (hw_read32(di->txregs, DMA64_CONTROL) & XC_SUSPEND) != 0;
}

/* docs/re/spec/bmac-data.md, P16 dma64_txsuspendedidle (slot 0x038) */
bool bcm4360_dma_txsuspendedidle(struct bcm4360_dma *di)
{
	if (!di->ntxd)
		return true;
	if (!(hw_read32(di->txregs, DMA64_CONTROL) & XC_SUSPEND))
		return false;
	return dma_state(hw_read32(di->txregs, DMA64_STATUS0)) == STATE_IDLE;
}

/* docs/re/spec/bmac-data.md, P23 dma64_txfast (slot 0x040) */
s32 bcm4360_dma_txfast(struct bcm4360_dma *di, void *p0, bool commit)
{
	u16 out = di->txout;
	u32 flags = 0;
	void *p, *next;
	u32 len, pa;
	u8 *data;

	/* step 1 */
	for (p = p0; p; p = next) {
		data = osl_pktdata(di->osh, p);
		len = osl_pktlen(di->osh, p);
		next = osl_pktnext(di->osh, p);

		if (ring_next(out, di->ntxd) == di->txin)
			goto outoftxd;
		if (!len)
			continue;

		pa = osl_dma_map(di->osh, data, len, DMA_TX, p, NULL);

		flags = p == p0 ? D64_CTRL1_SOF : 0;
		if (!next)
			flags |= D64_CTRL1_EOF | D64_CTRL1_IOC;
		if (out == di->ntxd - 1)
			flags |= D64_CTRL1_EOT;

		dma64_dd_upd(di, di->txd, pa, out, flags, len);

		out = ring_next(out, di->ntxd);
		if (out == di->txin)
			goto outoftxd;
	}

	/* step 2: the last buffer was empty (or there was none) */
	if (!(flags & D64_CTRL1_EOF))
		di->txd[ring_prev(out, di->ntxd)].ctrl1 = flags | D64_CTRL1_EOF | D64_CTRL1_IOC;

	/* step 3 */
	di->txp[ring_prev(out, di->ntxd)] = p0;
	di->txout = out;
	if (commit)
		hw_write32(di->txregs, DMA64_PTR,
			   ((u32)di->txout << DESC_SHIFT) + di->xmtptrbase);
	dma_txavail_update(di);
	return 0;

outoftxd:
	/* step 4 */
	osl_pktfree(di->osh, p0, true);
	di->pub.txnobuf++;
	di->pub.txavail = 0;
	return -1;
}

/* docs/re/spec/bmac-data.md, P24 dma64_txunframed (slot 0x048) */
s32 bcm4360_dma_txunframed(struct bcm4360_dma *di, void *buf, u32 len, bool commit)
{
	u32 flags, pa;

	if (ring_next(di->txout, di->ntxd) == di->txin) {
		di->pub.txnobuf++;
		di->pub.txavail = 0;
		return -1;
	}
	if (!len)
		return 0;

	pa = osl_dma_map(di->osh, buf, len, DMA_TX, NULL, NULL);
	flags = D64_CTRL1_SOF | D64_CTRL1_EOF | D64_CTRL1_IOC;
	if (di->txout == di->ntxd - 1)
		flags |= D64_CTRL1_EOT;
	dma64_dd_upd(di, di->txd, pa, di->txout, flags, len);
	di->txp[di->txout] = buf;
	di->txout = ring_next(di->txout, di->ntxd);
	if (commit)
		hw_write32(di->txregs, DMA64_PTR,
			   ((u32)di->txout << DESC_SHIFT) + di->xmtptrbase);
	dma_txavail_update(di);
	return 0;
}

/* docs/re/spec/bmac-data.md, P29 dma64_getpos (slot 0x050) */
void *bcm4360_dma_getpos(struct bcm4360_dma *di, bool direction_tx)
{
	u16 idx;
	void *p;

	if (direction_tx) {
		idx = dma_index(hw_read32(di->txregs, DMA64_STATUS0), di->xs0_cd_mask,
				di->xmtptrbase);
		p = di->txp[idx];
		return ring_active(di->txin, di->txout, di->ntxd) ? p : NULL;
	}

	idx = dma_index(hw_read32(di->rxregs, DMA64_STATUS0), di->rs0_cd_mask, di->rcvptrbase);
	p = di->rxp[idx];
	return ring_active(di->rxin, di->rxout, di->nrxd) ? p : NULL;
}

/* docs/re/spec/bmac-data.md, P17 dma64_txstopped (slot 0x058) */
bool bcm4360_dma_txstopped(struct bcm4360_dma *di)
{
	return dma_state(hw_read32(di->txregs, DMA64_STATUS0)) == STATE_STOPPED;
}

/* docs/re/spec/bmac-data.md, P26 dma64_txreclaim (slot 0x060) */
void bcm4360_dma_txreclaim(struct bcm4360_dma *di, s32 range)
{
	void *p;

	if (di->txin == di->txout)
		return;

	while ((p = bcm4360_dma_getnexttxp(di, range))) {
		if (!(di->pub.dmactrlflags & DMA_CTRL_TX_KEEP))
			osl_pktfree(di->osh, p, true);
	}
}

/*
 * docs/re/spec/bmac-data.md, P25 step 2: the end of the transmit reclaim for
 * a range other than "all"
 */
static u16 dma64_txreclaim_end(struct bcm4360_dma *di, s32 range)
{
	u16 end = di->xs0cd;
	u32 ad;

	if (di->txin == di->xs0cd) {
		end = dma_index(hw_read32(di->txregs, DMA64_STATUS0), di->xs0_cd_mask,
				di->xmtptrbase);
		di->xs0cd = end;
	}

	if (range == DMA_RANGE_TRANSFERED) {
		ad = hw_read32(di->txregs, DMA64_STATUS1);
		ad = ((((ad & di->xs1_ad_mask) - di->xmtptrbase) & di->xs0_cd_mask) &
		      INDEX_MASK16) >> DESC_SHIFT;
		if (ad != end)
			end = ring_prev(ad, di->ntxd);
	}
	return end;
}

/* docs/re/spec/bmac-data.md, P25 dma64_getnexttxp (slot 0x068) */
void *bcm4360_dma_getnexttxp(struct bcm4360_dma *di, s32 range)
{
	volatile struct dma64_desc *d;
	u16 start, end, i;
	u32 addrlow, ctrl2;
	void *p = NULL;

	/* step 1 */
	if (!di->ntxd)
		return NULL;
	start = di->txin;

	/* step 2 */
	if (range == DMA_RANGE_ALL)
		end = di->txout;
	else
		end = dma64_txreclaim_end(di, range);

	/* step 3 */
	if (start == 0 && end > di->txout)
		return NULL;

	/* step 4 */
	for (i = start; !p && i != end; i = ring_next(i, di->ntxd)) {
		d = &di->txd[i];
		addrlow = d->addrlow;
		ctrl2 = d->ctrl2;
		d->addrlow = DESC_RECLAIMED;
		d->addrhigh = DESC_RECLAIMED;

		p = di->txp[i];
		di->txp[i] = NULL;

		osl_dma_unmap(di->osh, addrlow - di->dataoffsetlow, ctrl2 & D64_CTRL2_BC_MASK,
			      DMA_TX);
	}

	/* step 5 */
	di->txin = i;
	dma_txavail_update(di);
	return p;
}

/* docs/re/spec/bmac-data.md, P27 dma64_peeknexttxp (slot 0x070) */
void *bcm4360_dma_peeknexttxp(struct bcm4360_dma *di)
{
	u16 end, i;

	if (!di->ntxd)
		return NULL;

	end = dma_index(hw_read32(di->txregs, DMA64_STATUS0), di->xs0_cd_mask, di->xmtptrbase);
	di->xs0cd = end;

	for (i = di->txin; i != end; i = ring_next(i, di->ntxd))
		if (di->txp[i])
			return di->txp[i];
	return NULL;
}

/* docs/re/spec/bmac-data.md, P28 dma64_peekntxp (slot 0x078) */
s32 bcm4360_dma_peekntxp(struct bcm4360_dma *di, s32 *n, void **pkts, s32 range)
{
	u16 end, cd, ad, i;
	s32 max, count = 0;
	u32 xs0, xs1;

	if (!di->ntxd) {
		*n = 0;
		return -1;
	}

	max = *n;
	*n = 0;

	if (range == DMA_RANGE_ALL) {
		end = di->txout;
	} else {
		xs0 = hw_read32(di->txregs, DMA64_STATUS0);
		xs1 = hw_read32(di->txregs, DMA64_STATUS1);
		cd = dma_index(xs0, di->xs0_cd_mask, di->xmtptrbase);
		di->xs0cd = cd;
		ad = (u16)((((xs1 & di->xs1_ad_mask) - di->xmtptrbase) & di->xs0_cd_mask) >>
			   DESC_SHIFT);
		end = ad == cd ? cd : ring_prev(ad, di->ntxd);
	}

	if (di->txin == 0 && end > di->txout)
		return -1;

	for (i = di->txin; i != end; i = ring_next(i, di->ntxd)) {
		if (!di->txp[i])
			continue;
		if (count >= max)
			break;
		pkts[count++] = di->txp[i];
	}
	*n = count;
	return 0;
}

/* docs/re/spec/bmac-data.md, P30 dma_txblock (slot 0x080) */
void bcm4360_dma_txblock(struct bcm4360_dma *di)
{
	di->pub.txavail = 0;
}

/* docs/re/spec/bmac-data.md, P31 dma_txunblock (slot 0x088) */
void bcm4360_dma_txunblock(struct bcm4360_dma *di)
{
	dma_txavail_update(di);
}

/* docs/re/spec/bmac-data.md, P32 dma_txactive (slot 0x090) */
u32 bcm4360_dma_txactive(struct bcm4360_dma *di)
{
	return ring_active(di->txin, di->txout, di->ntxd);
}

/* docs/re/spec/bmac-data.md, P35 dma64_txrotate (slot 0x098) */
void bcm4360_dma_txrotate(struct bcm4360_dma *di)
{
	volatile struct dma64_desc *from, *to;
	u16 nactive, ad, rot, old, new, stop;
	u32 ctrl1;

	/* step 1 */
	nactive = ring_active(di->txin, di->txout, di->ntxd);
	ad = (u16)((((hw_read32(di->txregs, DMA64_STATUS1) & di->xs1_ad_mask) -
		     di->xmtptrbase) & di->xs1_ad_mask) >> DESC_SHIFT);
	rot = (ad - di->txin) & (di->ntxd - 1);
	if (rot >= di->ntxd - nactive)
		return;

	/* step 2: from the last pending descriptor down to txin */
	stop = ring_prev(di->txin, di->ntxd);
	for (old = ring_prev(di->txout, di->ntxd); old != stop; old = ring_prev(old, di->ntxd)) {
		new = (old + rot) & (di->ntxd - 1);
		from = &di->txd[old];
		to = &di->txd[new];

		ctrl1 = from->ctrl1 & ~D64_CTRL1_EOT;
		if (new == di->ntxd - 1)
			ctrl1 |= D64_CTRL1_EOT;
		to->ctrl1 = ctrl1;
		to->ctrl2 = from->ctrl2;
		to->addrlow = from->addrlow;
		to->addrhigh = from->addrhigh;

		from->addrlow = DESC_RECLAIMED;
		from->addrhigh = DESC_RECLAIMED;

		di->txp[new] = di->txp[old];
		di->txp[old] = NULL;
	}

	/* step 3 */
	di->txin = ad;
	di->txout = (di->txout + rot) & (di->ntxd - 1);
	dma_txavail_update(di);
	hw_write32(di->txregs, DMA64_PTR, ((u32)di->txout << DESC_SHIFT) + di->xmtptrbase);
}

/* docs/re/spec/bmac-data.md, P8 dma_rxinit (slot 0x0a0) */
void bcm4360_dma_rxinit(struct bcm4360_dma *di)
{
	if (!di->nrxd)
		return;

	di->rs0cd = 0;
	di->rxout = 0;
	di->rxin = 0;
	osl_memset((void *)di->rxd, 0, di->nrxd * sizeof(struct dma64_desc));

	if (!di->aligndesc_4k) {
		dma_ddtable_init(di, DMA_RX, di->rxdpa);
		bcm4360_dma_rxenable(di);
	} else {
		bcm4360_dma_rxenable(di);
		dma_ddtable_init(di, DMA_RX, di->rxdpa);
	}
}

/* docs/re/spec/bmac-data.md, P9 dma64_rxreset (slot 0x0a8) */
bool bcm4360_dma_rxreset(struct bcm4360_dma *di)
{
	u32 v;

	if (!di->nrxd)
		return true;

	hw_write32(di->rxregs, DMA64_CONTROL, 0);
	v = dma_poll_state(di->rxregs, BIT(STATE_DISABLED));
	return dma_state(v) == STATE_DISABLED;
}

/* docs/re/spec/bmac-data.md, P18 dma64_rxidle (slot 0x0b0) */
bool bcm4360_dma_rxidle(struct bcm4360_dma *di)
{
	u32 rs0, rp;

	if (!di->nrxd)
		return true;

	rs0 = hw_read32(di->rxregs, DMA64_STATUS0);
	rp = hw_read32(di->rxregs, DMA64_PTR);
	return !((rs0 ^ rp) & di->rs0_cd_mask);
}

/* docs/re/spec/bmac-data.md, P19 dma64_rxstopped (slot 0x0b8) */
bool bcm4360_dma_rxstopped(struct bcm4360_dma *di)
{
	return dma_state(hw_read32(di->rxregs, DMA64_STATUS0)) == STATE_STOPPED;
}

/* docs/re/spec/bmac-data.md, P10 dma_rxenable (slot 0x0c0) */
void bcm4360_dma_rxenable(struct bcm4360_dma *di)
{
	u32 v;

	v = (hw_read32(di->rxregs, DMA64_CONTROL) & CTRL_ADDREXT_MASK) | CTRL_ENABLE;
	if (!(di->pub.dmactrlflags & DMA_CTRL_PARITY))
		v |= CTRL_PARITY_DIS;
	if (di->pub.dmactrlflags & DMA_CTRL_RX_OVF_CONT)
		v |= RC_OVERFLOW_CONT;
	v |= (u32)di->rxburstlen << CTRL_BURSTLEN_SHIFT;
	v = (v & ~CTRL_PREFETCHCTL_MASK) | (u32)di->rxprefetchctl << CTRL_PREFETCHCTL_SHIFT;
	v &= ~CTRL_PREFETCHTHR_MASK;
	hw_write32(di->rxregs, DMA64_CONTROL,
		   v | (u32)di->rxprefetchthresh << CTRL_PREFETCHTHR_SHIFT |
		   di->rxoffset << RC_OFFSET_SHIFT);
}

/* docs/re/spec/bmac-data.md, P20 dma64_rxenabled (slot 0x0c8) */
bool bcm4360_dma_rxenabled(struct bcm4360_dma *di)
{
	u32 v = hw_read32(di->rxregs, DMA64_CONTROL);

	return v != CTRL_CARD_GONE && (v & CTRL_ENABLE);
}

/* docs/re/spec/bmac-data.md, P37 dma_rx (slot 0x0d0) */
void *bcm4360_dma_rx(struct bcm4360_dma *di)
{
	void *head, *tail, *p;
	u32 len, bufsize = di->rxbufsize;
	s32 resid;
	u8 *data;

	for (;;) {
		/* step 1 */
		head = bcm4360_dma_getnextrxp(di, false);
		if (!head)
			return NULL;

		/* steps 2 to 4 */
		data = osl_pktdata(di->osh, head);
		len = data[0] | (u32)data[1] << 8;
		osl_pktsetlen(di->osh, head, dma_min(di->rxoffset + len, bufsize));
		resid = (s32)(len + di->rxoffset - bufsize);
		if (resid <= 0)
			return head;

		/* step 5 */
		tail = head;
		while (resid > 0) {
			p = bcm4360_dma_getnextrxp(di, false);
			if (!p)
				break;
			osl_pktsetnext(tail, p);
			osl_pktsetlen(di->osh, p, dma_min((u32)resid, bufsize));
			resid -= (s32)bufsize;
			tail = p;
		}

		/* step 6 */
		if (di->pub.dmactrlflags & DMA_CTRL_RX_CHAIN)
			return head;
		osl_pktfree(di->osh, head, false);
		di->pub.rxgiants++;
	}
}

/* docs/re/spec/bmac-data.md, P36 dma_rxfill (slot 0x0d8) */
bool bcm4360_dma_rxfill(struct bcm4360_dma *di)
{
	u32 n, i, align, extra, shift, pa, flags;
	u16 out = di->rxout;
	bool ret = false;
	void *p;

	/* step 1 */
	n = di->nrxpost - ring_active(di->rxin, di->rxout, di->nrxd);
	align = (di->pub.dmactrlflags & DMA_CTRL_RX_ALIGN16) ? RX_ALIGN_16 : 1;
	extra = di->rxbufsize > RXBUFSIZE_EXTRA_MIN ? di->rxextrahdrroom : 0;

	/* step 2 */
	for (i = 0; i < n; i++) {
		p = osl_pktget(di->osh, di->rxbufsize + align - 1 + extra);
		if (!p) {
			ret = i == 0 && bcm4360_dma_rxidle(di);
			di->pub.rxnobuf++;
			break;
		}

		shift = 0;
		if (di->pub.dmactrlflags & DMA_CTRL_RX_ALIGN16)
			shift = (align - (u32)(unsigned long)osl_pktdata(di->osh, p)) &
				(align - 1);
		if (shift + extra)
			osl_pktpull(di->osh, p, (int)(shift + extra));

		/* the length field of the receive status reads 0 until written */
		*(u32 *)osl_pktdata(di->osh, p) = 0;

		pa = osl_dma_map(di->osh, osl_pktdata(di->osh, p), di->rxbufsize, DMA_RX, p,
				 NULL);
		di->rxp[out] = p;

		flags = out == di->nrxd - 1 ? D64_CTRL1_EOT : 0;
		dma64_dd_upd(di, di->rxd, pa, out, flags, di->rxbufsize);
		out = ring_next(out, di->nrxd);
	}

	/* step 3: the pointer register is written in every case */
	di->rxout = out;
	hw_write32(di->rxregs, DMA64_PTR, ((u32)di->rxout << DESC_SHIFT) + di->rcvptrbase);
	return ret;
}

/* docs/re/spec/bmac-data.md, P39 dma_rxreclaim (slot 0x0e0) */
void bcm4360_dma_rxreclaim(struct bcm4360_dma *di)
{
	void *p;

	while ((p = bcm4360_dma_getnextrxp(di, true)))
		osl_pktfree(di->osh, p, false);
}

/* docs/re/spec/bmac-data.md, P38 dma_getnextrxp (slot 0x0e8) */
void *bcm4360_dma_getnextrxp(struct bcm4360_dma *di, bool forceall)
{
	volatile struct dma64_desc *d;
	u16 i;
	void *p;

	/* step 1 */
	if (!di->nrxd)
		return NULL;
	i = di->rxin;
	if (i == di->rxout)
		return NULL;

	/* step 2 */
	if (i == di->rs0cd) {
		di->rs0cd = dma_index(hw_read32(di->rxregs, DMA64_STATUS0), di->rs0_cd_mask,
				      di->rcvptrbase);
		if (i == di->rs0cd && !forceall)
			return NULL;
	}

	/* step 3 */
	p = di->rxp[i];
	di->rxp[i] = NULL;
	d = &di->rxd[i];
	osl_dma_unmap(di->osh, d->addrlow - di->dataoffsetlow, di->rxbufsize, DMA_RX);
	d->addrlow = DESC_RECLAIMED;
	d->addrhigh = DESC_RECLAIMED;

	/* step 4 */
	di->rxin = ring_next(i, di->nrxd);
	return p;
}

/* docs/re/spec/bmac-data.md, P40 dma_peeknextrxp (slot 0x0f0) */
void *bcm4360_dma_peeknextrxp(struct bcm4360_dma *di)
{
	u16 end, i;

	if (!di->nrxd)
		return NULL;

	end = dma_index(hw_read32(di->rxregs, DMA64_STATUS0), di->rs0_cd_mask, di->rcvptrbase);
	di->rs0cd = end;

	for (i = di->rxin; i != end; i = ring_next(i, di->nrxd))
		if (di->rxp[i])
			return di->rxp[i];
	return NULL;
}

/* docs/re/spec/bmac-data.md, P43 dma_rxparam_get (slot 0x0f8) */
void bcm4360_dma_rxparam_get(struct bcm4360_dma *di, u16 *rxoffset, u16 *rxbufsize)
{
	*rxoffset = (u16)di->rxoffset;
	*rxbufsize = di->rxbufsize;
}

/* docs/re/spec/bmac-data.md, P21 dma64_fifoloopbackenable (slot 0x100) */
void bcm4360_dma_fifoloopbackenable(struct bcm4360_dma *di)
{
	hw_write32(di->txregs, DMA64_CONTROL,
		   hw_read32(di->txregs, DMA64_CONTROL) | XC_LOOPBACK);
}

/* docs/re/spec/bmac-data.md, P44 dma_getvar (slot 0x108) */
void *bcm4360_dma_getvar(struct bcm4360_dma *di, const char *name)
{
	if (!osl_strcmp(name, "&txavail"))
		return &di->pub.txavail;
	return NULL;
}

/* docs/re/spec/bmac-data.md, P45 dma_counterreset (slot 0x110) */
void bcm4360_dma_counterreset(struct bcm4360_dma *di)
{
	di->pub.rxgiants = 0;
	di->pub.rxnobuf = 0;
	di->pub.txnobuf = 0;
}

/* docs/re/spec/bmac-data.md, P46 dma_ctrlflags (slot 0x118) */
u32 bcm4360_dma_ctrlflags(struct bcm4360_dma *di, u32 mask, u32 flags)
{
	u32 new, ctrl, v;

	if (!di)
		return 0;

	new = (di->pub.dmactrlflags & ~mask) | flags;
	if (new & DMA_CTRL_PARITY) {
		/* is the parity-disable bit writable? */
		ctrl = hw_read32(di->txregs, DMA64_CONTROL);
		hw_write32(di->txregs, DMA64_CONTROL, ctrl | CTRL_PARITY_DIS);
		v = hw_read32(di->txregs, DMA64_CONTROL);
		if (!(v & CTRL_PARITY_DIS))
			new &= ~DMA_CTRL_PARITY;
		else
			hw_write32(di->txregs, DMA64_CONTROL, ctrl);
	}
	di->pub.dmactrlflags = new;
	return new;
}

/* docs/re/spec/bmac-data.md, P41 dma_rxactive (slot 0x138) */
u32 bcm4360_dma_rxactive(struct bcm4360_dma *di)
{
	return ring_active(di->rxin, di->rxout, di->nrxd);
}

/* docs/re/spec/bmac-data.md, P33 dma_txpending (slot 0x140) */
u32 bcm4360_dma_txpending(struct bcm4360_dma *di)
{
	u16 cd;

	cd = dma_index(hw_read32(di->txregs, DMA64_STATUS0), di->xs0_cd_mask, di->xmtptrbase);
	di->xs0cd = cd;
	return ring_active(cd, di->txout, di->ntxd);
}

/* docs/re/spec/bmac-data.md, P34 dma_txcommitted (slot 0x148): base not subtracted */
u32 bcm4360_dma_txcommitted(struct bcm4360_dma *di)
{
	u32 ptr;

	if (di->txin == di->txout)
		return 0;
	ptr = hw_read32(di->txregs, DMA64_PTR);
	return (((ptr >> DESC_SHIFT) & INDEX_MASK16) - di->txin) & (di->ntxd - 1);
}

/* docs/re/spec/bmac-data.md, P47 dma_pktpool_set (slot 0x150) */
s32 bcm4360_dma_pktpool_set(struct bcm4360_dma *di, void *pool)
{
	di->pktpool = pool;
	return 0;
}

/* docs/re/spec/bmac-data.md, P48 dma_rxtxerror (slot 0x158) */
bool bcm4360_dma_rxtxerror(struct bcm4360_dma *di, bool istx)
{
	void *regs = istx ? di->txregs : di->rxregs;

	if (!di->dma64)
		return false;
	return (hw_read32(regs, DMA64_STATUS1) >> STATUS1_ERR_SHIFT) != 0;
}

/* docs/re/spec/bmac-data.md, P49 dma_burstlen_set (slot 0x160) */
void bcm4360_dma_burstlen_set(struct bcm4360_dma *di, u8 rxburstlen, u8 txburstlen)
{
	di->rxburstlen = rxburstlen;
	di->txburstlen = txburstlen;
}

/* docs/re/spec/bmac-data.md, P50 dma_avoidancecnt (slot 0x168) */
u32 bcm4360_dma_avoidancecnt(struct bcm4360_dma *di)
{
	return di->dma_avoidance_cnt;
}

/* docs/re/spec/bmac-data.md, P51 dma_param_set (slot 0x170) */
void bcm4360_dma_param_set(struct bcm4360_dma *di, u16 id, u16 value)
{
	u8 v = (u8)value;

	switch (id) {
	case PARAM_TXMULTIOUTSTDRD:
		di->txmultioutstdrd = v;
		break;
	case PARAM_TXPREFETCHCTL:
		di->txprefetchctl = v;
		break;
	case PARAM_TXPREFETCHTHRESH:
		di->txprefetchthresh = v;
		break;
	case PARAM_TXBURSTLEN:
		di->txburstlen = v;
		break;
	case PARAM_RXPREFETCHCTL:
		di->rxprefetchctl = v;
		break;
	case PARAM_RXPREFETCHTHRESH:
		di->rxprefetchthresh = v;
		break;
	case PARAM_RXBURSTLEN:
		di->rxburstlen = v;
		break;
	default:
		break;
	}
}

/* docs/re/spec/bmac-data.md, P22 dma_glom_enable (slot 0x178) */
bool bcm4360_dma_glom_enable(struct bcm4360_dma *di, u32 enable)
{
	u32 v = hw_read32(di->rxregs, DMA64_CONTROL);

	if (!enable) {
		hw_write32(di->rxregs, DMA64_CONTROL, v & ~RC_GLOM);
		return true;
	}
	hw_write32(di->rxregs, DMA64_CONTROL, v | RC_GLOM);
	return (hw_read32(di->rxregs, DMA64_CONTROL) & RC_GLOM) != 0;
}

/* docs/re/spec/bmac-data.md, P42 dma64_activerxbuf (slot 0x180) */
u32 bcm4360_dma_activerxbuf(struct bcm4360_dma *di)
{
	u16 cd, ptr;

	cd = dma_index(hw_read32(di->rxregs, DMA64_STATUS0), di->rs0_cd_mask, di->rcvptrbase);
	ptr = dma_index(hw_read32(di->rxregs, DMA64_PTR), di->rs0_cd_mask, di->rcvptrbase);
	return ring_active(cd, ptr, di->nrxd);
}

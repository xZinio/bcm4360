/*
 * The DMA engine of one FIFO of the 802.11 core (64-bit descriptors): rings of
 * descriptors in coherent memory, transmit and receive channels, packet
 * posting, reclaim and the channel state machine.
 *
 * Specification: docs/re/spec/bmac-data.md, section 1 (Broadcom's hnddma.c):
 * the functions are dma_attach, dma_addrwidth and those of the function table
 * dma64proc (D3), with the prefix bcm4360_dma_ and the names the
 * specification assigns (dma64_txinit -> bcm4360_dma_txinit, dma_rxfill ->
 * bcm4360_dma_rxfill, ...), the same arguments and results.
 *
 * An engine starts with struct bcm4360_dma_pub: Broadcom's other code reads
 * these fields of it directly (bmac-data.md D8), so they keep their offsets.
 * The rest of the engine is the implementation's. `fn` is the platform's
 * (the table through which Broadcom's code calls the engine): attach leaves
 * it NULL, the platform fills it in.
 *
 * Packets, DMA memory and mapping: the osl_pkt* / osl_dma_* functions of
 * hw.h. Channel registers: hw_read32/hw_write32 relative to `txregs` /
 * `rxregs` (virtual addresses of the channel's register block).
 */
#ifndef BCM4360_DMA_H
#define BCM4360_DMA_H

#include <bcm4360/types.h>

struct bcm4360_dma_pub {
	const void *fn;		/* 0x00: the platform's function table */
	u32 txavail;		/* 0x08: free transmit descriptors */
	u32 dmactrlflags;	/* 0x0c: control flags (D4) */
	u32 rxgiants;		/* 0x10 */
	u32 rxnobuf;		/* 0x14 */
	u32 txnobuf;		/* 0x18 */
	u32 txnodesc;		/* 0x1c */
};

/* an engine: begins with struct bcm4360_dma_pub (a cast to it is valid) */
struct bcm4360_dma;

/* what attach needs to know of the backplane (Broadcom's si_t) */
struct bcm4360_dma_bus {
	void *ctx;		/* the platform's handle, passed to the services */
	u32 bustype;		/* 1 = PCI */
	u32 buscoreid;		/* 0x83c: PCIe Gen2 */
	u32 chip;		/* chip id */
};

/* services of the platform (the backplane functions bmac-data.md names) */
u32 bcm4360_dma_env_core_sflags(void *ctx, u32 mask, u32 val);	/* si_core_sflags */
u32 bcm4360_dma_env_coreid(void *ctx);				/* si_coreid */
u32 bcm4360_dma_env_corerev(void *ctx);				/* si_corerev */
bool bcm4360_dma_env_backplane64(void *ctx);			/* si_backplane64 */

/* P1, P2 */
struct bcm4360_dma *bcm4360_dma_attach(void *osh, const char *name,
				       const struct bcm4360_dma_bus *bus, void *txregs,
				       void *rxregs, u32 ntxd, u32 nrxd, u32 rxbufsize,
				       s32 rxextheadroom, u32 nrxpost, u32 rxoffset,
				       u32 *msg_level);
u32 bcm4360_dma_addrwidth(const struct bcm4360_dma_bus *bus, void *dmaregs);

/* the slots of the function table (D3), in slot order */
void bcm4360_dma_detach(struct bcm4360_dma *di);				/* 0x000 */
void bcm4360_dma_txinit(struct bcm4360_dma *di);				/* 0x008 */
bool bcm4360_dma_txreset(struct bcm4360_dma *di);				/* 0x010 */
bool bcm4360_dma_txenabled(struct bcm4360_dma *di);				/* 0x018 */
void bcm4360_dma_txsuspend(struct bcm4360_dma *di);				/* 0x020 */
void bcm4360_dma_txresume(struct bcm4360_dma *di);				/* 0x028 */
bool bcm4360_dma_txsuspended(struct bcm4360_dma *di);			/* 0x030 */
bool bcm4360_dma_txsuspendedidle(struct bcm4360_dma *di);			/* 0x038 */
s32 bcm4360_dma_txfast(struct bcm4360_dma *di, void *pkt, bool commit);	/* 0x040 */
s32 bcm4360_dma_txunframed(struct bcm4360_dma *di, void *buf, u32 len,
			   bool commit);					/* 0x048 */
void *bcm4360_dma_getpos(struct bcm4360_dma *di, bool direction_tx);	/* 0x050 */
bool bcm4360_dma_txstopped(struct bcm4360_dma *di);				/* 0x058 */
void bcm4360_dma_txreclaim(struct bcm4360_dma *di, s32 range);		/* 0x060 */
void *bcm4360_dma_getnexttxp(struct bcm4360_dma *di, s32 range);		/* 0x068 */
void *bcm4360_dma_peeknexttxp(struct bcm4360_dma *di);			/* 0x070 */
s32 bcm4360_dma_peekntxp(struct bcm4360_dma *di, s32 *n, void **pkts,
			 s32 range);						/* 0x078 */
void bcm4360_dma_txblock(struct bcm4360_dma *di);				/* 0x080 */
void bcm4360_dma_txunblock(struct bcm4360_dma *di);				/* 0x088 */
u32 bcm4360_dma_txactive(struct bcm4360_dma *di);				/* 0x090 */
void bcm4360_dma_txrotate(struct bcm4360_dma *di);				/* 0x098 */
void bcm4360_dma_rxinit(struct bcm4360_dma *di);				/* 0x0a0 */
bool bcm4360_dma_rxreset(struct bcm4360_dma *di);				/* 0x0a8 */
bool bcm4360_dma_rxidle(struct bcm4360_dma *di);				/* 0x0b0 */
bool bcm4360_dma_rxstopped(struct bcm4360_dma *di);				/* 0x0b8 */
void bcm4360_dma_rxenable(struct bcm4360_dma *di);				/* 0x0c0 */
bool bcm4360_dma_rxenabled(struct bcm4360_dma *di);				/* 0x0c8 */
void *bcm4360_dma_rx(struct bcm4360_dma *di);				/* 0x0d0 */
bool bcm4360_dma_rxfill(struct bcm4360_dma *di);				/* 0x0d8 */
void bcm4360_dma_rxreclaim(struct bcm4360_dma *di);				/* 0x0e0 */
void *bcm4360_dma_getnextrxp(struct bcm4360_dma *di, bool forceall);	/* 0x0e8 */
void *bcm4360_dma_peeknextrxp(struct bcm4360_dma *di);			/* 0x0f0 */
void bcm4360_dma_rxparam_get(struct bcm4360_dma *di, u16 *rxoffset,
			     u16 *rxbufsize);					/* 0x0f8 */
void bcm4360_dma_fifoloopbackenable(struct bcm4360_dma *di);		/* 0x100 */
void *bcm4360_dma_getvar(struct bcm4360_dma *di, const char *name);	/* 0x108 */
void bcm4360_dma_counterreset(struct bcm4360_dma *di);			/* 0x110 */
u32 bcm4360_dma_ctrlflags(struct bcm4360_dma *di, u32 mask, u32 flags);	/* 0x118 */
u32 bcm4360_dma_rxactive(struct bcm4360_dma *di);				/* 0x138 */
u32 bcm4360_dma_txpending(struct bcm4360_dma *di);				/* 0x140 */
u32 bcm4360_dma_txcommitted(struct bcm4360_dma *di);			/* 0x148 */
s32 bcm4360_dma_pktpool_set(struct bcm4360_dma *di, void *pool);		/* 0x150 */
bool bcm4360_dma_rxtxerror(struct bcm4360_dma *di, bool istx);		/* 0x158 */
void bcm4360_dma_burstlen_set(struct bcm4360_dma *di, u8 rxburstlen, u8 txburstlen);
										/* 0x160 */
u32 bcm4360_dma_avoidancecnt(struct bcm4360_dma *di);			/* 0x168 */
void bcm4360_dma_param_set(struct bcm4360_dma *di, u16 id, u16 value);	/* 0x170 */
bool bcm4360_dma_glom_enable(struct bcm4360_dma *di, u32 enable);		/* 0x178 */
u32 bcm4360_dma_activerxbuf(struct bcm4360_dma *di);			/* 0x180 */

/*
 * Not in the table: who frees a transmit packet the engine gives up on (the
 * reclaim, a post that failed). Unset, the OS layer does, as in Broadcom's
 * engine (osl_pktfree with send); bcm4360_open sets it so that every frame of
 * mac80211's gets back to mac80211.
 */
void bcm4360_dma_txfree_set(struct bcm4360_dma *di, void (*fn)(void *ctx, void *pkt),
			    void *ctx);

#endif

/*
 * Hybrid build: Broadcom's DMA engine (hnddma.c) by the open code of
 * open/mac/dma.c (docs/re/spec/bmac-data.md section 1; task
 * docs/re/tasks/dma.md). Switch open_dma.
 *
 * The engine is reached through dma_attach and, after it, only through the
 * function table whose address is the engine's first field (bmac-data.md D3,
 * D8). With open_dma on, dma_attach makes an open engine and puts this
 * file's table into its first field: every later call of Broadcom's code
 * (wlc_bmac.c, wlc.c) arrives in the open code. The table has Broadcom's
 * layout (slot offsets, the count after it); its entries are small wrappers
 * that take the object's narrow arguments (bool, u8, u16) as u32 and
 * truncate them. With open_dma off Broadcom's own dma_attach runs and its
 * engines keep Broadcom's table.
 */
#include "hybrid.h"
#include <bcm4360/dma.h>

#ifdef __KERNEL__
#include <linux/module.h>
#endif

bool bcm4360_open_dma = true;

#ifdef __KERNEL__
module_param_named(open_dma, bcm4360_open_dma, bool, 0444);
MODULE_PARM_DESC(open_dma, "DMA engines: open code (1, default) or Broadcom's (0)");
#endif

HYB_TAKE(void *, dma_attach, (void *osh, const char *name, void *sih, void *txregs,
			      void *rxregs, u32 ntxd, u32 nrxd, u32 rxbufsize, s32 rxextheadroom,
			      u32 nrxpost, u32 rxoffset, u32 *msg_level));
HYB_TAKE(u32, dma_addrwidth, (void *sih, void *dmaregs));

/* Broadcom's backplane functions the services call (the backplane layer's takes) */
u32 si_core_sflags(void *sih, u32 mask, u32 val);
u32 si_coreid(void *sih);
u32 si_corerev(void *sih);
u8 si_backplane64(void *sih);

typedef struct bcm4360_dma D;

/* --- the table (bmac-data.md D3): wrappers in slot order --- */
static void w_detach(D *di) { bcm4360_dma_detach(di); }
static void w_txinit(D *di) { bcm4360_dma_txinit(di); }
static u32 w_txreset(D *di) { return bcm4360_dma_txreset(di); }
static u32 w_txenabled(D *di) { return bcm4360_dma_txenabled(di); }
static void w_txsuspend(D *di) { bcm4360_dma_txsuspend(di); }
static void w_txresume(D *di) { bcm4360_dma_txresume(di); }
static u32 w_txsuspended(D *di) { return bcm4360_dma_txsuspended(di); }
static u32 w_txsuspendedidle(D *di) { return bcm4360_dma_txsuspendedidle(di); }
static s32 w_txfast(D *di, void *pkt, u32 commit)
{
	return bcm4360_dma_txfast(di, pkt, (u8)commit != 0);
}
static s32 w_txunframed(D *di, void *buf, u32 len, u32 commit)
{
	return bcm4360_dma_txunframed(di, buf, len, (u8)commit != 0);
}
static void *w_getpos(D *di, u32 direction_tx)
{
	return bcm4360_dma_getpos(di, (u8)direction_tx != 0);
}
static u32 w_txstopped(D *di) { return bcm4360_dma_txstopped(di); }
static void w_txreclaim(D *di, s32 range) { bcm4360_dma_txreclaim(di, range); }
static void *w_getnexttxp(D *di, s32 range) { return bcm4360_dma_getnexttxp(di, range); }
static void *w_peeknexttxp(D *di) { return bcm4360_dma_peeknexttxp(di); }
static s32 w_peekntxp(D *di, s32 *n, void **pkts, s32 range)
{
	return bcm4360_dma_peekntxp(di, n, pkts, range);
}
static void w_txblock(D *di) { bcm4360_dma_txblock(di); }
static void w_txunblock(D *di) { bcm4360_dma_txunblock(di); }
static u32 w_txactive(D *di) { return bcm4360_dma_txactive(di); }
static void w_txrotate(D *di) { bcm4360_dma_txrotate(di); }
static void w_rxinit(D *di) { bcm4360_dma_rxinit(di); }
static u32 w_rxreset(D *di) { return bcm4360_dma_rxreset(di); }
static u32 w_rxidle(D *di) { return bcm4360_dma_rxidle(di); }
static u32 w_rxstopped(D *di) { return bcm4360_dma_rxstopped(di); }
static void w_rxenable(D *di) { bcm4360_dma_rxenable(di); }
static u32 w_rxenabled(D *di) { return bcm4360_dma_rxenabled(di); }
static void *w_rx(D *di) { return bcm4360_dma_rx(di); }
static u32 w_rxfill(D *di) { return bcm4360_dma_rxfill(di); }
static void w_rxreclaim(D *di) { bcm4360_dma_rxreclaim(di); }
static void *w_getnextrxp(D *di, u32 forceall)
{
	return bcm4360_dma_getnextrxp(di, (u8)forceall != 0);
}
static void *w_peeknextrxp(D *di) { return bcm4360_dma_peeknextrxp(di); }
static void w_rxparam_get(D *di, u16 *rxoffset, u16 *rxbufsize)
{
	bcm4360_dma_rxparam_get(di, rxoffset, rxbufsize);
}
static void w_fifoloopbackenable(D *di) { bcm4360_dma_fifoloopbackenable(di); }
static void *w_getvar(D *di, const char *name) { return bcm4360_dma_getvar(di, name); }
static void w_counterreset(D *di) { bcm4360_dma_counterreset(di); }
static u32 w_ctrlflags(D *di, u32 mask, u32 flags) { return bcm4360_dma_ctrlflags(di, mask, flags); }
static u32 w_rxactive(D *di) { return bcm4360_dma_rxactive(di); }
static u32 w_txpending(D *di) { return bcm4360_dma_txpending(di); }
static u32 w_txcommitted(D *di) { return bcm4360_dma_txcommitted(di); }
static s32 w_pktpool_set(D *di, void *pool) { return bcm4360_dma_pktpool_set(di, pool); }
static u32 w_rxtxerror(D *di, u32 istx) { return bcm4360_dma_rxtxerror(di, (u8)istx != 0); }
static void w_burstlen_set(D *di, u32 rxburstlen, u32 txburstlen)
{
	bcm4360_dma_burstlen_set(di, (u8)rxburstlen, (u8)txburstlen);
}
static u32 w_avoidancecnt(D *di) { return bcm4360_dma_avoidancecnt(di); }
static void w_param_set(D *di, u32 id, u32 value)
{
	bcm4360_dma_param_set(di, (u16)id, (u16)value);
}
static u32 w_glom_enable(D *di, u32 enable) { return bcm4360_dma_glom_enable(di, enable); }
static u32 w_activerxbuf(D *di) { return bcm4360_dma_activerxbuf(di); }

#define SLOT(fn)	((const void *)(fn))

/* 49 slots of 8 bytes and the count (40) after them: 0x190 bytes */
static const void *const hyb_dma_fn[50] = {
	SLOT(w_detach),			/* 0x000 */
	SLOT(w_txinit),			/* 0x008 */
	SLOT(w_txreset),		/* 0x010 */
	SLOT(w_txenabled),		/* 0x018 */
	SLOT(w_txsuspend),		/* 0x020 */
	SLOT(w_txresume),		/* 0x028 */
	SLOT(w_txsuspended),		/* 0x030 */
	SLOT(w_txsuspendedidle),	/* 0x038 */
	SLOT(w_txfast),			/* 0x040 */
	SLOT(w_txunframed),		/* 0x048 */
	SLOT(w_getpos),			/* 0x050 */
	SLOT(w_txstopped),		/* 0x058 */
	SLOT(w_txreclaim),		/* 0x060 */
	SLOT(w_getnexttxp),		/* 0x068 */
	SLOT(w_peeknexttxp),		/* 0x070 */
	SLOT(w_peekntxp),		/* 0x078 */
	SLOT(w_txblock),		/* 0x080 */
	SLOT(w_txunblock),		/* 0x088 */
	SLOT(w_txactive),		/* 0x090 */
	SLOT(w_txrotate),		/* 0x098 */
	SLOT(w_rxinit),			/* 0x0a0 */
	SLOT(w_rxreset),		/* 0x0a8 */
	SLOT(w_rxidle),			/* 0x0b0 */
	SLOT(w_rxstopped),		/* 0x0b8 */
	SLOT(w_rxenable),		/* 0x0c0 */
	SLOT(w_rxenabled),		/* 0x0c8 */
	SLOT(w_rx),			/* 0x0d0 */
	SLOT(w_rxfill),			/* 0x0d8 */
	SLOT(w_rxreclaim),		/* 0x0e0 */
	SLOT(w_getnextrxp),		/* 0x0e8 */
	SLOT(w_peeknextrxp),		/* 0x0f0 */
	SLOT(w_rxparam_get),		/* 0x0f8 */
	SLOT(w_fifoloopbackenable),	/* 0x100 */
	SLOT(w_getvar),			/* 0x108 */
	SLOT(w_counterreset),		/* 0x110 */
	SLOT(w_ctrlflags),		/* 0x118 */
	NULL,				/* 0x120: dump, absent */
	NULL,				/* 0x128: dumptx, absent */
	NULL,				/* 0x130: dumprx, absent */
	SLOT(w_rxactive),		/* 0x138 */
	SLOT(w_txpending),		/* 0x140 */
	SLOT(w_txcommitted),		/* 0x148 */
	SLOT(w_pktpool_set),		/* 0x150 */
	SLOT(w_rxtxerror),		/* 0x158 */
	SLOT(w_burstlen_set),		/* 0x160 */
	SLOT(w_avoidancecnt),		/* 0x168 */
	SLOT(w_param_set),		/* 0x170 */
	SLOT(w_glom_enable),		/* 0x178 */
	SLOT(w_activerxbuf),		/* 0x180 */
	(const void *)40UL,		/* 0x188: the count */
};

/* the backplane as the engine sees it (struct si_pub, hybrid.h) */
static void dma_bus(void *sih, struct bcm4360_dma_bus *bus)
{
	const struct hyb_si_pub *pub = sih;

	bus->ctx = sih;
	bus->bustype = pub->bustype;
	bus->buscoreid = pub->buscoretype;
	bus->chip = pub->chip;
}

void *dma_attach(void *osh, const char *name, void *sih, void *txregs, void *rxregs, u32 ntxd,
		 u32 nrxd, u32 rxbufsize, s32 rxextheadroom, u32 nrxpost, u32 rxoffset,
		 u32 *msg_level)
{
	struct bcm4360_dma_bus bus;
	struct bcm4360_dma *di;

	if (!bcm4360_open_dma || !sih)
		return bcm4360_blob_dma_attach(osh, name, sih, txregs, rxregs, ntxd, nrxd,
					       rxbufsize, rxextheadroom, nrxpost, rxoffset,
					       msg_level);
	dma_bus(sih, &bus);
	di = bcm4360_dma_attach(osh, name, &bus, txregs, rxregs, ntxd, nrxd, rxbufsize,
				rxextheadroom, nrxpost, rxoffset, msg_level);
	if (di)
		((struct bcm4360_dma_pub *)di)->fn = hyb_dma_fn;
	return di;
}

u32 dma_addrwidth(void *sih, void *dmaregs)
{
	struct bcm4360_dma_bus bus;

	if (!bcm4360_open_dma || !sih)
		return bcm4360_blob_dma_addrwidth(sih, dmaregs);
	dma_bus(sih, &bus);
	return bcm4360_dma_addrwidth(&bus, dmaregs);
}

/* the services of open/include/bcm4360/dma.h */
u32 bcm4360_dma_env_core_sflags(void *ctx, u32 mask, u32 val)
{
	return si_core_sflags(ctx, mask, val);
}

u32 bcm4360_dma_env_coreid(void *ctx)
{
	return si_coreid(ctx);
}

u32 bcm4360_dma_env_corerev(void *ctx)
{
	return si_corerev(ctx);
}

bool bcm4360_dma_env_backplane64(void *ctx)
{
	return si_backplane64(ctx) != 0;
}

// SPDX-License-Identifier: ISC
/*
 * Hybrid build: functions of the MAC core that Broadcom's core runs
 * constantly (wlc_bmac.c: the energy detect at every channel change, the
 * transmit FIFO suspend/resume of the mute, the CCA counters, the rate offsets
 * in shared memory, the TSF read) by the open code of open/mac/bmac_runtime.c
 * (docs/re/spec/bmac-run.md R1, R2, R5, R6, R7; task
 * docs/re/tasks/bmac-runtime.md). Part of the MAC layer: switch open_mac.
 *
 * As for the other MAC functions the state goes in and out with
 * hyb_mac_in/hyb_mac_out (hybrid/mac.c); the services of
 * open/include/bcm4360/mac_env.h for these functions are implemented here.
 *
 * The rate set and its entries (R6) belong to the upper layer: the rate set
 * is a 32-bit count followed by the rate bytes; the entries are passed on as
 * bytes, in the layout the specification gives.
 *
 * Parameters of type bool of the object are bytes (u8); integer arguments
 * narrower than 32 bit arrive as u32 and are truncated.
 */
#include "hybrid.h"
#include <bcm4360/mac.h>
#include <bcm4360/mac_env.h>

#define WLC_HW_DI		0x020	/* DMA engines of the FIFOs 0..5 */
#define WLC_HW_BAND		0x0e8	/* current band state */
#define BAND_PI			0x028	/* the PHY of the band */
#define RATESET_COUNT		0x00	/* u32 */
#define RATESET_RATES		0x04	/* u8 [] */
/* the DMA engine: its first field points to the table of its functions */
#define DI_FN_TXSUSPEND		0x20
#define DI_FN_TXRESUME		0x28
#define DI_FN_TXSUSPENDED	0x30

#define FIELD(p, off, type)	(*(type *)((u8 *)(p) + (off)))

HYB_TAKE(void, wlc_bmac_ifsctl_edcrs_set, (void *wlc_hw, u32 on));
HYB_TAKE(void, wlc_bmac_tx_fifo_suspend, (void *wlc_hw, u32 fifo));
HYB_TAKE(void, wlc_bmac_tx_fifo_resume, (void *wlc_hw, u32 fifo));
HYB_TAKE(u8, wlc_bmac_tx_fifo_suspended, (void *wlc_hw, u32 fifo));
HYB_TAKE(void, cca_stats_read, (void *wlc_hw, u32 *out));
HYB_TAKE(s32, wlc_bmac_cca_stats_read, (void *wlc_hw, u32 *out));
HYB_TAKE(u32, wlc_bmac_rate_shm_offset, (void *wlc_hw, u32 rate));
HYB_TAKE(void, wlc_bmac_stf_get_rateset_shm_offset, (void *wlc_hw, void *rs, u32 off,
						     void *out));
HYB_TAKE(void, wlc_bmac_stf_set_rateset_shm_offset, (void *wlc_hw, void *rs, u32 off,
						     void *in));
HYB_TAKE(void, wlc_bmac_read_tsf, (void *wlc_hw, u32 *lo, u32 *hi));

/* the PHY function the service calls (a take of the PHY layer) */
void wlc_acphy_set_scramb_dyn_bw_en(void *pi, u8 on);

#define PROLOGUE(blob_call)						\
	struct bcm4360_hw hw;						\
	struct bcm4360_mac mac, mac0;						\
									\
	if (!hyb_mac_on(HYB_MAC_RUNTIME)) {					\
		blob_call;						\
	}								\
	hyb_mac_in(wlc_hw, &hw, &mac, &mac0)

void wlc_bmac_ifsctl_edcrs_set(void *wlc_hw, u32 on)
{
	PROLOGUE(bcm4360_blob_wlc_bmac_ifsctl_edcrs_set(wlc_hw, on); return);
	bcm4360_bmac_ifsctl_edcrs_set(&mac, (u8)on != 0);
	hyb_mac_out(wlc_hw, &mac, &mac0);
}

void wlc_bmac_tx_fifo_suspend(void *wlc_hw, u32 fifo)
{
	PROLOGUE(bcm4360_blob_wlc_bmac_tx_fifo_suspend(wlc_hw, fifo); return);
	bcm4360_bmac_tx_fifo_suspend(&mac, fifo);
	hyb_mac_out(wlc_hw, &mac, &mac0);
}

void wlc_bmac_tx_fifo_resume(void *wlc_hw, u32 fifo)
{
	PROLOGUE(bcm4360_blob_wlc_bmac_tx_fifo_resume(wlc_hw, fifo); return);
	bcm4360_bmac_tx_fifo_resume(&mac, fifo);
	hyb_mac_out(wlc_hw, &mac, &mac0);
}

u8 wlc_bmac_tx_fifo_suspended(void *wlc_hw, u32 fifo)
{
	bool r;

	PROLOGUE(return bcm4360_blob_wlc_bmac_tx_fifo_suspended(wlc_hw, fifo));
	r = bcm4360_bmac_tx_fifo_suspended(&mac, fifo);
	hyb_mac_out(wlc_hw, &mac, &mac0);
	return r ? 1 : 0;
}

void cca_stats_read(void *wlc_hw, u32 *out)
{
	PROLOGUE(bcm4360_blob_cca_stats_read(wlc_hw, out); return);
	bcm4360_bmac_cca_stats_read(&mac, out);
	hyb_mac_out(wlc_hw, &mac, &mac0);
}

s32 wlc_bmac_cca_stats_read(void *wlc_hw, u32 *out)
{
	s32 r;

	PROLOGUE(return bcm4360_blob_wlc_bmac_cca_stats_read(wlc_hw, out));
	r = bcm4360_bmac_cca_stats_get(&mac, out);
	hyb_mac_out(wlc_hw, &mac, &mac0);
	return r;
}

u32 wlc_bmac_rate_shm_offset(void *wlc_hw, u32 rate)
{
	u32 r;

	PROLOGUE(return bcm4360_blob_wlc_bmac_rate_shm_offset(wlc_hw, rate));
	r = bcm4360_bmac_rate_shm_offset(&mac, (u8)rate);
	hyb_mac_out(wlc_hw, &mac, &mac0);
	return r;
}

void wlc_bmac_stf_get_rateset_shm_offset(void *wlc_hw, void *rs, u32 off, void *out)
{
	PROLOGUE(bcm4360_blob_wlc_bmac_stf_get_rateset_shm_offset(wlc_hw, rs, off, out);
		 return);
	bcm4360_bmac_stf_get_rateset_shm_offset(&mac, (const u8 *)rs + RATESET_RATES,
						FIELD(rs, RATESET_COUNT, u32), (u16)off, out);
	hyb_mac_out(wlc_hw, &mac, &mac0);
}

void wlc_bmac_stf_set_rateset_shm_offset(void *wlc_hw, void *rs, u32 off, void *in)
{
	PROLOGUE(bcm4360_blob_wlc_bmac_stf_set_rateset_shm_offset(wlc_hw, rs, off, in);
		 return);
	bcm4360_bmac_stf_set_rateset_shm_offset(&mac, FIELD(rs, RATESET_COUNT, u32), (u16)off,
						in);
	hyb_mac_out(wlc_hw, &mac, &mac0);
}

void wlc_bmac_read_tsf(void *wlc_hw, u32 *lo, u32 *hi)
{
	PROLOGUE(bcm4360_blob_wlc_bmac_read_tsf(wlc_hw, lo, hi); return);
	bcm4360_bmac_read_tsf(&mac, lo, hi);
	hyb_mac_out(wlc_hw, &mac, &mac0);
}

/* --- the services of open/include/bcm4360/mac_env.h at run time --- */

static void *pi_of(struct bcm4360_mac *mac)
{
	void *band = FIELD(mac->env, WLC_HW_BAND, void *);

	return band ? FIELD(band, BAND_PI, void *) : NULL;
}

void bcm4360_bmac_env_phy_scramb_dyn_bw_en(struct bcm4360_mac *mac, bool on)
{
	hyb_mac_sync(mac);
	wlc_acphy_set_scramb_dyn_bw_en(pi_of(mac), on ? 1 : 0);
	hyb_mac_in(mac->env, mac->hw, mac, mac->env_shadow);
}

typedef void (*dma_fn)(void *di);
typedef u8 (*dma_bool_fn)(void *di);	/* the object's slot returns a bool in the low byte */

static void *di_of(struct bcm4360_mac *mac, u32 fifo)
{
	return fifo < 6 ? FIELD(mac->env, WLC_HW_DI + 8 * fifo, void *) : NULL;
}

void bcm4360_bmac_env_dma_txsuspend(struct bcm4360_mac *mac, u32 fifo)
{
	void *di = di_of(mac, fifo);

	if (di)
		FIELD(FIELD(di, 0, void *), DI_FN_TXSUSPEND, dma_fn)(di);
}

void bcm4360_bmac_env_dma_txresume(struct bcm4360_mac *mac, u32 fifo)
{
	void *di = di_of(mac, fifo);

	if (di)
		FIELD(FIELD(di, 0, void *), DI_FN_TXRESUME, dma_fn)(di);
}

bool bcm4360_bmac_env_dma_txsuspended(struct bcm4360_mac *mac, u32 fifo)
{
	void *di = di_of(mac, fifo);

	if (!di)
		return false;
	return FIELD(FIELD(di, 0, void *), DI_FN_TXSUSPENDED, dma_bool_fn)(di) != 0;
}

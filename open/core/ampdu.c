/*
 * A-MPDU transmit (docs/re/spec/ampdu.md): the sessions the platform starts
 * (3.1), what an aggregatable MPDU's transmit header gets (1.4), the epoch
 * bit (1.6), the sequence check (1.7). The aggregate transmit status is
 * decoded in tx.c (2).
 */
#include "core.h"

/* ampdu.md 1.4: cache bytes of the header */
#define TXH_AMPDU_MAX		0x66	/* max MPDUs at the primary rate entry */
#define TXH_AMPDU_MAX_FB	0x67	/* the same for the fallback entries */
#define TXH_AMPDU_DUR		0x68	/* bits 4..15 max duration us, bits 0..3 spacing */
#define TXH_AMPDU_WIN		0x6a	/* Block Ack window - 1 */
#define TXH_AMPDU_RES		0x6b	/* 0 */

#define TXC_AMPDU		0x0040	/* MacTxControlLow: aggregate */
#define TXC_HI_VHT		0x0004	/* MacTxControlHigh: "sent alone" (1.4) */
#define PFX_EPOCH		0x02	/* prefix byte 2 */
#define PFX_EPOCH_BYTE		2

#define AMPDU_MAX_DUR		0x1528	/* ampdu+0x2c (1.1) */
#define AMPDU_OWN_DENSITY	5	/* own minimum MPDU start spacing (1.1) */
#define AMPDU_MAX_WINDOW	64

static void put16(u8 *p, u32 off, u16 v)
{
	p[off] = v;
	p[off + 1] = v >> 8;
}

static struct core_ampdu_sess *sess_of(struct bcm4360_core *c, u32 peer, u32 tid)
{
	if (peer >= AMPDU_NPEERS || tid >= AMPDU_NTID)
		return NULL;
	return &c->fr.ampdu.sess[peer][tid];
}

/* ampdu.md 3.1: a session begins (sub_055023 / mac80211 TX_START) */
void bcm4360_core_ampdu_tx_start(struct bcm4360_core *c, u8 peer, u8 tid, u16 ssn,
				 u8 max_mpdus, u8 density)
{
	struct core_ampdu_sess *s = sess_of(c, peer, tid);
	struct core_ampdu_peer *pe;

	if (!s)
		return;
	pe = &c->fr.ampdu.peer[peer];
	pe->max_mpdus = max_mpdus ? max_mpdus : 32;
	pe->density = density > AMPDU_OWN_DENSITY ? density : AMPDU_OWN_DENSITY;
	s->started = true;
	s->on = false;
	s->window = AMPDU_MAX_WINDOW;
	s->next_seq = ssn & 0xfff;
}

/* ampdu.md 3.1: the response was accepted (wlc_ampdu_recv_addba_resp) */
void bcm4360_core_ampdu_tx_operational(struct bcm4360_core *c, u8 peer, u8 tid, u8 buf_size)
{
	struct core_ampdu_sess *s = sess_of(c, peer, tid);

	if (!s || !s->started)
		return;
	s->window = !buf_size || buf_size > AMPDU_MAX_WINDOW ? AMPDU_MAX_WINDOW : buf_size;
	s->on = true;
}

/* ampdu.md 3.2: ampdu_cleanup_tid_ini; no hardware access */
void bcm4360_core_ampdu_tx_stop(struct bcm4360_core *c, u8 peer, u8 tid)
{
	struct core_ampdu_sess *s = sess_of(c, peer, tid);

	if (s) {
		s->started = false;
		s->on = false;
	}
}

/* ampdu.md 1.4, 1.5 step 4: the frame is aggregatable (a session that is on, an HT or VHT first rate) */
bool core_ampdu_is_agg(struct bcm4360_core *c, const struct bcm4360_txparams *p)
{
	struct core_ampdu_sess *s;

	if (!p->ampdu)
		return false;
	s = sess_of(c, p->peer, p->tid);
	return s && s->on && p->rates[0].enc >= 1;
}

/* ampdu.md 1.6: kind of a frame by its first rate entry (0 not aggregated, 1 HT, 2 VHT) */
static u8 epoch_kind(const struct bcm4360_txparams *p, bool agg)
{
	return agg ? p->rates[0].enc : 0;
}

/* ampdu.md 1.6: flip the FIFO's epoch bit */
static void epoch_flip(struct core_ampdu_fifo *fs)
{
	fs->epoch ^= 1;
}

/* ampdu.md 1.7: the sequence check; true: a gap (the epoch must change) */
static bool seq_gap(struct core_ampdu_sess *s, const u8 *f)
{
	u16 seq = (f[22] | (u16)f[23] << 8) >> 4;
	u16 d = (seq - s->next_seq) & 0xfff;

	if (!d) {
		s->next_seq = (seq + 1) & 0xfff;
		return false;
	}
	/* the spec drops an old MPDU (d > 2048); a mac80211 driver sends it as a new aggregate */
	if (d <= 2048)
		s->next_seq = (seq + 1) & 0xfff;
	return true;
}

/* ampdu.md 1.6: does the frame differ from the FIFO's last in station, TID, kind or rate? */
static bool epoch_differs(const struct core_ampdu_fifo *fs, u8 kind, const struct bcm4360_txparams *p,
			  const u8 *plcp)
{
	u32 i;

	if (fs->kind != kind || fs->peer != p->peer || fs->tid != p->tid)
		return true;
	for (i = 0; i < 5; i++) {
		if (kind == 1 && i != 0 && i != 3)
			continue;		/* HT: only bytes 0 and 3 */
		if (fs->plcp[i] != plcp[i])
			return true;
	}
	return false;
}

/*
 * ampdu.md 1.4, 1.5, 1.6: the A-MPDU parts of the header of one frame, after
 * tx.c built it as for a single frame: for an aggregatable MPDU the control
 * bits, the cache bytes and the epoch; for any other frame the epoch flip
 * when the FIFO's last frame was aggregated (wlc_send_q).
 */
void core_ampdu_header(struct bcm4360_core *c, u8 *prefix, u8 *h, const u8 *f,
		       const struct bcm4360_txparams *p)
{
	struct core_ampdu *a = &c->fr.ampdu;
	struct core_ampdu_fifo *fs = &a->fifo[p->fifo];
	struct core_ampdu_sess *s = p->ampdu ? sess_of(c, p->peer, p->tid) : NULL;
	bool agg = core_ampdu_is_agg(c, p);
	const u8 *plcp = h + TX_RATE_ENTRY(0) + TX_RATE_PLCP;
	u8 kind = epoch_kind(p, agg), i;
	u16 low = h[TXH_CTL_LOW] | (u16)h[TXH_CTL_LOW + 1] << 8;
	u16 high = h[TXH_CTL_HIGH] | (u16)h[TXH_CTL_HIGH + 1] << 8;
	bool gap = false, flip;
	const struct core_ampdu_peer *pe;

	if (s && s->started)
		gap = seq_gap(s, f);
	if (!agg) {
		/* 1.4: a session frame sent alone (or torn down); 1.6 reasons 2 and 3 */
		if (p->ampdu) {
			low &= ~TXC_AMPDU;
			high |= TXC_HI_VHT;
			put16(h, TXH_CTL_LOW, low);
			put16(h, TXH_CTL_HIGH, high);
		}
		if (fs->kind) {
			epoch_flip(fs);
			fs->kind = 0;
			prefix[PFX_EPOCH_BYTE] |= fs->epoch ? PFX_EPOCH : 0;
		}
		return;
	}
	pe = &a->peer[p->peer];
	low |= TXC_AMPDU;
	high &= ~TXC_HI_VHT;
	put16(h, TXH_CTL_LOW, low);
	put16(h, TXH_CTL_HIGH, high);
	h[TXH_AMPDU_MAX] = pe->max_mpdus;
	h[TXH_AMPDU_MAX_FB] = pe->max_mpdus;
	put16(h, TXH_AMPDU_DUR, (AMPDU_MAX_DUR & 0xfff0) | (pe->density & 0xf));
	h[TXH_AMPDU_WIN] = s->window - 1;
	h[TXH_AMPDU_RES] = 0;

	flip = gap || epoch_differs(fs, kind, p, plcp);
	if (flip)
		epoch_flip(fs);
	fs->kind = kind;
	fs->peer = p->peer;
	fs->tid = p->tid;
	for (i = 0; i < 5; i++)
		fs->plcp[i] = plcp[i];
	if (fs->epoch)
		prefix[PFX_EPOCH_BYTE] |= PFX_EPOCH;
}

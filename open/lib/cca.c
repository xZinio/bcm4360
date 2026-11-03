// SPDX-License-Identifier: ISC
/*
 * Channel statistics: per channel and second, how much of the air time went
 * to the own network, to other networks and to interference, accumulated
 * from the microcode's occupancy counters once per watchdog tick in a ring
 * of 60 one-second slots per channel; a driver module (watchdog, down
 * function, the iovar "cca_get_stats").
 *
 * Written from docs/re/spec/cca-stf.md, "Procedures, part 1: wlc_cca.c" and
 * the parts of "Data" it uses. Everything of the driver is reached through
 * the bcm4360_cca_env_* services with the caller's handle `wlc`.
 */
#include <bcm4360/cca.h>
#include <bcm4360/channel.h>
#include <bcm4360/hw.h>

/* cca-stf.md, Data: "Error codes" */
#define BCME_BUFTOOSHORT	(-14)
#define BCME_BADCHAN		(-20)
#define BCME_UNSUPPORTED	(-23)
#define BCME_NOMEM		(-27)

/* cca-stf.md, Data: "The state cca_info" */
#define CCA_STATE_SIZE		0x4390	/* allocated and freed with this size */
#define CCA_CHANNELS		38
#define CCA_SECONDS		60	/* the ring length, nsecs */
#define CCA_POOL		450
#define CCA_COUNTERS		9	/* words of wlc_bmac_cca_stats_read */
#define CCA_SAMPLE_FREE		0xffff	/* busy-own of a free sample */
#define CCA_SAMPLE_FIRST	1	/* pool[0] is never handed out */

/* the counter words (cca-stf.md, Data: meaning of the sample fields) */
#define CNT_TX			0
#define CNT_OWN			1
#define CNT_OTHER_A		2
#define CNT_OTHER_B		3
#define CNT_INTERFERENCE	4
#define CNT_TSF			5	/* TSF low word, microseconds */
#define CNT_PS			6	/* subtracted while power save is enabled */
#define CNT_A			7
#define CNT_B			8

/* wlc_cca_attach, step 1: MAC core revisions without the module */
#define CCA_COREREV_MIN		15

/* cca-stf.md, Data: "The output of cca_query_stats" */
#define REC_HDR			4	/* u16 chanspec, u8 count, one byte not written */
#define REC_HDR_COUNT		2
#define REC_SIZE		0x1c	/* 7 words */
#define REC_WORDS		7
#define REC_ROUND		500	/* (x + 500) / 1000 */
#define REC_SCALE		1000

/* cca-stf.md, Data: "The value of the iovar cca_get_stats" */
#define IOV_MINLEN		24
#define IOV_PARAM_COUNT		2	/* u8 number of seconds at params+2 */
#define IOV_REC_SIZE		0x14	/* the first five words of a record */
#define IOV_REC_WORDS		5
#define IOV_BUF_FIRST		0x20	/* the query buffer for one second */

/* cca-stf.md, Data: "The iovar table of the module" */
#define IOV_CCA_GET_STATS	0
#define IOVAR_GET		0	/* action id of a get */

static const char cca_module_name[] = "cca_stats";

static const struct bcm4360_iovar cca_iovars[] = {
	{ .name = "cca_get_stats", .varid = IOV_CCA_GET_STATS, .flags = 0,
	  .type = BCM4360_IOVT_BUFFER, .minlen = IOV_MINLEN },
	{ .name = NULL },
};

/* cca-stf.md, Data: "The channel table" (all 20 MHz chanspecs) */
static const u16 cca_chanspecs[CCA_CHANNELS] = {
	0x1001, 0x1002, 0x1003, 0x1004, 0x1005, 0x1006, 0x1007,
	0x1008, 0x1009, 0x100a, 0x100b, 0x100c, 0x100d, 0x100e,
	0xd024, 0xd028, 0xd02c, 0xd030, 0xd034, 0xd038, 0xd03c, 0xd040,
	0xd064, 0xd068, 0xd06c, 0xd070, 0xd074, 0xd078, 0xd07c, 0xd080,
	0xd084, 0xd088, 0xd08c, 0xd095, 0xd099, 0xd09d, 0xd0a1, 0xd0a5,
};

/* cca-stf.md, Data: "The state cca_info", a pool sample */
struct cca_sample {
	u32 time;
	u32 busy_own;		/* CCA_SAMPLE_FREE: free */
	u32 busy_other;
	u32 interference;
	u32 timestamp;
	u32 counter_a;
	u32 counter_b;
};

/* a channel of the table and its ring of sample indices (0 = none) */
struct cca_chan {
	u16 chanspec;
	u16 slot[CCA_SECONDS];
};

/*
 * The state, laid out as the specification gives it: wlc_cca_get_chanspec
 * reads chan[i] for any index i, so the layout around chan[] is visible.
 */
struct bcm4360_cca {
	void *wlc;
	u32 last[CCA_COUNTERS];
	struct cca_chan chan[CCA_CHANNELS];
	s32 cur;
	s32 nsecs;
	u32 nosample;
	struct cca_sample pool[CCA_POOL];
};

_Static_assert(sizeof(struct cca_chan) == 0x7a, "cca_info channel");
_Static_assert(__builtin_offsetof(struct bcm4360_cca, chan) == 0x2c, "cca_info chan");
_Static_assert(__builtin_offsetof(struct bcm4360_cca, cur) == 0x1248, "cca_info cur");
_Static_assert(__builtin_offsetof(struct bcm4360_cca, pool) == 0x1254, "cca_info pool");
_Static_assert(sizeof(struct bcm4360_cca) <= CCA_STATE_SIZE, "cca_info size");

/* the first channel of the table with chanspec cs, or -1 */
static s32 cca_find(const struct bcm4360_cca *cca, u16 cs)
{
	s32 c;

	for (c = 0; c < CCA_CHANNELS; c++)
		if (cca->chan[c].chanspec == cs)
			return c;
	return -1;
}

/* the sample of chan[c].slot[s] back to the pool */
static void cca_slot_free(struct bcm4360_cca *cca, s32 c, s32 s)
{
	u16 i = cca->chan[c].slot[s];

	if (!i)
		return;
	cca->pool[i].busy_own = CCA_SAMPLE_FREE;
	cca->chan[c].slot[s] = 0;
}

/*
 * cca-stf.md, sub_06e17b step 6 (also cca_stats_upd step 4.1): a fresh
 * sample for chan[c] at the current ring position.
 */
static void cca_sample_alloc(struct bcm4360_cca *cca, s32 c)
{
	u32 i;

	for (i = CCA_SAMPLE_FIRST; i < CCA_POOL; i++) {
		if (cca->pool[i].busy_own != CCA_SAMPLE_FREE)
			continue;
		hw_memset(&cca->pool[i], 0, sizeof(cca->pool[i]));
		cca->chan[c].slot[cca->cur] = (u16)i;
		return;
	}
	cca->nosample++;
}

/* the 20 MHz control chanspec of the upper layer's current chanspec */
static u16 cca_cur_ctlchspec(struct bcm4360_cca *cca)
{
	return bcm4360_chspec_ctlchspec(bcm4360_cca_env_chanspec(cca->wlc));
}

/* cca-stf.md, "wlc_cca_attach" */
struct bcm4360_cca *bcm4360_cca_attach(void *wlc, void *osh, u32 unit, u32 corerev)
{
	struct bcm4360_cca *cca;
	u32 c, i;

	(void)unit;
	/* step 1 */
	if (corerev < CCA_COREREV_MIN)
		return NULL;
	/* step 2 (wlc_calloc: allocate, then zero) */
	cca = osl_malloc(osh, CCA_STATE_SIZE);
	if (!cca)
		return NULL;
	hw_memset(cca, 0, CCA_STATE_SIZE);
	/* step 3 */
	cca->wlc = wlc;
	cca->nsecs = CCA_SECONDS;
	cca->cur = 0;
	hw_memset(cca->last, 0, sizeof(cca->last));
	/* step 4 */
	for (c = 0; c < CCA_CHANNELS; c++)
		cca->chan[c].chanspec = cca_chanspecs[c];
	/* step 5: all free, pool[0] too */
	for (i = 0; i < CCA_POOL; i++)
		cca->pool[i].busy_own = CCA_SAMPLE_FREE;
	/* step 6 */
	if (bcm4360_cca_env_module_register(wlc, cca_iovars, cca_module_name, cca,
					    bcm4360_cca_doiovar, bcm4360_cca_watchdog, NULL,
					    bcm4360_cca_down)) {
		osl_mfree(osh, cca, CCA_STATE_SIZE);
		return NULL;
	}
	/* step 7 */
	return cca;
}

/* cca-stf.md, "wlc_cca_detach" */
void bcm4360_cca_detach(struct bcm4360_cca *cca)
{
	void *w = cca->wlc;

	bcm4360_cca_env_module_unregister(w, cca_module_name, cca);
	osl_mfree(bcm4360_cca_env_osh(w), cca, CCA_STATE_SIZE);
}

/* cca-stf.md, "sub_06dd9c" (the module's down function) */
s32 bcm4360_cca_down(void *hdl)
{
	struct bcm4360_cca *cca = hdl;
	s32 s, c;

	if (!cca)
		return 0;
	for (s = 0; s < cca->nsecs; s++)
		for (c = 0; c < CCA_CHANNELS; c++)
			cca_slot_free(cca, c, s);
	return 0;
}

/* cca-stf.md, "sub_06e17b" (the module's watchdog) */
s32 bcm4360_cca_watchdog(void *hdl)
{
	struct bcm4360_cca *cca = hdl;
	s32 c, c2;
	u16 cs;

	/* step 1 */
	if (!cca)
		return 0;
	/* step 2 */
	cs = cca_cur_ctlchspec(cca);
	/* step 3 */
	cca->cur = cca->cur == cca->nsecs - 1 ? 0 : cca->cur + 1;
	/* step 4 */
	c = cca_find(cca, cs);
	if (c < 0)
		return BCME_BADCHAN;
	/* step 5 */
	for (c2 = 0; c2 < CCA_CHANNELS; c2++)
		cca_slot_free(cca, c2, cca->cur);
	/* step 6 */
	cca_sample_alloc(cca, c);
	/* step 7 */
	bcm4360_cca_stats_upd(cca, 1);
	return 0;
}

/* cca-stf.md, "cca_stats_upd" */
void bcm4360_cca_stats_upd(struct bcm4360_cca *cca, s32 calc)
{
	u32 v[CCA_COUNTERS], d[CCA_COUNTERS];
	struct cca_sample *p;
	u32 k;
	s32 c;
	u16 i;

	/* steps 1 and 2 */
	if (!cca)
		return;
	c = cca_find(cca, cca_cur_ctlchspec(cca));
	if (c < 0)
		return;
	/* step 3 */
	if (bcm4360_cca_env_stats_read(cca->wlc, v))
		return;
	/* step 4 */
	if (calc) {
		if (!cca->chan[c].slot[cca->cur])
			cca_sample_alloc(cca, c);
		i = cca->chan[c].slot[cca->cur];
		if (i) {
			p = &cca->pool[i];
			for (k = 0; k < CCA_COUNTERS; k++)
				d[k] = v[k] - cca->last[k];
			p->time += d[CNT_TSF];
			if (bcm4360_cca_env_pm_enabled(cca->wlc))
				p->time -= d[CNT_PS];
			p->busy_own += d[CNT_TX] + d[CNT_OWN];
			p->busy_other += d[CNT_OTHER_A] + d[CNT_OTHER_B];
			p->interference += d[CNT_INTERFERENCE];
			p->counter_a += d[CNT_A];
			p->counter_b += d[CNT_B];
			p->timestamp = osl_sysuptime();
		}
	}
	/* step 5 */
	for (k = 0; k < CCA_COUNTERS; k++)
		cca->last[k] = v[k];
}

/* cca-stf.md, "cca_stats_tsf_upd" */
void bcm4360_cca_stats_tsf_upd(struct bcm4360_cca *cca)
{
	u32 lo, hi;

	if (!cca)
		return;
	bcm4360_cca_env_read_tsf(cca->wlc, &lo, &hi);
	cca->last[CNT_TSF] = lo;
}

/* a time or count in a record: microseconds -> milliseconds etc., rounded */
static u32 cca_round(u32 x)
{
	return (x + REC_ROUND) / REC_SCALE;
}

static void put_u16(void *p, u16 v)
{
	u8 *b = p;

	b[0] = (u8)v;
	b[1] = (u8)(v >> 8);
}

static u16 get_u16(const void *p)
{
	const u8 *b = p;

	return (u16)(b[0] | (b[1] << 8));
}

/* cca-stf.md, "cca_query_stats" */
s32 bcm4360_cca_query_stats(struct bcm4360_cca *cca, u16 cs, s32 n, void *out, u32 size)
{
	const struct cca_sample *p;
	u8 *o = out;
	u32 room, *rec;
	s32 m, pos, c, k;

	/* step 1 */
	if (!cca)
		return 0;
	/* step 2 */
	m = n <= cca->nsecs ? n : cca->nsecs;
	pos = cca->cur;
	/* step 3 */
	c = cca_find(cca, cs);
	if (c < 0) {
		o[REC_HDR_COUNT] = 0;
		put_u16(o, 0);
		return 0;
	}
	/* step 4 */
	put_u16(o, cs);
	room = size - REC_HDR;
	k = 0;
	/* step 5: newest first, from the position before cur */
	while (room > REC_SIZE - 1 && k < m) {
		if (pos == 0)
			pos = cca->nsecs;
		pos--;
		room -= REC_SIZE;
		p = &cca->pool[cca->chan[c].slot[pos]];
		rec = (u32 *)(o + REC_HDR + REC_SIZE * k);
		rec[0] = cca_round(p->time);
		rec[1] = cca_round(p->busy_own);
		rec[2] = cca_round(p->busy_other);
		rec[3] = cca_round(p->interference);
		rec[4] = cca_round(p->timestamp);
		rec[5] = p->counter_a;
		rec[6] = p->counter_b;
		k++;
	}
	/* step 6 */
	o[REC_HDR_COUNT] = (u8)k;
	return 0;
}

/* cca-stf.md, "sub_06e3fe" (the module's iovar handler) */
s32 bcm4360_cca_doiovar(void *hdl, const struct bcm4360_iovar *vi, u32 actionid,
			const char *name, void *params, u32 p_len, void *arg, s32 len, s32 val_size,
			void *wlcif)
{
	struct bcm4360_cca *cca = hdl;
	const u8 *pp = params;
	u8 *a = arg, *buf;
	u32 size, k, w;
	void *osh;
	s32 r;
	u16 cs;
	u8 n;

	(void)vi;
	(void)name;
	(void)val_size;
	(void)wlcif;
	/* step 1; step 2 has no effect */
	if (!cca)
		return 0;
	/* step 3 */
	if (actionid != IOVAR_GET)
		return BCME_UNSUPPORTED;
	/* step 4 */
	if (len < IOV_MINLEN || p_len < IOV_MINLEN)
		return BCME_BUFTOOSHORT;
	/* step 5 */
	if (bcm4360_chspec_malformed(get_u16(pp)))
		return BCME_BADCHAN;
	/* step 6 */
	cs = bcm4360_chspec_ctlchspec(get_u16(pp));
	n = pp[IOV_PARAM_COUNT];
	size = n == 0 ? IOV_BUF_FIRST : (n - 1) * REC_SIZE + IOV_BUF_FIRST;
	/* step 7 */
	osh = bcm4360_cca_env_osh(cca->wlc);
	buf = osl_malloc(osh, size);
	if (!buf)
		return BCME_NOMEM;
	hw_memset(buf, 0, size);
	/* step 8 */
	r = bcm4360_cca_query_stats(cca, cs, n, buf, size);
	if (r == 0) {
		put_u16(a, get_u16(buf));
		a[REC_HDR_COUNT] = buf[REC_HDR_COUNT];
		/* the requested count of records, not the reported one */
		for (k = 0; k < n; k++)
			for (w = 0; w < IOV_REC_WORDS; w++)
				((u32 *)(a + REC_HDR + IOV_REC_SIZE * k))[w] =
					((u32 *)(buf + REC_HDR + REC_SIZE * k))[w];
	}
	/* step 9 */
	osl_mfree(osh, buf, size);
	return r;
}

/* cca-stf.md, "wlc_cca_get_chanspec": chan[i] for any i, not bounded */
u16 bcm4360_cca_get_chanspec(struct bcm4360_cca *cca, s32 i)
{
	const u8 *base;

	if (!cca)
		return 0;
	base = (const u8 *)cca + __builtin_offsetof(struct bcm4360_cca, chan);
	return get_u16(base + (s64)i * (s64)sizeof(struct cca_chan));
}

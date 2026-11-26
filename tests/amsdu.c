// SPDX-License-Identifier: GPL-2.0-only
/* Exercise the real receive guard with independent card/AMI instances.
 * Only the blob's state transitions and card lookup are substituted.
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "hybrid/hybrid.h"

extern bool bcm4360_amsdu_guard;
extern unsigned int bcm4360_amsdu_broken_chains;
void wlc_recvamsdu(void *ami, void *rxh, void *p);

struct fake_pub {
	u8 pad[0x100];
	const void *sih;
};

struct fake_ami {
	void *wlc;
	struct fake_pub *pub;
	u8 pad[0x68 - 2 * sizeof(void *)];
	u32 state;
	u32 delivered;
	u32 discarded;
};

_Static_assert(offsetof(struct fake_ami, pub) == 0x08, "AMI pub offset");
_Static_assert(offsetof(struct fake_ami, state) == 0x68, "AMI state offset");
_Static_assert(offsetof(struct fake_pub, sih) == 0x100, "pub sih offset");

static struct hyb_card cards[2];
static struct fake_pub pubs[2];
static struct fake_ami amis[2];
static unsigned int delivered, discarded, checks, failures;

struct hyb_card *hyb_card_of(const void *sih)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(cards); i++)
		if (sih == &cards[i])
			return &cards[i];
	return NULL;
}

void bcm4360_blob_wlc_recvamsdu(void *arg, void *rxh, void *p)
{
	struct fake_ami *ami = arg;
	u32 agg = (((u8 *)rxh)[0x12] & 6) >> 1;

	(void)p;
	if (agg == 1) {
		ami->state = 1;
	} else if (agg == 3) {
		ami->state = 0;
		ami->delivered++;
		delivered++;
	} else if (ami->state != 1) {
		ami->state = 0;
		ami->discarded++;
		discarded++;
	} else if (agg == 2) {
		ami->state = 0;
		ami->delivered++;
		delivered++;
	}
}

static void reset(void)
{
	unsigned int i;

	memset(cards, 0, sizeof(cards));
	memset(pubs, 0, sizeof(pubs));
	memset(amis, 0, sizeof(amis));
	for (i = 0; i < ARRAY_SIZE(cards); i++) {
		pubs[i].sih = &cards[i];
		amis[i].pub = &pubs[i];
	}
	bcm4360_amsdu_guard = true;
	bcm4360_amsdu_broken_chains = 0;
	delivered = discarded = 0;
}

static void feed(struct fake_ami *ami, u32 agg, u32 idx)
{
	u8 rxh[0x14] = {0};
	u16 s2 = (u16)((idx << 10) | (agg << 1));

	rxh[0x12] = s2 & 0xff;
	rxh[0x13] = s2 >> 8;
	wlc_recvamsdu(ami, rxh, NULL);
}

static void expect(const char *name, bool ok)
{
	checks++;
	printf("%s %s\n", ok ? "ok  " : "FAIL", name);
	if (!ok)
		failures++;
}

int main(void)
{
	struct fake_ami other;

	reset();
	feed(&amis[0], 1, 10); feed(&amis[0], 0, 11); feed(&amis[0], 2, 12);
	expect("contiguous first/intermediate/last", delivered == 1 && !discarded);
	reset();
	feed(&amis[0], 1, 63); feed(&amis[0], 2, 0);
	expect("sequence wrap 63 to 0", delivered == 1 && !discarded);
	reset();
	feed(&amis[0], 1, 10); feed(&amis[0], 2, 12);
	expect("missing last predecessor", !delivered && discarded == 1 &&
	       bcm4360_amsdu_broken_chains == 1);
	reset();
	feed(&amis[0], 1, 10); feed(&amis[0], 0, 12); feed(&amis[0], 2, 13);
	expect("missing intermediate flushes once", !delivered && discarded == 2 &&
	       bcm4360_amsdu_broken_chains == 1);
	reset();
	feed(&amis[0], 1, 10); feed(&amis[0], 2, 10);
	expect("duplicate sequence rejected", !delivered && discarded == 1);
	reset();
	feed(&amis[0], 1, 10); feed(&amis[0], 1, 20); feed(&amis[0], 2, 21);
	expect("new first replaces an incomplete chain", delivered == 1 &&
	       !bcm4360_amsdu_broken_chains);
	reset();
	bcm4360_amsdu_guard = false;
	feed(&amis[0], 1, 10); feed(&amis[0], 2, 12);
	expect("disabled guard delegates unchanged", delivered == 1 &&
	       !bcm4360_amsdu_broken_chains);
	reset();
	feed(&amis[0], 2, 12);
	expect("orphan last is left to the blob", discarded == 1 &&
	       !bcm4360_amsdu_broken_chains);
	reset();
	feed(&amis[0], 1, 10); feed(&amis[1], 1, 20);
	feed(&amis[0], 2, 11); feed(&amis[1], 2, 21);
	expect("interleaved cards preserve both valid chains", delivered == 2 &&
	       !discarded && !bcm4360_amsdu_broken_chains);
	reset();
	feed(&amis[0], 1, 10); feed(&amis[1], 1, 11);
	feed(&amis[0], 2, 12); feed(&amis[1], 2, 12);
	expect("other card cannot mask a missing buffer", delivered == 1 &&
	       discarded == 1 && bcm4360_amsdu_broken_chains == 1 &&
	       amis[0].discarded == 1 && amis[1].delivered == 1);
	reset();
	feed(&amis[0], 1, 10);
	other = amis[0];
	feed(&other, 2, 11);
	expect("new AMI cannot inherit another instance's sequence", !delivered &&
	       discarded == 1 && bcm4360_amsdu_broken_chains == 1);
	reset();
	feed(&amis[0], 1, 10);
	memset(&cards[0], 0, sizeof(cards[0])); /* si_attach resets a reused slot */
	feed(&amis[0], 2, 11);
	expect("reused card slot does not retain sequence state", !delivered &&
	       discarded == 1);
	reset();
	pubs[0].sih = &other; /* no tracked card */
	feed(&amis[0], 1, 10); feed(&amis[0], 2, 11);
	expect("untracked card discards an unverifiable chain", !delivered &&
	       discarded == 1 && bcm4360_amsdu_broken_chains == 1);
	printf("amsdu: %u checks, %u failures\n", checks, failures);
	return failures ? 1 : 0;
}

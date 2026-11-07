/*
 * Hardware keys (docs/re/spec/ps-keys.md, section 2): the key table in
 * shared memory, the security index block, the binding of a pairwise key to
 * its peer's address in the address match table (AMT), the TKIP receive
 * phase-1 block. The packet numbers and the IV are the platform's (2.4).
 */
#include "core.h"

#define SHM_SECINDEX		0x5e0	/* ps-keys.md 2.1: one word per entry */
#define SHM_TKIP_P1K		0x2e0	/* 7 words per entry, from entry 4 */
#define SECINDEX_WORDS		68
#define AMT_KEY_FIRST		4	/* entry i >= 4 is bound to AMT[i - 4] */
#define AMT_ATTR_KEY		0x8004	/* bit 2: key peer; bit 15: keep the old attribute bits */
#define AMT_ATTR_OR		0x8000
#define AMT_ENTRY_BYTES		8
#define AMT_SEL			0x40000	/* object memory selector of the AMT (bmac-init.md B4) */
#define KEY_WORDS		8	/* 16 bytes per key entry */
#define KEY_ENTRY_BYTES		16
#define HF1_WEP_UNICAST		0x4000	/* host flag 1 bit 14 (ps-keys.md 2.1) */

#define E_INVAL			(-22)
#define E_NOSPC			(-28)
#define E_NOTSUP		(-95)

static void shm_w(struct bcm4360_core *c, u32 off, u16 v)
{
	bcm4360_shm_write(c->hw, (u16)off, v);
}

static bool is_wep(u8 cipher)
{
	return cipher == PK_CIPHER_WEP40 || cipher == PK_CIPHER_WEP104;
}

/* ps-keys.md 2.4 step 2: the length of the ICV / MIC the engine appends */
static u8 icv_len(u8 cipher)
{
	return cipher == PK_CIPHER_CCMP ? 8 : 4;
}

/* ps-keys.md 2.3 step 2: free the AMT entry of a key entry before it is written */
static void amt_clear(struct bcm4360_core *c, u32 i)
{
	static const u8 zero[6];
	u8 e[AMT_ENTRY_BYTES];
	u16 attr;

	CORE_MAC_CALL(c, bcm4360_bmac_copyfrom_objmem(m, (i - AMT_KEY_FIRST) * AMT_ENTRY_BYTES, e,
						      AMT_ENTRY_BYTES, AMT_SEL));
	attr = e[6] | (u16)e[7] << 8;
	if (attr & AMT_ATTR_OR)
		CORE_MAC_CALL(c, bcm4360_bmac_write_amt(m, i - AMT_KEY_FIRST, zero,
							attr & ~AMT_ATTR_OR));
	else
		CORE_MAC_CALL(c, bcm4360_bmac_set_rcmta(m, i - AMT_KEY_FIRST, zero));
}

/* ps-keys.md 2.1: host flag 1 bit 14 follows the WEP keys among the default keys */
static void wep_hostflag(struct bcm4360_core *c)
{
	bool wep = false;
	u32 i;

	for (i = 0; i < PK_NGROUP; i++)
		if (c->pk.key[i].used && is_wep(c->pk.key[i].cipher))
			wep = true;
	CORE_MAC_CALL(c, bcm4360_bmac_mhf(m, 0, HF1_WEP_UNICAST, wep ? HF1_WEP_UNICAST : 0, 3));
}

/* ps-keys.md 2.3: wlc_key_hw_init, one entry, in the object's order */
static void key_write(struct bcm4360_core *c, u32 i)
{
	const struct core_key *k = &c->pk.key[i];
	bool present = k->used && k->cipher;
	u32 base = c->w.key_base + KEY_ENTRY_BYTES * i, w;

	if (i >= AMT_KEY_FIRST)
		amt_clear(c, i);
	shm_w(c, SHM_SECINDEX + 2 * i, (present ? k->cipher : 0) | (u16)(i << 4));
	for (w = 0; w < KEY_WORDS; w++)
		shm_w(c, base + 2 * w,
		      present ? (u16)(k->key[2 * w] | (u16)k->key[2 * w + 1] << 8) : 0);
	if (present && k->cipher == PK_CIPHER_TKIP && i >= AMT_KEY_FIRST) {
		base = SHM_TKIP_P1K + 14 * i;
		for (w = 0; w < 5; w++)
			shm_w(c, base + 2 * w, k->p1k[w]);
		shm_w(c, base + 10, (u16)k->iv32);
		shm_w(c, base + 12, (u16)(k->iv32 >> 16));
	}
	if (i >= AMT_KEY_FIRST && present)
		CORE_MAC_CALL(c, bcm4360_bmac_write_amt(m, i - AMT_KEY_FIRST, k->addr,
							AMT_ATTR_KEY));
	if (i < PK_NGROUP)
		wep_hostflag(c);
}

/* ps-keys.md 2.3 last paragraph: wlc_key_hw_init_all writes every configured key again */
void core_keys_reinit(struct bcm4360_core *c)
{
	u32 i;

	for (i = 0; i < PK_NKEYS; i++)
		if (c->pk.key[i].used)
			key_write(c, i);
}

/* ps-keys.md 2.2: the entry a key gets; the key bytes as mac80211 gives them */
int bcm4360_core_set_key(struct bcm4360_core *c, u8 cipher, u8 key_id, const u8 *addr,
			 const u8 *key, u8 keylen, const u16 *rx_p1k, u32 rx_iv32)
{
	static const u8 zero[6];
	struct core_key *k;
	u32 i, n;

	switch (cipher) {
	case PK_CIPHER_WEP40:
		n = 5;
		break;
	case PK_CIPHER_WEP104:
		n = 13;
		break;
	case PK_CIPHER_CCMP:
		n = 16;
		break;
	case PK_CIPHER_TKIP:
		n = 32;
		break;
	default:
		return E_NOTSUP;
	}
	if (keylen != n || !key)
		return E_INVAL;
	if (!addr) {
		if (cipher == PK_CIPHER_TKIP)		/* 2.1: the host decrypts those */
			return E_NOTSUP;
		if (key_id >= PK_NGROUP)
			return E_INVAL;
		i = key_id;
	} else {
		if (!hw_memcmp(addr, zero, 6))
			return E_INVAL;
		for (i = AMT_KEY_FIRST; i < PK_NKEYS; i++)
			if (c->pk.key[i].used && !hw_memcmp(c->pk.key[i].addr, addr, 6))
				break;
		if (i == PK_NKEYS)
			for (i = AMT_KEY_FIRST; i < PK_NKEYS; i++)
				if (!c->pk.key[i].used)
					break;
		if (i == PK_NKEYS)
			return E_NOSPC;
	}
	k = &c->pk.key[i];
	hw_memset(k, 0, sizeof(*k));
	k->used = 1;
	k->cipher = cipher;
	hw_memcpy(k->key, key, n < KEY_ENTRY_BYTES ? n : KEY_ENTRY_BYTES);
	if (cipher == PK_CIPHER_TKIP) {
		/* mac80211's order: transmit MIC key, then receive MIC key */
		hw_memcpy(k->txmic, key + 16, 8);
		hw_memcpy(k->rxmic, key + 24, 8);
		if (rx_p1k) {
			for (n = 0; n < 5; n++)
				k->p1k[n] = rx_p1k[n];
			k->iv32 = rx_iv32;
		}
	}
	if (addr) {
		k->has_addr = 1;
		hw_memcpy(k->addr, addr, 6);
	}
	if (c->up)
		key_write(c, i);
	return (int)i;
}

/* ps-keys.md 2.2 (wlc_key_delete), 2.7 DISABLE_KEY: the entry written as empty */
void bcm4360_core_del_key(struct bcm4360_core *c, u8 entry)
{
	if (entry >= PK_NKEYS || !c->pk.key[entry].used)
		return;
	hw_memset(&c->pk.key[entry], 0, sizeof(c->pk.key[entry]));
	if (c->up)
		key_write(c, entry);
}

/* ps-keys.md 3: the receive phase-1 block of a TKIP pairwise entry (the seven words only) */
void bcm4360_core_tkip_rx_update(struct bcm4360_core *c, u8 entry, u32 iv32, const u16 *p1k)
{
	struct core_key *k;
	u32 base, w;

	if (entry < AMT_KEY_FIRST || entry >= PK_NKEYS)
		return;
	k = &c->pk.key[entry];
	if (!k->used || k->cipher != PK_CIPHER_TKIP)
		return;
	for (w = 0; w < 5; w++)
		k->p1k[w] = p1k[w];
	k->iv32 = iv32;
	if (!c->up)
		return;
	base = SHM_TKIP_P1K + 14 * entry;
	for (w = 0; w < 5; w++)
		shm_w(c, base + 2 * w, k->p1k[w]);
	shm_w(c, base + 10, (u16)iv32);
	shm_w(c, base + 12, (u16)(iv32 >> 16));
}

/* ps-keys.md 2.4 step 2: what the transmit header needs of a key entry; 0 or -1 (not usable) */
int core_keys_tx(struct bcm4360_core *c, u32 entry, u8 *cipher, u8 *icvlen)
{
	const struct core_key *k;

	if (entry >= PK_NKEYS)
		return -1;
	k = &c->pk.key[entry];
	if (!k->used || !k->cipher)
		return -1;
	*cipher = k->cipher;
	*icvlen = icv_len(k->cipher);
	return 0;
}

/* ps-keys.md 2.1: host flag 1 bit 14 as the group keys want it (wlc_key_defkeyflag; a join) */
void core_keys_hostflag(struct bcm4360_core *c)
{
	wep_hostflag(c);
}

/* the platform sets its keys again after a restart: forget the table (no hardware access;
 * the next up clears the hardware table itself) */
void core_keys_forget(struct bcm4360_core *c)
{
	hw_memset(c->pk.key, 0, sizeof(c->pk.key));
}

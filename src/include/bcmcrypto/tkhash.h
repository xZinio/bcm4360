/*
 * tkhash.h
 * Prototypes for TKIP hash functions.
 *
 * $Id: tkhash.h 458337 2014-02-26 18:39:26Z $
 */

#ifndef _TKHASH_H_
#define _TKHASH_H_

#include <typedefs.h>

#define TKHASH_P1_KEY_SIZE	10	
#define TKHASH_P2_KEY_SIZE	16	

extern void BCMROMFN(tkhash_phase1)(uint16 *P1K, const uint8 *TK, const uint8 *TA, uint32 IV32);
extern void BCMROMFN(tkhash_phase2)(uint8 *RC4KEY, const uint8 *TK, const uint16 *P1K, uint16 IV16);

#endif 

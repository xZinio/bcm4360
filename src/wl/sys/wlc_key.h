/*
 * Key management related declarations
 * and exported functions for
 * Broadcom 802.11abg Networking Device Driver
 *
 * $Id: wlc_key.h 458427 2014-02-26 23:12:38Z $
 */

#ifndef _wlc_key_h_
#define _wlc_key_h_

#include <bcmcrypto/tkhash.h>

typedef struct tkip_info {
	uint16		phase1[TKHASH_P1_KEY_SIZE/sizeof(uint16)];	
	uint8		phase2[TKHASH_P2_KEY_SIZE];	
	uint32		micl;
	uint32		micr;
} tkip_info_t;

typedef struct wsec_iv {
	uint32		hi;	
	uint16		lo;	
} wsec_iv_t;

#define WLC_NUMRXIVS	4	

#define TWSIZE	128

typedef struct wsec_key {
	struct ether_addr ea;		
	uint8		idx;		
	uint8		id;		
	uint8		algo;		
	uint8		rcmta;		
	uint16		flags;		
	uint8 		algo_hw;	
	uint8 		aes_mode;	
	int8		iv_len;		
	int8		icv_len;	
	uint32		len;		

	uint8		data[DOT11_MAX_KEY_SIZE];	
	wsec_iv_t	rxiv[WLC_NUMRXIVS];		
	wsec_iv_t	txiv;		
	tkip_info_t	tkip_tx;	
	tkip_info_t	tkip_rx;	
	uint32		tkip_rx_iv32;	
	uint8		tkip_rx_ividx;	
	uint8		tkip_tx_lefts;	
	uint8		tkip_tx_left[4];	
	uint16		tkip_tx_offset;	
	uint8		tkip_tx_fmic[8];	
	int		tkip_tx_fmic_written;	

#if defined(UCODE_SEQ)
	wsec_iv_t	bk_iv;	
	tkip_info_t	tkip_bk_tx;	
#endif
} wsec_key_t;

#endif 

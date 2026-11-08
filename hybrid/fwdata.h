/*
 * Data of Broadcom that the open code loads at runtime (hw_fw_data() of
 * open/include/bcm4360/hw.h): PHY tables, transmit gain tables, radio tables,
 * and for the MAC the microcode, its initialisation lists and a coexistence
 * table.
 * In the hybrid module it is read where it is, in the linked copy of
 * Broadcom's object (hybrid/takeover.txt makes these symbols global). Names
 * and sizes in bytes are those of lib/wlc_hybrid.o_shipped, version
 * 6.30.223.271 (SHA-256 352a6e34...), which scripts/fetch-blob.sh verifies.
 *
 * HYB_FW(name, size)
 */

HYB_FW(acphy_est_pwr_lut_core0_rev0, 256)
HYB_FW(acphy_est_pwr_lut_core1_rev0, 256)
HYB_FW(acphy_est_pwr_lut_core2_rev0, 256)
HYB_FW(acphy_iq_lut_core0_rev0, 512)
HYB_FW(acphy_iq_lut_core2_rev0, 512)
HYB_FW(acphy_loft_lut_core0_rev0, 256)
HYB_FW(acphy_loft_lut_core1_rev0, 256)
HYB_FW(acphy_loft_lut_core2_rev0, 256)
HYB_FW(acphy_mcs_tbl_rev0, 256)
HYB_FW(acphy_noise_shaping_tbl_rev0, 1024)
HYB_FW(acphy_papd_cal_scalars_tbl_core0_rev0, 256)
HYB_FW(acphy_papd_cal_scalars_tbl_core1_rev0, 256)
HYB_FW(acphy_papd_cal_scalars_tbl_core2_rev0, 256)
HYB_FW(acphy_papd_comp_epsilon_tbl_core0_rev0, 256)
HYB_FW(acphy_papd_comp_epsilon_tbl_core1_rev0, 256)
HYB_FW(acphy_papd_comp_epsilon_tbl_core2_rev0, 256)
HYB_FW(acphy_papd_comp_rfpwr_tbl_core0_rev0, 256)
HYB_FW(acphy_papd_comp_rfpwr_tbl_core1_rev0, 256)
HYB_FW(acphy_papd_comp_rfpwr_tbl_core2_rev0, 256)
HYB_FW(acphy_phasetrack_tbl_rev0, 88)
HYB_FW(acphy_rx_evm_shaping_tbl_rev0, 256)
HYB_FW(acphy_tx_evm_tbl_rev0, 38)
HYB_FW(acphy_txgain_epa_2g_2069rev0, 768)
HYB_FW(acphy_txgain_epa_2g_2069rev4, 768)
HYB_FW(acphy_txgain_epa_2g_2069rev4_id1, 768)
HYB_FW(acphy_txgain_epa_5g_2069rev0, 768)
HYB_FW(acphy_txgain_epa_5g_2069rev4, 768)
HYB_FW(acphy_txgain_ipa_2g_2069rev0, 768)
HYB_FW(acphy_txgain_ipa_5g_2069rev0, 768)
HYB_FW(acphy_txv_for_spexp, 7776)
HYB_FW(chan_tuning_2069rev3, 8932)
HYB_FW(chan_tuning_2069rev4, 8932)
HYB_FW(prefregs_2069_rev3, 92)
HYB_FW(prefregs_2069_rev4, 100)
HYB_FW(rx_farrow_tbl, 4428)
HYB_FW(tx_farrow_dac1_tbl, 4428)

/* the MAC (bmac-init.md B1, B2, B10, E10) */
HYB_FW(btc_ucode_flags, 36)
HYB_FW(d11ac1bsinitvals42, 592)
HYB_FW(d11ac1initvals42, 4888)
HYB_FW(d11ucode42, 43400)

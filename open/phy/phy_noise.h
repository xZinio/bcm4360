/* SPDX-License-Identifier: ISC */
/*
 * Between the noise path (open/phy/phy_rx.c) and the carrier-sense
 * minimum-power calibration (open/phy/phy_desense.c).
 *
 * docs/re/spec/acphy-api-rx.md, "Noise, completed" N2/N3: the per-core noise
 * level pi_ac+0x14+c (compute_dB(power >> 9) - 37) is written by sub_0b701e
 * and read only by the calibrate path of wlc_phy_crs_min_pwr_cal_acphy, which
 * N3 calls right after it (acphy-desense.md C1 step 1, corrected by
 * phy-noise); the channel change clears it (acphy-chanspec.md A1 step 9)
 * before any later reading writes it again. So it is handed over here instead
 * of being kept in struct bcm4360_phy.
 */
#ifndef BCM4360_PHY_NOISE_H
#define BCM4360_PHY_NOISE_H

#include <bcm4360/phy.h>

/*
 * acphy-desense.md C1 with restore = 0: the calibrate path, with the
 * per-core input pi_ac+0x14+c of the reading (one value per core < pi+0x168)
 */
void bcm4360_phy_crs_min_pwr_cal_noise(struct bcm4360_phy *phy, const s8 *noise_in);

#endif /* BCM4360_PHY_NOISE_H */

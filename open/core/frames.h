/* SPDX-License-Identifier: ISC */
/*
 * State of the core's frame path (docs/re/spec/frames.md), embedded in
 * struct bcm4360_core as `fr`. Owned by open/core/frames.c and the files of
 * the frame path.
 */
#ifndef BCM4360_CORE_FRAMES_H
#define BCM4360_CORE_FRAMES_H

#include <bcm4360/types.h>

struct core_frames {
	u32 placeholder;	/* the frame path's state goes here */
};

#endif

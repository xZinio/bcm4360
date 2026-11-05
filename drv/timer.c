// SPDX-License-Identifier: ISC
/*
 * Timers for the open layers (core.h, bcm4360_plat_timer_*): a delayed work
 * item per timer, whose callback runs with the device lock held, like every
 * other call into the core.
 */
#include <linux/slab.h>
#include <linux/workqueue.h>

#include "drv.h"

struct bcm4360_timer {
	struct delayed_work work;
	struct bcm4360_dev *dev;
	void (*fn)(void *arg);
	void *arg;
	u32 ms;
	bool periodic;
	bool armed;		/* added and not yet run (one-shot) or deleted */
};

static void bcm4360_timer_run(struct work_struct *work)
{
	struct bcm4360_timer *t = container_of(work, struct bcm4360_timer, work.work);
	struct bcm4360_dev *dev = t->dev;

	spin_lock_bh(&dev->lock);
	if (t->armed) {
		if (t->periodic)
			queue_delayed_work(system_wq, &t->work, msecs_to_jiffies(t->ms));
		else
			t->armed = false;
		t->fn(t->arg);
	}
	spin_unlock_bh(&dev->lock);
	bcm4360_deliver(dev);
}

void *bcm4360_plat_timer_init(void *plat, void (*fn)(void *arg), void *arg, const char *name)
{
	struct bcm4360_timer *t = kzalloc(sizeof(*t), GFP_ATOMIC);

	if (!t)
		return NULL;
	INIT_DELAYED_WORK(&t->work, bcm4360_timer_run);
	t->dev = plat;
	t->fn = fn;
	t->arg = arg;
	return t;
}

/* called with the device lock held */
void bcm4360_plat_timer_add(void *plat, void *timer, u32 ms, bool periodic)
{
	struct bcm4360_timer *t = timer;

	t->ms = ms;
	t->periodic = periodic;
	t->armed = true;
	mod_delayed_work(system_wq, &t->work, msecs_to_jiffies(ms));
}

/* called with the device lock held: the work may still run, but finds the
 * timer disarmed */
bool bcm4360_plat_timer_del(void *plat, void *timer)
{
	struct bcm4360_timer *t = timer;
	bool was = t->armed;

	t->armed = false;
	cancel_delayed_work(&t->work);
	return was;
}

/* called with the device lock held, from the core's detach: the work item
 * must not run any more once freed */
void bcm4360_plat_timer_free(void *plat, void *timer)
{
	struct bcm4360_timer *t = timer;
	struct bcm4360_dev *dev = plat;

	if (!t)
		return;
	t->armed = false;
	if (!cancel_delayed_work(&t->work) && delayed_work_pending(&t->work))
		dev_warn(&dev->pdev->dev, "timer freed while pending\n");
	/* a callback already running waits on the lock we hold and then finds
	 * the timer disarmed; free it only after that */
	spin_unlock_bh(&dev->lock);
	cancel_delayed_work_sync(&t->work);
	spin_lock_bh(&dev->lock);
	kfree(t);
}

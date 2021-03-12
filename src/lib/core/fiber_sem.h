#pragma once
/*
 * Copyright 2020, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "fiber.h"
#include "small/rlist.h"

/**
 * A counting semaphore for fibers. Kind of.
 * Actually, this differs from a normal counting semaphore in some aspects.
 *
 * First of all, the blocking is advisory rather than mandatory. You may take
 * as many resources as you want without blocking. To implement normal 'take',
 * you need a combination of the following:
 * | if (fiber_sem_would_block(sem))
 * |         fiber_sem_wait(sem);
 * | fiber_sem_take(sem, amount);
 *
 * Secondly, the limit on the resource isn't strict: you may take whichever
 * amount you like without blocking as long as you take it all at once and there
 * is at least some resource left.
 *
 * Thirdly, you may take whichever amount you like. Not just 1 piece of
 * resource.
 */
struct fiber_sem {
	/** Available resource count. */
	int64_t count;
	/** A list of waiting fibers, in FIFO order. */
	struct rlist waiters;
	/** A total number of sleeping and not yet woken fibers. */
	int waiter_count;
};

/** Check whether anyone else is waiting for the resource. */
static inline bool
fiber_sem_has_waiters(struct fiber_sem *sem)
{
	return sem->waiter_count > 0;
}

/** Check whether taking the resource would block. */
static inline bool
fiber_sem_would_block(struct fiber_sem *sem)
{
	return sem->count <= 0 || fiber_sem_has_waiters(sem);
}

/** Take @a amount of the resource, without blocking or checking limits. */
static inline void
fiber_sem_take(struct fiber_sem *sem, int64_t amount)
{
	sem->count -= amount;
}

/** Release @a amount of the resource. */
static inline void
fiber_sem_release(struct fiber_sem *sem, int64_t amount)
{
	sem->count += amount;
}

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/** Wait in queue until there is some free resource. */
void
fiber_sem_wait(struct fiber_sem *sem);

/** Wake the queue up, if there's enough resource. */
void
fiber_sem_wakeup(struct fiber_sem *sem);

/** Wake the queue up, regardless of resource count left. */
void
fiber_sem_wakeup_all(struct fiber_sem *sem);

#if defined(__cplusplus)
}
#endif /* defined(__cplusplus) */

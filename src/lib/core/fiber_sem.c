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

#include "fiber_sem.h"

void
fiber_sem_wakeup(struct fiber_sem *sem)
{
	struct rlist *list = &sem->waiters;
	if (!rlist_empty(list) && sem->count > 0)
		fiber_wakeup(rlist_first_entry(list, struct fiber, state));
}

void
fiber_sem_wait(struct fiber_sem *sem)
{
	sem->waiter_count++;
	rlist_add_tail_entry(&sem->waiters, fiber(), state);

	fiber_yield();
	/*
	 * Decrease waiter count only after actually waking up.
	 * This is done to ensure noone else can squeeze in front of this fiber,
	 * when, say, it's already waken, but hasn't been put to execution yet.
	 */
	sem->waiter_count--;
	fiber_sem_wakeup(sem);
}

void
fiber_sem_wakeup_all(struct fiber_sem *sem)
{
	if (!fiber_sem_has_waiters(sem))
		return;
	struct rlist *list = &sem->waiters;
	while (!rlist_empty(list))
		fiber_wakeup(rlist_first_entry(list, struct fiber, state));
}


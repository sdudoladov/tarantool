/*
 * Copyright 2010-2017, Tarantool AUTHORS, please see AUTHORS file.
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
#include "journal.h"
#include <small/region.h>
#include <diag.h>

struct journal *current_journal = NULL;

struct journal_queue journal_queue = {
	.max_size = 100 * 1024 * 1024, /* 100 megabytes */
	.size = 0,
	.waiters = RLIST_HEAD_INITIALIZER(journal_queue.waiters),
	.waiter_count = 0,
};

struct journal_entry *
journal_entry_new(size_t n_rows, struct region *region,
		  journal_write_async_f write_async_cb,
		  void *complete_data)
{
	struct journal_entry *entry;

	size_t size = (sizeof(struct journal_entry) +
		       sizeof(entry->rows[0]) * n_rows);

	entry = region_aligned_alloc(region, size,
				     alignof(struct journal_entry));
	if (entry == NULL) {
		diag_set(OutOfMemory, size, "region", "struct journal_entry");
		return NULL;
	}

	journal_entry_create(entry, n_rows, 0, write_async_cb,
			     complete_data);
	return entry;
}

void
journal_queue_wakeup(void)
{
	struct rlist *list = &journal_queue.waiters;
	if (!rlist_empty(list) && !journal_queue_is_full())
		fiber_wakeup(rlist_first_entry(list, struct fiber, state));
}

void
journal_queue_wait(void)
{
	if (!journal_queue_is_full() && !journal_queue_has_waiters())
		return;
	++journal_queue.waiter_count;
	rlist_add_tail_entry(&journal_queue.waiters, fiber(), state);
	/*
	 * Will be waken up by either queue emptying or a synchronous write.
	 */
	fiber_yield();
	--journal_queue.waiter_count;
	journal_queue_wakeup();
}

void
journal_queue_flush(void)
{
	if (!journal_queue_has_waiters())
		return;
	struct rlist *list = &journal_queue.waiters;
	while (!rlist_empty(list))
		fiber_wakeup(rlist_first_entry(list, struct fiber, state));
	journal_queue_wait();
}

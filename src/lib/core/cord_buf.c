/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "cord_buf.h"
#include "fiber.h"
#include "trigger.h"

#include "small/ibuf.h"

// REPLACE IT WITH ENUM !!!!!!!!!!!!!!!!!!!!!!!!
#define CORD_IBUF_START_CAPACITY 16384

struct cord_buf {
	struct ibuf base;
	struct trigger on_stop;
	struct trigger on_yield;
	struct fiber *owner;
};

static inline void
cord_buf_set_owner(struct cord_buf *buf, struct fiber *f)
{
	assert(buf->owner == NULL);
	// SLOW SHIT TRIGGERS!
	trigger_add(&f->on_stop, &buf->on_stop);
	trigger_add(&f->on_yield, &buf->on_yield);
	buf->owner = f;
	ibuf_reset(&buf->base);
}

static inline void
cord_buf_clear_owner(struct cord_buf *buf)
{
	assert(buf->owner == fiber());
	// SLOW SHIT TRIGGERS!
	trigger_clear(&buf->on_stop);
	trigger_clear(&buf->on_yield);
	buf->owner = NULL;
}

static int
cord_buf_on_stop(struct trigger *trigger, void *event)
{
	(void)event;
	struct cord_buf *buf = trigger->data;
	assert(trigger == &buf->on_stop);
	cord_buf_clear_owner(buf);
	return 0;
}

static int
cord_buf_on_yield(struct trigger *trigger, void *event)
{
	(void)event;
	struct cord_buf *buf = trigger->data;
	assert(trigger == &buf->on_yield);
	cord_buf_clear_owner(buf);
	return 0;
}

static struct cord_buf *
cord_buf_new(struct cord *c)
{
	struct cord_buf *buf = malloc(sizeof(*buf));
	if (buf == NULL)
		panic("Couldn't allocate thread buffer");
	ibuf_create(&buf->base, &c->slabc, CORD_IBUF_START_CAPACITY);
	trigger_create(&buf->on_stop, cord_buf_on_stop, buf, NULL);
	trigger_create(&buf->on_yield, cord_buf_on_yield, buf, NULL);
	buf->owner = NULL;
	return buf;
}

static void
cord_buf_delete(struct cord_buf *buf)
{
	assert(buf->owner == NULL);
	ibuf_destroy(&buf->base);
	TRASH(buf);
	free(buf);
}

static inline struct cord_buf *
cord_buf_take(void)
{
	struct cord *c = cord();
	struct cord_buf *buf = c->buf;
	if (buf != NULL)
		c->buf = NULL;
	else
		buf = cord_buf_new(c);
	cord_buf_set_owner(buf, c->fiber);
	return buf;
}

static inline void
cord_buf_put(struct cord_buf *buf)
{
	struct cord *c = cord();
	cord_buf_clear_owner(buf);
	if (c->buf == NULL)
		c->buf = buf;
	else
		cord_buf_delete(buf);
}

struct ibuf *
cord_ibuf_take(void)
{
	return &cord_buf_take()->base;
}

void
cord_ibuf_put(struct ibuf *ibuf)
{
	cord_buf_put((struct cord_buf *)ibuf);
}

void
cord_buf_destroy(void)
{
	struct cord *c = cord();
	struct ibuf *buf = (struct ibuf *)c->buf;
	if (buf == NULL)
		return;
	ibuf_destroy(buf);
	TRASH(buf);
	free(buf);
	c->buf = NULL;
}

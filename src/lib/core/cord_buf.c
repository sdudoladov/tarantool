/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "cord_buf.h"
#include "fiber.h"

#include "small/ibuf.h"

#define CORD_IBUF_START_CAPACITY 16384

struct ibuf *
cord_ibuf_take(void)
{
	struct cord *c = cord();
	struct ibuf *buf = (struct ibuf *)c->buf;
	if (buf != NULL) {
		ibuf_reset(buf);
		return buf;
	}

	buf = malloc(sizeof(*buf));
	if (buf == NULL)
		panic("Couldn't allocate thread buffer");
	ibuf_create(buf, &c->slabc, CORD_IBUF_START_CAPACITY);
	c->buf = (struct cord_buf *)buf;
	return buf;
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

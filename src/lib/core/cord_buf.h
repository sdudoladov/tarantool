/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct ibuf;

struct ibuf *
cord_ibuf_take(void);

void
cord_ibuf_put(struct ibuf *ibuf);

void
cord_buf_destroy(void);

#if defined(__cplusplus)
}
#endif /* defined(__cplusplus) */

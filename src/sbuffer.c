/*
 *   This file is part of nftlb, nftables load balancer.
 *
 *   Copyright (C) ZEVENET SL.
 *   Author: Laura Garcia <laura.garcia@zevenet.com>
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU Affero General Public License as
 *   published by the Free Software Foundation, either version 3 of the
 *   License, or any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU Affero General Public License for more details.
 *
 *   You should have received a copy of the GNU Affero General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sbuffer.h"
#include "tools.h"

int get_buf_size(struct sbuffer *buf)
{
	return buf->size;
}

char * get_buf_next(struct sbuffer *buf)
{
	return buf->data + buf->next;
}

int resize_buf(struct sbuffer *buf, int times)
{
	char *pbuf;
	int newsize;

	if (times == 0)
		return 0;

	newsize = buf->size + (times * EXTRA_SIZE) + 1;

	if (!buf->data)
		return 1;

	pbuf = (char *) realloc(buf->data, newsize);
	if (!pbuf)
		return 1;

	buf->data = pbuf;
	buf->size = newsize;
	return 0;
}

int create_buf(struct sbuffer *buf)
{
	buf->size = 0;
	buf->next = 0;

	buf->data = (char *) calloc(1, DEFAULT_BUFFER_SIZE);
	if (!buf->data) {
		return 1;
	}

	*buf->data = '\0';
	buf->size = DEFAULT_BUFFER_SIZE;
	return 0;
}

int isempty_buf(struct sbuffer *buf)
{
	return (buf->data[0] == 0);
}

char *get_buf_data(struct sbuffer *buf)
{
	return buf->data;
}

int clean_buf(struct sbuffer *buf)
{
	if (buf->data)
		free(buf->data);
	buf->size = 0;
	buf->next = 0;
	return 0;
}

int reset_buf(struct sbuffer *buf)
{
	buf->data[0] = 0;
	buf->next = 0;
	return 0;
}

int concat_buf_va(struct sbuffer *buf, int len, char *fmt, va_list args)
{
	int times = 0;
	char *pnext;

	if (buf->next + len >= buf->size)
		times = ((buf->next + len - buf->size) / EXTRA_SIZE) + 1;

	if (resize_buf(buf, times)) {
		tools_printlog(LOG_ERR, "Error resizing the buffer %d times from a size of %d!", times, buf->size);
		return 1;
	}

	pnext = get_buf_next(buf);
	vsnprintf(pnext, len + 1, fmt, args);
	buf->next += len;

	return 0;
}

int concat_buf(struct sbuffer *buf, char *fmt, ...)
{
	int len;
	va_list args;

	va_start(args, fmt);
	len = vsnprintf(0, 0, fmt, args);
	va_end(args);

	va_start(args, fmt);
	concat_buf_va(buf, len, fmt, args);
	va_end(args);

	return 0;
}

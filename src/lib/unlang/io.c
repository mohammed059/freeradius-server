/*
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

/**
 * $Id$
 *
 * @file unlang/io.c
 * @brief Shim I/O worker functions for running fake requests.
 *
 * @copyright 2006-2019 The FreeRADIUS server project
 */
RCSID("$Id$")

#include <freeradius-devel/io/listen.h>
#include "unlang_priv.h"

/** Run the interpreter after creating a subrequest.
 *
 * Just run some "unlang", but don't do anything else.
 *
 * This is a shim function added to 'fake' requests by the subrequest and parallel keywords.
 */
fr_io_final_t unlang_io_process_interpret(UNUSED void const *instance, REQUEST *request, fr_io_action_t action)
{
	rlm_rcode_t rcode;

	REQUEST_VERIFY(request);

	/*
	 *	Pass this through asynchronously to the module which
	 *	is waiting for something to happen.
	 */
	if (action != FR_IO_ACTION_RUN) {
		unlang_signal(request, (fr_state_signal_t) action);
		return FR_IO_DONE;
	}

	rcode = unlang_interpret_resume(request);

	if (request->master_state == REQUEST_STOP_PROCESSING) return FR_IO_DONE;

	if (rcode == RLM_MODULE_YIELD) return FR_IO_YIELD;

	/*
	 *	Don't bother setting request->reply->code.
	 */
	return FR_IO_DONE;
}

/** Allocate a child request based on the parent.
 *
 * @param[in] parent		spawning the child request.
 * @param[in] instruction	child starts executing.
 * @param[in] server_cs		the child is operating in.  If NULL the parent's server is used.
 * @param[in] namespace		the child request operates in. If NULL the parent's namespace is used.
 * @param[in] default_rcode	for the section the child is executing.
 * @param[in] do_next_sibling	Execute next sibling.
 * @param[in] detachable	Allow/disallow the child to be detached.
 * @return
 *      - The new child request.
 *	- NULL on error.
 */
REQUEST *unlang_io_child_alloc(REQUEST *parent, unlang_t *instruction,
			       CONF_SECTION *server_cs, fr_dict_t const *namespace,
			       rlm_rcode_t default_rcode,
			       bool do_next_sibling, bool detachable)
{
	REQUEST			*child;
	unlang_stack_t		*stack;

	if (!detachable) {
		child = request_alloc_fake(parent);
	} else {
		child = request_alloc_detachable(parent);
	}
	if (!child) return NULL;

	/*
	 *	Push the child, and set it's top frame to be true.
	 */
	stack = child->stack;
	child->log.unlang_indent = parent->log.unlang_indent;
	unlang_push(stack, instruction, default_rcode, do_next_sibling, UNLANG_SUB_FRAME);
	stack->frame[stack->depth].top_frame = true;

	/*
	 *	Initialize some basic information for the child.
	 *
	 *	Note that we do NOT initialize child->backlog, as the
	 *	child is never resumable... the parent is resumable.
	 */
	child->number = parent->number;
	child->el = parent->el;
	child->server_cs = server_cs;
	child->dict = namespace;

	/*
	 *	Initialize all of the async fields.
	 */
	child->async = talloc_zero(child, fr_async_t);

#define COPY_FIELD(_x) child->async->_x = parent->async->_x
	COPY_FIELD(listen);
	COPY_FIELD(recv_time);
	child->async->original_recv_time = &child->async->recv_time;
	child->async->fake = true;

	/*
	 *	Always set the "process" function to the local
	 *	bare-bones function which just runs on section of
	 *	"unlang", and doesn't send replies or anything else.
	 */
	child->async->process = unlang_io_process_interpret;

	/*
	 *	Note that we don't do time tracking on the child.
	 *	Instead, all of it is done in the context of the
	 *	parent.
	 */
	fr_dlist_init(&child->async->tracking.list, fr_time_tracking_t, list.entry);

	return child;
}

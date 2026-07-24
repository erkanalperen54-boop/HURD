## `ipc_mqueue.c` içerisinde ki kritik zafiyetler

```C
/*
 *	Routine:	ipc_mqueue_send
 *	Purpose:
 *		Send a message to a port.  The message holds a reference
 *		for the destination port in the msgh_remote_port field.
 *
 *		If unsuccessful, the caller still has possession of
 *		the message and must do something with it.  If successful,
 *		the message is queued, given to a receiver, destroyed,
 *		or handled directly by the kernel via mach_msg.
 *	Conditions:
 *		Nothing locked.
 *	Returns:
 *		MACH_MSG_SUCCESS	The message was accepted.
 *		MACH_SEND_TIMED_OUT	Caller still has message.
 *		MACH_SEND_INTERRUPTED	Caller still has message.
 */

mach_msg_return_t
ipc_mqueue_send(
	ipc_kmsg_t 		kmsg,
	mach_msg_option_t 	option,
	mach_msg_timeout_t 	time_out)
{
	ipc_port_t port;

	port = (ipc_port_t) kmsg->ikm_header.msgh_remote_port;
	assert(IP_VALID(port));

	ip_lock(port);

	if (port->ip_receiver == ipc_space_kernel) {
		ipc_kmsg_t reply;

		/*
		 *	We can check ip_receiver == ipc_space_kernel
		 *	before checking that the port is active because
		 *	ipc_port_dealloc_kernel clears ip_receiver
		 *	before destroying a kernel port.
		 */

		assert(ip_active(port));
		ip_unlock(port);

		reply = ipc_kobject_server(kmsg);
		if (reply != IKM_NULL)
			ipc_mqueue_send_always(reply);

		return MACH_MSG_SUCCESS;
	}

	for (;;) {
		ipc_thread_t self;

		/*
		 *	Can't deliver to a dead port.
		 *	However, we can pretend it got sent
		 *	and was then immediately destroyed.
		 */

		if (!ip_active(port)) {
			/*
			 *	We can't let ipc_kmsg_destroy deallocate
			 *	the port right, because we might end up
			 *	in an infinite loop trying to deliver
			 *	a send-once notification.
			 */

			ip_release(port);
			ip_check_unlock(port);
			kmsg->ikm_header.msgh_remote_port = MACH_PORT_NULL;
			ipc_kmsg_destroy(kmsg);
			return MACH_MSG_SUCCESS;
		}

		/*
		 *  Don't block if:
		 *	1) We're under the queue limit.
		 *	2) Caller used the MACH_SEND_ALWAYS internal option.
		 *	3) Message is sent to a send-once right.
		 */

		if ((port->ip_msgcount < port->ip_qlimit) ||
		    (option & MACH_SEND_ALWAYS) ||
		    (MACH_MSGH_BITS_REMOTE(kmsg->ikm_header.msgh_bits) ==
						MACH_MSG_TYPE_PORT_SEND_ONCE))
			break;

		/* must block waiting for queue to clear */

		self = current_thread();

		if (option & MACH_SEND_TIMEOUT) {
			if (time_out == 0) {
				ip_unlock(port);
				return MACH_SEND_TIMED_OUT;
			}

			thread_will_wait_with_timeout(self, time_out);
		} else
			thread_will_wait(self);

		ipc_thread_enqueue(&port->ip_blocked, self);
		self->ith_state = MACH_SEND_IN_PROGRESS;

	 	ip_unlock(port);
		counter(c_ipc_mqueue_send_block++);
		thread_block(thread_no_continuation);
		ip_lock(port);

		/* why did we wake up? */

		if (self->ith_state == MACH_MSG_SUCCESS)
			continue;
		assert(self->ith_state == MACH_SEND_IN_PROGRESS);

		/* take ourselves off blocked queue */

		ipc_thread_rmqueue(&port->ip_blocked, self);

		/*
		 *	Thread wakeup-reason field tells us why
		 *	the wait was interrupted.
		 */

		switch (self->ith_wait_result) {
		    case THREAD_INTERRUPTED:
			/* send was interrupted - give up */

			ip_unlock(port);
			return MACH_SEND_INTERRUPTED;

		    case THREAD_TIMED_OUT:
			/* timeout expired */

			assert(option & MACH_SEND_TIMEOUT);
			time_out = 0;
			break;

		    case THREAD_RESTART:
		    default:
#if MACH_ASSERT
			assert(!"ipc_mqueue_send");
#else
			panic("ipc_mqueue_send");
#endif
		}
	}

	if (kmsg->ikm_header.msgh_bits & MACH_MSGH_BITS_CIRCULAR) {
		ip_unlock(port);

		/* don't allow the creation of a circular loop */

		ipc_kmsg_destroy(kmsg);
		return MACH_MSG_SUCCESS;
	}

    {
	ipc_mqueue_t mqueue;
	ipc_pset_t pset;
	ipc_thread_t receiver;
	ipc_thread_queue_t receivers;

	port->ip_msgcount++;
	assert(port->ip_msgcount > 0);

	pset = port->ip_pset;
	if (pset == IPS_NULL)
		mqueue = &port->ip_messages;
	else
		mqueue = &pset->ips_messages;

	imq_lock(mqueue);
	receivers = &mqueue->imq_threads;

	/*
	 *	Can unlock the port now that the msg queue is locked
	 *	and we know the port is active.  While the msg queue
	 *	is locked, we have control of the kmsg, so the ref in
	 *	it for the port is still good.  If the msg queue is in
	 *	a set (dead or alive), then we're OK because the port
	 *	is still a member of the set and the set won't go away
	 *	until the port is taken out, which tries to lock the
	 *	set's msg queue to remove the port's msgs.
	 */

	ip_unlock(port);

	/* check for a receiver for the message */

	for (;;) {
		receiver = ipc_thread_queue_first(receivers);
		if (receiver == ITH_NULL) {
			/* no receivers; queue kmsg */

			ipc_kmsg_enqueue_macro(&mqueue->imq_messages, kmsg);
			imq_unlock(mqueue);
			break;
		}

		ipc_thread_rmqueue_first_macro(receivers, receiver);
		assert(ipc_kmsg_queue_empty(&mqueue->imq_messages));

		if (kmsg->ikm_header.msgh_size <= receiver->ith_msize) {
			/* got a successful receiver */

			receiver->ith_state = MACH_MSG_SUCCESS;
			receiver->ith_kmsg = kmsg;
			receiver->ith_seqno = port->ip_seqno++;
			imq_unlock(mqueue);

			thread_go(receiver);
			break;
		}

		receiver->ith_state = MACH_RCV_TOO_LARGE;
		receiver->ith_msize = kmsg->ikm_header.msgh_size;
		thread_go(receiver);
	}
    }

	current_task()->messages_sent++;

	return MACH_MSG_SUCCESS;
}
```


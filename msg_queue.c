/*
 * This code is provided solely for the personal and private use of students
 * taking the CSC369H course at the University of Toronto. Copying for purposes
 * other than this use is expressly prohibited. All forms of distribution of
 * this code, including but not limited to public repositories on GitHub,
 * GitLab, Bitbucket, or any other online platform, whether as given or with
 * any changes, are expressly prohibited.
 *
 * Authors: Alexey Khrabrov, Andrew Pelegris, Karen Reid
 *
 * All of the files in this directory and all subdirectories are:
 * Copyright (c) 2019, 2020 Karen Reid
 */

/**
 * CSC369 Assignment 2 - Message queue implementation.
 *
 * You may not use the pthread library directly. Instead you must use the
 * functions and types available in sync.h.
 */

#include <assert.h>
#include <errno.h>
#include <stdlib.h>

#include "errors.h"
#include "list.h"
#include "msg_queue.h"
#include "ring_buffer.h"

typedef struct wait_queue
{
	cond_t status;
	mutex_t lock;
	int curr_event;
	list_entry lst;
} wait_queue;

// Message queue implementation backend
typedef struct mq_backend
{
	// Ring buffer for storing the messages
	ring_buffer buffer;

	// Reference count
	size_t refs;

	// Number of handles open for reads
	size_t readers;
	// Number of handles open for writes
	size_t writers;

	// Set to true when all the reader handles have been closed. Starts false
	// when they haven't been opened yet.
	bool no_readers;
	// Set to true when all the writer handles have been closed. Starts false
	// when they haven't been opened yet.
	bool no_writers;

	//TODO: add necessary synchronization primitives, as well as data structures
	//      needed to implement the msg_queue_poll() functionality
	mutex_t mutex;
	cond_t empty;
	cond_t full;
	int curr;

	list_head the_list;

} mq_backend;

static int mq_init(mq_backend *mq, size_t capacity)
{
	if (ring_buffer_init(&mq->buffer, capacity) < 0)
	{
		return -1;
	}

	mq->refs = 0;

	mq->readers = 0;
	mq->writers = 0;

	mq->no_readers = false;
	mq->no_writers = false;

	//TODO: initialize remaining fields (synchronization primitives, etc.)
	mutex_init(&mq->mutex);
	cond_init(&mq->empty);
	cond_init(&mq->full);

	list_init(&mq->the_list);

	return 0;
}

static void mq_destroy(mq_backend *mq)
{
	assert(mq->refs == 0);
	assert(mq->readers == 0);
	assert(mq->writers == 0);

	ring_buffer_destroy(&mq->buffer);

	//TODO: cleanup remaining fields (synchronization primitives, etc.)
	mutex_destroy(&mq->mutex);
	cond_destroy(&mq->empty);
	cond_destroy(&mq->full);

	list_destroy(&mq->the_list);
}

#define ALL_FLAGS (MSG_QUEUE_READER | MSG_QUEUE_WRITER | MSG_QUEUE_NONBLOCK)

// Message queue handle is a combination of the pointer to the queue backend and
// the handle flags. The pointer is always aligned on 8 bytes - its 3 least
// significant bits are always 0. This allows us to store the flags within the
// same word-sized value as the pointer by ORing the pointer with the flag bits.

// Get queue backend pointer from the queue handle
static mq_backend *get_backend(msg_queue_t queue)
{
	mq_backend *mq = (mq_backend *)(queue & ~ALL_FLAGS);
	assert(mq);
	return mq;
}

// Get handle flags from the queue handle
static int get_flags(msg_queue_t queue)
{
	return (int)(queue & ALL_FLAGS);
}

// Create a queue handle for given backend pointer and handle flags
static msg_queue_t make_handle(mq_backend *mq, int flags)
{
	assert(((uintptr_t)mq & ALL_FLAGS) == 0);
	assert((flags & ~ALL_FLAGS) == 0);
	return (uintptr_t)mq | flags;
}

static msg_queue_t mq_open(mq_backend *mq, int flags)
{
	++mq->refs;

	if (flags & MSG_QUEUE_READER)
	{
		++mq->readers;
		mq->no_readers = false;
	}
	if (flags & MSG_QUEUE_WRITER)
	{
		++mq->writers;
		mq->no_writers = false;
	}

	return make_handle(mq, flags);
}

// Returns true if this was the last handle
static bool mq_close(mq_backend *mq, int flags)
{
	assert(mq->refs != 0);
	assert(mq->refs >= mq->readers);
	assert(mq->refs >= mq->writers);

	if ((flags & MSG_QUEUE_READER) && (--mq->readers == 0))
	{
		mq->no_readers = true;
	}
	if ((flags & MSG_QUEUE_WRITER) && (--mq->writers == 0))
	{
		mq->no_writers = true;
	}

	if (--mq->refs == 0)
	{
		assert(mq->readers == 0);
		assert(mq->writers == 0);
		return true;
	}
	return false;
}

msg_queue_t msg_queue_create(size_t capacity, int flags)
{
	if (flags & ~ALL_FLAGS)
	{
		errno = EINVAL;
		report_error("msg_queue_create");
		return MSG_QUEUE_NULL;
	}

	// Refuse to create a message queue without capacity for
	// at least one message (length + 1 byte of message data).
	if (capacity < (sizeof(size_t) + 1))
	{
		errno = EINVAL;
		report_error("msg_queue_create");
		return MSG_QUEUE_NULL;
	}

	mq_backend *mq = (mq_backend *)malloc(sizeof(mq_backend));
	if (!mq)
	{
		report_error("malloc");
		return MSG_QUEUE_NULL;
	}
	// Result of malloc() is always aligned on 8 bytes, allowing us to use the
	// 3 least significant bits of the handle to store the 3 bits of flags
	assert(((uintptr_t)mq & ALL_FLAGS) == 0);

	if (mq_init(mq, capacity) < 0)
	{
		// Preserve errno value that can be changed by free()
		int e = errno;
		free(mq);
		errno = e;
		return MSG_QUEUE_NULL;
	}

	return mq_open(mq, flags);
}

msg_queue_t msg_queue_open(msg_queue_t queue, int flags)
{
	if (!queue)
	{
		errno = EBADF;
		report_error("msg_queue_open");
		return MSG_QUEUE_NULL;
	}

	if (flags & ~ALL_FLAGS)
	{
		errno = EINVAL;
		report_error("msg_queue_open");
		return MSG_QUEUE_NULL;
	}

	mq_backend *mq = get_backend(queue);

	//TODO: add necessary synchronization
	mutex_lock(&mq->mutex);
	msg_queue_t new_handle = mq_open(mq, flags);
	mutex_unlock(&mq->mutex);

	return new_handle;
}

int msg_queue_close(msg_queue_t *queue)
{
	if (!queue || !*queue)
	{
		errno = EBADF;
		report_error("msg_queue_close");
		return -1;
	}

	mq_backend *mq = get_backend(*queue);

	//TODO: add necessary synchronization
	mutex_lock(&mq->mutex);
	if (mq_close(mq, get_flags(*queue)))
	{
		// Closed last handle; destroy the queue
		mutex_unlock(&mq->mutex);
		mq_destroy(mq);
		free(mq);
		*queue = MSG_QUEUE_NULL;
		return 0;
	}

	//TODO: if this is the last reader (or writer) handle, notify all the writer
	//      (or reader) threads currently blocked in msg_queue_write() (or
	//      msg_queue_read()) and msg_queue_poll() calls for this queue.
	if (mq->no_readers)
	{
		cond_broadcast(&(mq->full));
		mq->curr |= MQPOLL_WRITABLE | MQPOLL_NOREADERS;
	}
	if (mq->no_writers)
	{
		cond_broadcast(&(mq->empty));
		mq->curr |= MQPOLL_READABLE | MQPOLL_NOWRITERS;
	}
	*queue = MSG_QUEUE_NULL;
	mutex_unlock(&mq->mutex);
	return 0;
}

ssize_t msg_queue_read(msg_queue_t queue, void *buffer, size_t length)
{
	//TODO
	(void)queue;
	(void)buffer;
	(void)length;
	errno = ENOSYS;
	mq_backend *be = get_backend(queue);
	mutex_lock(&be->mutex);
	int curr_flag = get_flags(queue);
	// check is reader is enabled
	if (!(curr_flag & MSG_QUEUE_READER))
	{
		mutex_unlock(&be->mutex);
		errno = EBADF;
		report_error("msg_queue_read: queue is not a valid message queue handle open for reads.");
		return -1;
	}
	// check when nonblock is on, is there enough material to read
	else if (curr_flag & MSG_QUEUE_NONBLOCK)
	{
		if (!ring_buffer_used(&be->buffer))
		{
			mutex_unlock(&be->mutex);
			errno = EAGAIN;
			report_error("msg_queue_read:The queue handle is non-blocking and the read would block because there is no message in the queue to read.");
			return -1;
		}
	}
	size_t size;
	// wait till there's enough thing to read
	while (ring_buffer_used(&be->buffer) == 0)
	{
		if (be->no_writers)
		{
			mutex_unlock(&be->mutex);
			return 0;
		}
		cond_wait(&be->empty, &be->mutex);
	}

	// read the header (size) without take it out of the buffer
	ring_buffer_peek(&be->buffer, &size, sizeof(size_t));
	// check if length is enough
	if (length < size)
	{
		mutex_unlock(&be->mutex);
		errno = EMSGSIZE;
		report_error("msg_queue_read: The buffer is not large enough to hold the message.");
		return ~size;
	}
	// do the actual read
	ring_buffer_read(&be->buffer, &size, sizeof(size_t));
	ring_buffer_read(&be->buffer, buffer, size);
	cond_signal(&be->full);
	be->curr |= MQPOLL_WRITABLE;
	list_entry *curr_entry = NULL;
	wait_queue *dentry = NULL;
	list_for_each(curr_entry, &be->the_list)
	{
		dentry = container_of(curr_entry, wait_queue, lst);
		if (dentry->curr_event & be->curr)
		{
			mutex_lock(&dentry->lock);
			cond_signal(&dentry->status);
			mutex_unlock(&dentry->lock);
		}
	}
	mutex_unlock(&be->mutex);
	return size;
}

int msg_queue_write(msg_queue_t queue, const void *buffer, size_t length)
{
	//TODO
	(void)queue;
	(void)buffer;
	(void)length;
	errno = ENOSYS;
	mq_backend *be = get_backend(queue);
	mutex_lock(&be->mutex);
	int curr_flag = get_flags(queue);
	// check if length is 0
	if (length == 0)
	{
		mutex_unlock(&be->mutex);
		errno = EINVAL;
		report_error("msg_queue_write: Zero length message.");
		return -1;
	}
	// check if writer is enabled
	else if (!(curr_flag & MSG_QUEUE_WRITER))
	{
		mutex_unlock(&be->mutex);
		errno = EBADF;
		report_error("msg_queue_write: queue is not a valid message queue handle open for writes.");
		return -1;
	}
	// check if buffer size is large enough
	else if (be->buffer.size < length + sizeof(size_t))
	{
		mutex_unlock(&be->mutex);
		errno = EMSGSIZE;
		report_error("msg_queue_write: The capacity of the queue is not large enough for the message.");
		return -1;
	}
	// check when nonblock is on, is there enough space to write in
	else if (curr_flag & MSG_QUEUE_NONBLOCK)
	{
		if (ring_buffer_free(&be->buffer) < length + sizeof(size_t))
		{
			mutex_unlock(&be->mutex);
			errno = EAGAIN;
			report_error("msg_queue_write: The queue handle is non-blocking and the write would block because there is not enough space in the queue to write message.");
			return -1;
		}
	}
	// wait till space is enough for the whole sentence
	while (ring_buffer_free(&be->buffer) < length + sizeof(size_t))
	{
		if (be->no_readers)
		{
			mutex_unlock(&be->mutex);
			errno = EPIPE;
			report_error("msg_queue_write: All reader handles to the queue have been closed (broken pipe).");
			return -1;
		}
		cond_wait(&be->full, &be->mutex);
	}

	be->curr |= MQPOLL_READABLE;
	// do the write
	// head
	ring_buffer_write(&be->buffer, &length, sizeof(size_t));
	// actual material
	ring_buffer_write(&be->buffer, buffer, length);
	cond_signal(&be->empty);
	list_entry *curr_entry = NULL;
	wait_queue *dentry = NULL;
	list_for_each(curr_entry, &be->the_list)
	{
		dentry = container_of(curr_entry, wait_queue, lst);
		if (dentry->curr_event & be->curr)
		{
			mutex_lock(&dentry->lock);
			cond_signal(&dentry->status);
			mutex_unlock(&dentry->lock);
		}
	}
	mutex_unlock(&be->mutex);

	return 0;
}

#define ALL_EVENTS_FLAGS (MQPOLL_NOWRITERS | MQPOLL_NOREADERS | MQPOLL_READABLE | MQPOLL_WRITABLE)
int msg_queue_poll(msg_queue_pollfd *fds, size_t nfds)
{
	//TODO
	(void)fds;
	(void)nfds;
	errno = ENOSYS;

	if (nfds == 0)
	{
		errno = EINVAL;
		report_error("msg_queue_poll: No events are subscribed to");
		return -1;
	}

	unsigned int num_null = 0;
	for (unsigned int i = 0; i < nfds; i++)
	{
		if (fds[i].queue == MSG_QUEUE_NULL)
		{
			num_null++;
			fds[i].revents = 0;
			continue;
		}
		if (fds[i].events & !ALL_EVENTS_FLAGS)
		{
			errno = EINVAL;
			report_error("msg_queue_poll: events field in a pollfd entry is invalid");
			return -1;
		}
		if (((fds[i].events & MQPOLL_READABLE) && !(get_flags(fds[i].queue) & MSG_QUEUE_READER)) || ((fds[i].events & MQPOLL_WRITABLE) && !(get_flags(fds[i].queue) & MSG_QUEUE_WRITER)))
		{
			errno = EINVAL;
			report_error("msg_queue_poll: MQPOLL_READABLE requested for a non-reader queue handle or MQPOLL_WRITABLE requested for a non-writer queue handle");
			return -1;
		}
	}

	if (num_null == nfds)
	{
		errno = EINVAL;
		report_error("msg_queue_poll: No events are subscribed to");
		return -1;
	}

	cond_t status;
	mutex_t lock;
	cond_init(&status);
	mutex_init(&lock);
	wait_queue *queue = (wait_queue *)malloc(sizeof(wait_queue) * nfds);

	if (queue == NULL)
	{
		report_error("Malloc: not enough space for queue");
		return -1;
	}

	for (long unsigned int i = 0; i < nfds; i++)
	{
		list_entry_init(&queue[i].lst);
		mq_backend *mq = get_backend(fds[i].queue);
		mutex_lock(&mq->mutex);
		if (fds[i].queue != MSG_QUEUE_NULL)
		{
			queue[i].lock = lock;
			queue[i].status = status;
			queue[i].curr_event = fds[i].events;
			list_add_tail(&mq->the_list, &queue[i].lst);
		}
		mutex_unlock(&mq->mutex);
	}
	mutex_lock(&lock);
	int ready = 0;
	while ((size_t)ready < nfds)
	{
		for (long unsigned int i = 0; i < nfds; i++)
		{
			mq_backend *mq = get_backend(fds[i].queue);
			int curr_flag = get_flags(fds[i].queue);
			fds[i].revents = fds[i].events & mq->curr;
			if (curr_flag & MSG_QUEUE_WRITER)
			{
				fds[i].revents |= mq->curr & MQPOLL_NOWRITERS;
			}
			if (curr_flag & MSG_QUEUE_READER)
			{
				fds[i].revents |= mq->curr & MQPOLL_NOREADERS;
			}
			if (fds[i].revents)
			{
				ready++;
			}
		}
		if (ready)
		{
			break;
		}
		cond_wait(&status, &lock);
	}
	mutex_unlock(&lock);
	for (long unsigned int i = 0; i < nfds; i++)
	{
		if (fds[i].queue == MSG_QUEUE_NULL)
		{
			continue;
		}
		mq_backend *mq = get_backend(fds[i].queue);
		mutex_lock(&mq->mutex);
		list_init(&mq->the_list);
		mutex_unlock(&mq->mutex);
	}
	cond_destroy(&status);
	mutex_unlock(&lock);
	free(queue);
	return ready;
}

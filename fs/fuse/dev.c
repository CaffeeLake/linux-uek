/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2008  Miklos Szeredi <miklos@szeredi.hu>

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.
*/

#include "fuse_i.h"

#include <linux/init.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/uio.h>
#include <linux/miscdevice.h>
#include <linux/pagemap.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/pipe_fs_i.h>
#include <linux/swap.h>
#include <linux/splice.h>

MODULE_ALIAS_MISCDEV(FUSE_MINOR);
MODULE_ALIAS("devname:fuse");

static struct kmem_cache *fuse_req_cachep;

static inline int fuse_get_numa_id(struct fuse_conn *fc)
{
	if (fc->numa_on)
		return numa_node_id();
	else
		return 0;
}

static inline struct fuse_numa_node *fuse_get_numa_node(struct fuse_conn *fc)
{
	if (fc->numa_on)
		return fc->nn[numa_node_id()];
	else
		return fc->nn[0];
}

static struct fuse_conn *fuse_get_conn(struct file *file)
{
	/*
	 * Lockless access is OK, because file->private data is set
	 * once during mount and is valid until the file is released.
	 */
	return file->private_data;
}

static u64 fuse_get_unique(struct fuse_conn *fc)
{
	u64 ctr;

	spin_lock(&fc->lock);
	fc->reqctr++;
	/* zero is special */
	if (fc->reqctr == 0)
		fc->reqctr = 1;
	ctr = fc->reqctr;
	spin_unlock(&fc->lock);

	return ctr;
}

static void fuse_request_init(struct fuse_numa_node *nn, struct fuse_req *req)
{
	memset(req, 0, sizeof(*req));
	INIT_LIST_HEAD(&req->list);
	INIT_LIST_HEAD(&req->intr_entry);
	init_waitqueue_head(&req->waitq);
	atomic_set(&req->count, 1);
	req->numa_id = nn->numa_id;
	req->in.h.unique = fuse_get_unique(nn->fc);
}

struct fuse_req *fuse_request_alloc(struct fuse_conn *fc)
{
	struct fuse_req *req;
	struct fuse_numa_node *nn;

	nn = fuse_get_numa_node(fc);

	req = kmem_cache_alloc_node(fuse_req_cachep, GFP_KERNEL, nn->numa_id);
	if (req)
		fuse_request_init(nn, req);
	return req;
}
EXPORT_SYMBOL_GPL(fuse_request_alloc);

struct fuse_req *fuse_request_alloc_nofs(struct fuse_conn *fc)
{
	struct fuse_req *req;
	struct fuse_numa_node *nn;

	nn = fuse_get_numa_node(fc);
	req = kmem_cache_alloc_node(fuse_req_cachep, GFP_NOFS, nn->numa_id);
	if (req)
		fuse_request_init(nn, req);
	return req;
}

void fuse_request_free(struct fuse_req *req)
{
	kmem_cache_free(fuse_req_cachep, req);
}

static void block_sigs(sigset_t *oldset)
{
	sigset_t mask;

	siginitsetinv(&mask, sigmask(SIGKILL));
	sigprocmask(SIG_BLOCK, &mask, oldset);
}

static void restore_sigs(sigset_t *oldset)
{
	sigprocmask(SIG_SETMASK, oldset, NULL);
}

static void __fuse_get_request(struct fuse_req *req)
{
	atomic_inc(&req->count);
}

/* Must be called with > 1 refcount */
static void __fuse_put_request(struct fuse_req *req)
{
	BUG_ON(atomic_read(&req->count) < 2);
	atomic_dec(&req->count);
}

static void fuse_req_init_context(struct fuse_req *req)
{
	req->in.h.uid = from_kuid_munged(&init_user_ns, current_fsuid());
	req->in.h.gid = from_kgid_munged(&init_user_ns, current_fsgid());
	req->in.h.pid = current->pid;
}

struct fuse_req *fuse_get_req(struct fuse_conn *fc)
{
	struct fuse_numa_node *nn;
	struct fuse_req *req;
	sigset_t oldset;
	int intr;
	int err;

	nn = fuse_get_numa_node(fc);
	atomic_inc(&nn->num_waiting);
	block_sigs(&oldset);
	intr = wait_event_interruptible(nn->blocked_waitq, !nn->blocked);
	restore_sigs(&oldset);
	err = -EINTR;
	if (intr)
		goto out;

	err = -ENOTCONN;
	if (!fc->connected)
		goto out;

	req = fuse_request_alloc(fc);
	err = -ENOMEM;
	if (!req)
		goto out;

	fuse_req_init_context(req);
	req->waiting = 1;
	return req;

 out:
	atomic_dec(&nn->num_waiting);
	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(fuse_get_req);

/*
 * Return request in fuse_file->reserved_req.  However that may
 * currently be in use.  If that is the case, wait for it to become
 * available.
 */
static struct fuse_req *get_reserved_req(struct fuse_conn *fc,
					 struct file *file)
{
	struct fuse_req *req = NULL;
	struct fuse_file *ff = file->private_data;
	struct fuse_numa_node *nn;

	do {
		wait_event(fc->reserved_req_waitq, ff->reserved_req);
		spin_lock(&fc->lock);
		if (ff->reserved_req) {
			req = ff->reserved_req;
			ff->reserved_req = NULL;
		}
		spin_unlock(&fc->lock);
	} while (!req);

 	/*
 	 * it's better to initialize during get than put so we use local
 	 * fuse numa node
 	 */
 	nn = fuse_get_numa_node(fc);
 	fuse_request_init(nn, req);
 	req->stolen_file = file;
	return req;
}

/*
 * Put stolen request back into fuse_file->reserved_req
 */
static void put_reserved_req(struct fuse_conn *fc, struct fuse_req *req)
{
	struct file *file = req->stolen_file;
	struct fuse_file *ff = file->private_data;

	spin_lock(&fc->lock);
	BUG_ON(ff->reserved_req);
	ff->reserved_req = req;
	wake_up_all(&fc->reserved_req_waitq);
	spin_unlock(&fc->lock);
	fput(file);
}

/*
 * Gets a requests for a file operation, always succeeds
 *
 * This is used for sending the FLUSH request, which must get to
 * userspace, due to POSIX locks which may need to be unlocked.
 *
 * If allocation fails due to OOM, use the reserved request in
 * fuse_file.
 *
 * This is very unlikely to deadlock accidentally, since the
 * filesystem should not have it's own file open.  If deadlock is
 * intentional, it can still be broken by "aborting" the filesystem.
 */
struct fuse_req *fuse_get_req_nofail(struct fuse_conn *fc, struct file *file)
{
	struct fuse_req *req;
	struct fuse_numa_node *nn = fuse_get_numa_node(fc);

	atomic_inc(&nn->num_waiting);
	wait_event(nn->blocked_waitq, !nn->blocked);
	req = fuse_request_alloc(fc);
	if (!req) {
		req = get_reserved_req(fc, file);
		if (req->numa_id != nn->numa_id)
			req->numa_id = nn->numa_id;
	}

	fuse_req_init_context(req);
	req->waiting = 1;
	return req;
}

void fuse_put_request(struct fuse_conn *fc, struct fuse_req *req)
{
	struct fuse_numa_node *nn = fc->nn[req->numa_id];

	if (atomic_dec_and_test(&req->count)) {
		if (req->waiting)
			atomic_dec(&nn->num_waiting);

		if (req->stolen_file)
			put_reserved_req(fc, req);
		else
			fuse_request_free(req);
	}
}
EXPORT_SYMBOL_GPL(fuse_put_request);

static unsigned len_args(unsigned numargs, struct fuse_arg *args)
{
	unsigned nbytes = 0;
	unsigned i;

	for (i = 0; i < numargs; i++)
		nbytes += args[i].size;

	return nbytes;
}

static void queue_request(struct fuse_numa_node *nn, struct fuse_req *req)
{
	struct fuse_conn *fc = nn->fc;

	req->in.h.len = sizeof(struct fuse_in_header) +
		len_args(req->in.numargs, (struct fuse_arg *) req->in.args);
	list_add_tail(&req->list, &nn->pending);
	req->state = FUSE_REQ_PENDING;
	if (!req->waiting) {
		req->waiting = 1;
		atomic_inc(&nn->num_waiting);
	}
	wake_up(&nn->waitq);
	wake_up(&fc->poll_waitq);
	kill_fasync(&fc->fasync, SIGIO, POLL_IN);
}

void fuse_queue_forget(struct fuse_conn *fc, struct fuse_forget_link *forget,
		       u64 nodeid, u64 nlookup)
{
	struct fuse_numa_node *nn;
	int numa_id = fuse_get_numa_id(fc);

	nn = fc->nn[numa_id];
	forget->forget_one.nodeid = nodeid;
	forget->forget_one.nlookup = nlookup;

	spin_lock(&nn->lock);
	if (fc->connected) {
		nn->forget_list_tail->next = forget;
		nn->forget_list_tail = forget;
		wake_up(&nn->waitq);
		kill_fasync(&fc->fasync, SIGIO, POLL_IN);
	} else {
		kfree(forget);
	}
	spin_unlock(&nn->lock);
}

static void flush_bg_queue(struct fuse_numa_node *nn)
{
	while (nn->active_background < nn->max_background &&
	       !list_empty(&nn->bg_queue)) {
		struct fuse_req *req;

		req = list_entry(nn->bg_queue.next, struct fuse_req, list);
		list_del(&req->list);
		nn->active_background++;
		queue_request(nn, req);
	}
}

/*
 * This function is called when a request is finished.  Either a reply
 * has arrived or it was aborted (and not yet sent) or some error
 * occurred during communication with userspace, or the device file
 * was closed.  The requester thread is woken up (if still waiting),
 * the 'end' callback is called if given, else the reference to the
 * request is released
 *
 * Called with fc->lock, unlocks it
 */
static void request_end(struct fuse_numa_node *nn, struct fuse_req *req)
__releases(&nn->lock)
{
	void (*end) (struct fuse_conn *, struct fuse_req *) = req->end;
	struct fuse_conn *fc = nn->fc;

	req->end = NULL;
	list_del(&req->list);
	list_del(&req->intr_entry);
	req->state = FUSE_REQ_FINISHED;
	if (req->background) {
		if (nn->num_background == nn->max_background) {
			nn->blocked = 0;
			wake_up_all(&nn->blocked_waitq);
		}
		if (nn->num_background == nn->congestion_threshold &&
		    fc->connected && fc->bdi_initialized) {
			clear_bdi_congested(&fc->bdi, BLK_RW_SYNC);
			clear_bdi_congested(&fc->bdi, BLK_RW_ASYNC);
		}
		nn->num_background--;
		nn->active_background--;
		flush_bg_queue(nn);
	}
	spin_unlock(&nn->lock);
	wake_up(&req->waitq);
	if (end)
		end(fc, req);
	fuse_put_request(fc, req);
}

static void wait_answer_interruptible(struct fuse_numa_node *nn,
				      struct fuse_req *req)
__releases(nn->lock)
__acquires(nn->lock)
{
	if (signal_pending(current))
		return;

	spin_unlock(&nn->lock);
	wait_event_interruptible(req->waitq, req->state == FUSE_REQ_FINISHED);
	spin_lock(&nn->lock);
}

static void queue_interrupt(struct fuse_numa_node *nn, struct fuse_req *req)
{
	struct fuse_conn *fc = nn->fc;

	list_add_tail(&req->intr_entry, &nn->interrupts);
	wake_up(&nn->waitq);
	wake_up(&fc->poll_waitq);
	kill_fasync(&fc->fasync, SIGIO, POLL_IN);
}

static void request_wait_answer(struct fuse_numa_node *nn, struct fuse_req *req)
__releases(&nn->lock)
__acquires(&nn->lock)
{
	struct fuse_conn *fc = nn->fc;

	if (!fc->no_interrupt) {
		/* Any signal may interrupt this */
		wait_answer_interruptible(nn, req);

		if (req->aborted)
			goto aborted;
		if (req->state == FUSE_REQ_FINISHED)
			return;

		req->interrupted = 1;
		if (req->state == FUSE_REQ_SENT)
			queue_interrupt(nn, req);
	}

	if (!req->force) {
		sigset_t oldset;

		/* Only fatal signals may interrupt this */
		block_sigs(&oldset);
		wait_answer_interruptible(nn, req);
		restore_sigs(&oldset);

		if (req->aborted)
			goto aborted;
		if (req->state == FUSE_REQ_FINISHED)
			return;

		/* Request is not yet in userspace, bail out */
		if (req->state == FUSE_REQ_PENDING) {
			list_del(&req->list);
			__fuse_put_request(req);
			req->out.h.error = -EINTR;
			return;
		}
	}

	/*
	 * Either request is already in userspace, or it was forced.
	 * Wait it out.
	 */
	spin_unlock(&nn->lock);
	wait_event(req->waitq, req->state == FUSE_REQ_FINISHED);
	spin_lock(&nn->lock);

	if (!req->aborted)
		return;

 aborted:
	BUG_ON(req->state != FUSE_REQ_FINISHED);
	if (req->locked) {
		/* This is uninterruptible sleep, because data is
		   being copied to/from the buffers of req.  During
		   locked state, there mustn't be any filesystem
		   operation (e.g. page fault), since that could lead
		   to deadlock */
		spin_unlock(&nn->lock);
		wait_event(req->waitq, !req->locked);
		spin_lock(&nn->lock);
	}
}

void fuse_request_send(struct fuse_conn *fc, struct fuse_req *req)
{
	struct fuse_numa_node *nn = fc->nn[req->numa_id];

	req->isreply = 1;
	spin_lock(&nn->lock);
	if (!fc->connected)
		req->out.h.error = -ENOTCONN;
	else if (fc->conn_error)
		req->out.h.error = -ECONNREFUSED;
	else {
		queue_request(nn, req);
		/* acquire extra reference, since request is still needed
		   after request_end() */
		__fuse_get_request(req);

		request_wait_answer(nn, req);
	}
	spin_unlock(&nn->lock);
}
EXPORT_SYMBOL_GPL(fuse_request_send);

static void fuse_request_send_nowait_locked(struct fuse_numa_node *nn,
					    struct fuse_req *req)
{
	struct fuse_conn *fc = nn->fc;

	req->background = 1;
	nn->num_background++;
	if (nn->num_background == nn->max_background)
		nn->blocked = 1;
	if (nn->num_background == nn->congestion_threshold &&
	    fc->bdi_initialized) {
		set_bdi_congested(&fc->bdi, BLK_RW_SYNC);
		set_bdi_congested(&fc->bdi, BLK_RW_ASYNC);
	}
	list_add_tail(&req->list, &nn->bg_queue);
	flush_bg_queue(nn);
}

static void fuse_request_send_nowait(struct fuse_numa_node *nn,
				     struct fuse_req *req)
{
	spin_lock(&nn->lock);
	if (nn->fc->connected) {
		fuse_request_send_nowait_locked(nn, req);
		spin_unlock(&nn->lock);
	} else {
		req->out.h.error = -ENOTCONN;
		request_end(nn, req);
	}
}

void fuse_request_send_background(struct fuse_conn *fc, struct fuse_req *req)
{
	req->isreply = 1;
	fuse_request_send_nowait(fc->nn[req->numa_id], req);
}
EXPORT_SYMBOL_GPL(fuse_request_send_background);

static int fuse_request_send_notify_reply(struct fuse_conn *fc,
					  struct fuse_req *req, u64 unique)
{
	int err = -ENODEV;
	struct fuse_numa_node *nn = fc->nn[req->numa_id];

	req->isreply = 0;
	req->in.h.unique = unique;
	spin_lock(&nn->lock);
	if (fc->connected) {
		queue_request(nn, req);
		err = 0;
	}
	spin_unlock(&nn->lock);

	return err;
}

/*
 * Called under fc->lock
 *
 * fc->connected must have been checked previously
 */
void fuse_request_send_background_locked(struct fuse_conn *fc,
					 struct fuse_req *req)
{
	struct fuse_numa_node *nn = fc->nn[req->numa_id];

	spin_lock(&nn->lock);
	req->isreply = 1;
	fuse_request_send_nowait_locked(nn, req);
	spin_unlock(&nn->lock);
}

/*
 * Lock the request.  Up to the next unlock_request() there mustn't be
 * anything that could cause a page-fault.  If the request was already
 * aborted bail out.
 */
static int lock_request(struct fuse_numa_node *nn, struct fuse_req *req)
{
	int err = 0;
	if (req) {
		spin_lock(&nn->lock);
		if (req->aborted)
			err = -ENOENT;
		else
			req->locked = 1;
		spin_unlock(&nn->lock);
	}
	return err;
}

/*
 * Unlock request.  If it was aborted during being locked, the
 * requester thread is currently waiting for it to be unlocked, so
 * wake it up.
 */
static void unlock_request(struct fuse_numa_node *nn, struct fuse_req *req)
{
	if (req) {
		spin_lock(&nn->lock);
		req->locked = 0;
		if (req->aborted)
			wake_up(&req->waitq);
		spin_unlock(&nn->lock);
	}
}

struct fuse_copy_state {
	struct fuse_numa_node *nn;
	int write;
	struct fuse_req *req;
	const struct iovec *iov;
	struct pipe_buffer *pipebufs;
	struct pipe_buffer *currbuf;
	struct pipe_inode_info *pipe;
	unsigned long nr_segs;
	unsigned long seglen;
	unsigned long addr;
	struct page *pg;
	void *mapaddr;
	void *buf;
	unsigned len;
	unsigned move_pages:1;
};

static void fuse_copy_init(struct fuse_copy_state *cs,
			   struct fuse_numa_node *nn, int write,
			   const struct iovec *iov, unsigned long nr_segs)
{
	memset(cs, 0, sizeof(*cs));
	cs->nn = nn;
	cs->write = write;
	cs->iov = iov;
	cs->nr_segs = nr_segs;
}

/* Unmap and put previous page of userspace buffer */
static void fuse_copy_finish(struct fuse_copy_state *cs)
{
	if (cs->currbuf) {
		struct pipe_buffer *buf = cs->currbuf;

		if (!cs->write) {
			buf->ops->unmap(cs->pipe, buf, cs->mapaddr);
		} else {
			kunmap(buf->page);
			buf->len = PAGE_SIZE - cs->len;
		}
		cs->currbuf = NULL;
		cs->mapaddr = NULL;
	} else if (cs->mapaddr) {
		kunmap(cs->pg);
		if (cs->write) {
			flush_dcache_page(cs->pg);
			set_page_dirty_lock(cs->pg);
		}
		put_page(cs->pg);
		cs->mapaddr = NULL;
	}
}

/*
 * Get another pagefull of userspace buffer, and map it to kernel
 * address space, and lock request
 */
static int fuse_copy_fill(struct fuse_copy_state *cs)
{
	unsigned long offset;
	int err;

	unlock_request(cs->nn, cs->req);
	fuse_copy_finish(cs);
	if (cs->pipebufs) {
		struct pipe_buffer *buf = cs->pipebufs;

		if (!cs->write) {
			err = buf->ops->confirm(cs->pipe, buf);
			if (err)
				return err;

			BUG_ON(!cs->nr_segs);
			cs->currbuf = buf;
			cs->mapaddr = buf->ops->map(cs->pipe, buf, 0);
			cs->len = buf->len;
			cs->buf = cs->mapaddr + buf->offset;
			cs->pipebufs++;
			cs->nr_segs--;
		} else {
			struct page *page;

			if (cs->nr_segs == cs->pipe->buffers)
				return -EIO;

			page = alloc_page(GFP_HIGHUSER);
			if (!page)
				return -ENOMEM;

			buf->page = page;
			buf->offset = 0;
			buf->len = 0;

			cs->currbuf = buf;
			cs->mapaddr = kmap(page);
			cs->buf = cs->mapaddr;
			cs->len = PAGE_SIZE;
			cs->pipebufs++;
			cs->nr_segs++;
		}
	} else {
		if (!cs->seglen) {
			BUG_ON(!cs->nr_segs);
			cs->seglen = cs->iov[0].iov_len;
			cs->addr = (unsigned long) cs->iov[0].iov_base;
			cs->iov++;
			cs->nr_segs--;
		}
		err = get_user_pages_fast(cs->addr, 1, cs->write, &cs->pg);
		if (err < 0)
			return err;
		BUG_ON(err != 1);
		offset = cs->addr % PAGE_SIZE;
		cs->mapaddr = kmap(cs->pg);
		cs->buf = cs->mapaddr + offset;
		cs->len = min(PAGE_SIZE - offset, cs->seglen);
		cs->seglen -= cs->len;
		cs->addr += cs->len;
	}

	return lock_request(cs->nn, cs->req);
}

/* Do as much copy to/from userspace buffer as we can */
static int fuse_copy_do(struct fuse_copy_state *cs, void **val, unsigned *size)
{
	unsigned ncpy = min(*size, cs->len);
	if (val) {
		if (cs->write)
			memcpy(cs->buf, *val, ncpy);
		else
			memcpy(*val, cs->buf, ncpy);
		*val += ncpy;
	}
	*size -= ncpy;
	cs->len -= ncpy;
	cs->buf += ncpy;
	return ncpy;
}

static int fuse_check_page(struct page *page)
{
	if (page_mapcount(page) ||
	    page->mapping != NULL ||
	    page_count(page) != 1 ||
	    (page->flags & PAGE_FLAGS_CHECK_AT_PREP &
	     ~(1 << PG_locked |
	       1 << PG_referenced |
	       1 << PG_uptodate |
	       1 << PG_lru |
	       1 << PG_active |
	       1 << PG_reclaim))) {
		printk(KERN_WARNING "fuse: trying to steal weird page\n");
		printk(KERN_WARNING "  page=%p index=%li flags=%08lx, count=%i, mapcount=%i, mapping=%p\n", page, page->index, page->flags, page_count(page), page_mapcount(page), page->mapping);
		return 1;
	}
	return 0;
}

static int fuse_try_move_page(struct fuse_copy_state *cs, struct page **pagep)
{
	int err;
	struct page *oldpage = *pagep;
	struct page *newpage;
	struct pipe_buffer *buf = cs->pipebufs;

	unlock_request(cs->nn, cs->req);
	fuse_copy_finish(cs);

	err = buf->ops->confirm(cs->pipe, buf);
	if (err)
		return err;

	BUG_ON(!cs->nr_segs);
	cs->currbuf = buf;
	cs->len = buf->len;
	cs->pipebufs++;
	cs->nr_segs--;

	if (cs->len != PAGE_SIZE)
		goto out_fallback;

	if (buf->ops->steal(cs->pipe, buf) != 0)
		goto out_fallback;

	newpage = buf->page;

	if (!PageUptodate(newpage))
		SetPageUptodate(newpage);

	ClearPageMappedToDisk(newpage);

	if (fuse_check_page(newpage) != 0)
		goto out_fallback_unlock;

	/*
	 * This is a new and locked page, it shouldn't be mapped or
	 * have any special flags on it
	 */
	if (WARN_ON(page_mapped(oldpage)))
		goto out_fallback_unlock;
	if (WARN_ON(page_has_private(oldpage)))
		goto out_fallback_unlock;
	if (WARN_ON(PageDirty(oldpage) || PageWriteback(oldpage)))
		goto out_fallback_unlock;
	if (WARN_ON(PageMlocked(oldpage)))
		goto out_fallback_unlock;

	err = replace_page_cache_page(oldpage, newpage, GFP_KERNEL);
	if (err) {
		unlock_page(newpage);
		return err;
	}

	page_cache_get(newpage);

	if (!(buf->flags & PIPE_BUF_FLAG_LRU))
		lru_cache_add_file(newpage);

	err = 0;
	spin_lock(&cs->nn->lock);
	if (cs->req->aborted)
		err = -ENOENT;
	else
		*pagep = newpage;
	spin_unlock(&cs->nn->lock);

	if (err) {
		unlock_page(newpage);
		page_cache_release(newpage);
		return err;
	}

	unlock_page(oldpage);
	page_cache_release(oldpage);
	cs->len = 0;

	return 0;

out_fallback_unlock:
	unlock_page(newpage);
out_fallback:
	cs->mapaddr = buf->ops->map(cs->pipe, buf, 1);
	cs->buf = cs->mapaddr + buf->offset;

	err = lock_request(cs->nn, cs->req);
	if (err)
		return err;

	return 1;
}

static int fuse_ref_page(struct fuse_copy_state *cs, struct page *page,
			 unsigned offset, unsigned count)
{
	struct pipe_buffer *buf;

	if (cs->nr_segs == cs->pipe->buffers)
		return -EIO;

	unlock_request(cs->nn, cs->req);
	fuse_copy_finish(cs);

	buf = cs->pipebufs;
	page_cache_get(page);
	buf->page = page;
	buf->offset = offset;
	buf->len = count;

	cs->pipebufs++;
	cs->nr_segs++;
	cs->len = 0;

	return 0;
}

/*
 * Copy a page in the request to/from the userspace buffer.  Must be
 * done atomically
 */
static int fuse_copy_page(struct fuse_copy_state *cs, struct page **pagep,
			  unsigned offset, unsigned count, int zeroing)
{
	int err;
	struct page *page = *pagep;

	if (page && zeroing && count < PAGE_SIZE)
		clear_highpage(page);

	while (count) {
		if (cs->write && cs->pipebufs && page) {
			return fuse_ref_page(cs, page, offset, count);
		} else if (!cs->len) {
			if (cs->move_pages && page &&
			    offset == 0 && count == PAGE_SIZE) {
				err = fuse_try_move_page(cs, pagep);
				if (err <= 0)
					return err;
			} else {
				err = fuse_copy_fill(cs);
				if (err)
					return err;
			}
		}
		if (page) {
			void *mapaddr = kmap_atomic(page);
			void *buf = mapaddr + offset;
			offset += fuse_copy_do(cs, &buf, &count);
			kunmap_atomic(mapaddr);
		} else
			offset += fuse_copy_do(cs, NULL, &count);
	}
	if (page && !cs->write)
		flush_dcache_page(page);
	return 0;
}

/* Copy pages in the request to/from userspace buffer */
static int fuse_copy_pages(struct fuse_copy_state *cs, unsigned nbytes,
			   int zeroing)
{
	unsigned i;
	struct fuse_req *req = cs->req;
	unsigned offset = req->page_offset;
	unsigned count = min(nbytes, (unsigned) PAGE_SIZE - offset);

	for (i = 0; i < req->num_pages && (nbytes || zeroing); i++) {
		int err;

		err = fuse_copy_page(cs, &req->pages[i], offset, count,
				     zeroing);
		if (err)
			return err;

		nbytes -= count;
		count = min(nbytes, (unsigned) PAGE_SIZE);
		offset = 0;
	}
	return 0;
}

/* Copy a single argument in the request to/from userspace buffer */
static int fuse_copy_one(struct fuse_copy_state *cs, void *val, unsigned size)
{
	while (size) {
		if (!cs->len) {
			int err = fuse_copy_fill(cs);
			if (err)
				return err;
		}
		fuse_copy_do(cs, &val, &size);
	}
	return 0;
}

/* Copy request arguments to/from userspace buffer */
static int fuse_copy_args(struct fuse_copy_state *cs, unsigned numargs,
			  unsigned argpages, struct fuse_arg *args,
			  int zeroing)
{
	int err = 0;
	unsigned i;

	for (i = 0; !err && i < numargs; i++)  {
		struct fuse_arg *arg = &args[i];
		if (i == numargs - 1 && argpages)
			err = fuse_copy_pages(cs, arg->size, zeroing);
		else
			err = fuse_copy_one(cs, arg->value, arg->size);
	}
	return err;
}

static int forget_pending(struct fuse_numa_node *nn)
{
	return nn->forget_list_head.next != NULL;
}

static int request_pending(struct fuse_numa_node *nn)
{
	return !list_empty(&nn->pending) || !list_empty(&nn->interrupts) ||
		forget_pending(nn);
}

/* Wait until a request is available on the pending list */
static void request_wait(struct fuse_numa_node *nn)
__releases(&nn->lock)
__acquires(&nn->lock)
{
	struct fuse_conn *fc = nn->fc;

	DECLARE_WAITQUEUE(wait, current);

	add_wait_queue_exclusive(&nn->waitq, &wait);
	while (fc->connected && !request_pending(nn)) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (signal_pending(current))
			break;

		spin_unlock(&nn->lock);
		schedule();
		spin_lock(&nn->lock);
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&nn->waitq, &wait);
}

/*
 * Transfer an interrupt request to userspace
 *
 * Unlike other requests this is assembled on demand, without a need
 * to allocate a separate fuse_req structure.
 *
 * Called with fc->lock held, releases it
 */
static int fuse_read_interrupt(struct fuse_numa_node *nn,
			       struct fuse_copy_state *cs,
			       size_t nbytes, struct fuse_req *req)
__releases(nn->lock)
{
	struct fuse_in_header ih;
	struct fuse_interrupt_in arg;
	struct fuse_conn *fc = nn->fc;
	unsigned reqsize = sizeof(ih) + sizeof(arg);
	int err;
	u64 unique,req_orig_id;

	list_del_init(&req->intr_entry);
	req_orig_id = req->in.h.unique;
	spin_unlock(&nn->lock);
	unique = fuse_get_unique(fc);
	spin_lock(&nn->lock);
	if (req_orig_id != req->in.h.unique) {
		spin_unlock(&nn->lock);
		return -ENOENT;
	}
	req->intr_unique = unique;
	spin_unlock(&nn->lock);
	memset(&ih, 0, sizeof(ih));
	memset(&arg, 0, sizeof(arg));
	ih.len = reqsize;
	ih.opcode = FUSE_INTERRUPT;
	ih.unique = req->intr_unique;
	arg.unique = req->in.h.unique;

	if (nbytes < reqsize)
		return -EINVAL;

	err = fuse_copy_one(cs, &ih, sizeof(ih));
	if (!err)
		err = fuse_copy_one(cs, &arg, sizeof(arg));
	fuse_copy_finish(cs);

	return err ? err : reqsize;
}

static struct fuse_forget_link *dequeue_forget(struct fuse_numa_node *nn,
					       unsigned max,
					       unsigned *countp)
{
	struct fuse_forget_link *head = nn->forget_list_head.next;
	struct fuse_forget_link **newhead = &head;
	unsigned count;

	for (count = 0; *newhead != NULL && count < max; count++)
		newhead = &(*newhead)->next;

	nn->forget_list_head.next = *newhead;
	*newhead = NULL;
	if (nn->forget_list_head.next == NULL)
		nn->forget_list_tail = &nn->forget_list_head;

	if (countp != NULL)
		*countp = count;

	return head;
}

static int fuse_read_single_forget(struct fuse_numa_node *nn,
				   struct fuse_copy_state *cs,
				   size_t nbytes)
__releases(nn->lock)
{
	int err;
	struct fuse_forget_link *forget = dequeue_forget(nn, 1, NULL);
	struct fuse_forget_in arg = {
		.nlookup = forget->forget_one.nlookup,
	};
	struct fuse_in_header ih = {
		.opcode = FUSE_FORGET,
		.nodeid = forget->forget_one.nodeid,
		.unique = fuse_get_unique(nn->fc),
		.len = sizeof(ih) + sizeof(arg),
	};

	spin_unlock(&nn->lock);
	kfree(forget);
	if (nbytes < ih.len)
		return -EINVAL;

	err = fuse_copy_one(cs, &ih, sizeof(ih));
	if (!err)
		err = fuse_copy_one(cs, &arg, sizeof(arg));
	fuse_copy_finish(cs);

	if (err)
		return err;

	return ih.len;
}

static int fuse_read_batch_forget(struct fuse_numa_node *nn,
				  struct fuse_copy_state *cs, size_t nbytes)
__releases(nn->lock)
{
	int err;
	unsigned max_forgets;
	unsigned count;
	struct fuse_forget_link *head;
	struct fuse_batch_forget_in arg = { .count = 0 };
	struct fuse_in_header ih = {
		.opcode = FUSE_BATCH_FORGET,
		.unique = fuse_get_unique(nn->fc),
		.len = sizeof(ih) + sizeof(arg),
	};

	if (nbytes < ih.len) {
		spin_unlock(&nn->lock);
		return -EINVAL;
	}

	max_forgets = (nbytes - ih.len) / sizeof(struct fuse_forget_one);
	head = dequeue_forget(nn, max_forgets, &count);
	spin_unlock(&nn->lock);

	arg.count = count;
	ih.len += count * sizeof(struct fuse_forget_one);
	err = fuse_copy_one(cs, &ih, sizeof(ih));
	if (!err)
		err = fuse_copy_one(cs, &arg, sizeof(arg));

	while (head) {
		struct fuse_forget_link *forget = head;

		if (!err) {
			err = fuse_copy_one(cs, &forget->forget_one,
					    sizeof(forget->forget_one));
		}
		head = forget->next;
		kfree(forget);
	}

	fuse_copy_finish(cs);

	if (err)
		return err;

	return ih.len;
}

static int fuse_read_forget(struct fuse_numa_node *nn,
			    struct fuse_copy_state *cs, size_t nbytes)
__releases(nn->lock)
{
	if (nn->fc->minor < 16 || nn->forget_list_head.next->next == NULL)
		return fuse_read_single_forget(nn, cs, nbytes);
	else
		return fuse_read_batch_forget(nn, cs, nbytes);
}

/*
 * Read a single request into the userspace filesystem's buffer.  This
 * function waits until a request is available, then removes it from
 * the pending list and copies request data to userspace buffer.  If
 * no reply is needed (FORGET) or request has been aborted or there
 * was an error during the copying then it's finished by calling
 * request_end().  Otherwise add it to the processing list, and set
 * the 'sent' flag.
 */
static ssize_t fuse_dev_do_read(struct fuse_numa_node *nn, struct file *file,
				struct fuse_copy_state *cs, size_t nbytes)
{
	int err;
	struct fuse_req *req;
	struct fuse_in *in;
	unsigned reqsize;
	struct fuse_conn *fc = nn->fc;

 restart:
	spin_lock(&nn->lock);
	err = -EAGAIN;
	if ((file->f_flags & O_NONBLOCK) && fc->connected &&
	    !request_pending(nn))
		goto err_unlock;

	request_wait(nn);
	err = -ENODEV;
	if (!fc->connected)
		goto err_unlock;
	err = -ERESTARTSYS;
	if (!request_pending(nn))
		goto err_unlock;

	if (!list_empty(&nn->interrupts)) {
		req = list_entry(nn->interrupts.next, struct fuse_req,
				 intr_entry);
		err = fuse_read_interrupt(nn, cs, nbytes, req);
		if (err == -ENOENT)
		       goto restart;
		return err;
	}

	if (forget_pending(nn)) {
		if (list_empty(&nn->pending) || nn->forget_batch-- > 0)
			return fuse_read_forget(nn, cs, nbytes);

		if (nn->forget_batch <= -8)
			nn->forget_batch = 16;
	}

	req = list_entry(nn->pending.next, struct fuse_req, list);
	req->state = FUSE_REQ_READING;
	list_move(&req->list, &nn->io);

	in = &req->in;
	reqsize = in->h.len;
	/* If request is too large, reply with an error and restart the read */
	if (nbytes < reqsize) {
		req->out.h.error = -EIO;
		/* SETXATTR is special, since it may contain too large data */
		if (in->h.opcode == FUSE_SETXATTR)
			req->out.h.error = -E2BIG;
		request_end(nn, req);
		goto restart;
	}
	spin_unlock(&nn->lock);
	cs->req = req;
	err = fuse_copy_one(cs, &in->h, sizeof(in->h));
	if (!err)
		err = fuse_copy_args(cs, in->numargs, in->argpages,
				     (struct fuse_arg *) in->args, 0);
	fuse_copy_finish(cs);
	spin_lock(&nn->lock);
	req->locked = 0;
	if (req->aborted) {
		request_end(nn, req);
		return -ENODEV;
	}
	if (err) {
		req->out.h.error = -EIO;
		request_end(nn, req);
		return err;
	}
	if (!req->isreply)
		request_end(nn, req);
	else {
		req->state = FUSE_REQ_SENT;
		list_move_tail(&req->list, &nn->processing);
		if (req->interrupted)
			queue_interrupt(nn, req);
		spin_unlock(&nn->lock);
	}
	return reqsize;

 err_unlock:
	spin_unlock(&nn->lock);
	return err;
}

static ssize_t fuse_dev_read(struct kiocb *iocb, const struct iovec *iov,
			      unsigned long nr_segs, loff_t pos)
{
	struct fuse_copy_state cs;
	struct file *file = iocb->ki_filp;
	struct fuse_conn *fc = fuse_get_conn(file);
	struct fuse_numa_node *nn;
	if (!fc)
		return -EPERM;

	nn = fuse_get_numa_node(fc);
	fuse_copy_init(&cs, nn, 1, iov, nr_segs);

	return fuse_dev_do_read(nn, file, &cs, iov_length(iov, nr_segs));
}

static ssize_t fuse_dev_splice_read(struct file *in, loff_t *ppos,
				    struct pipe_inode_info *pipe,
				    size_t len, unsigned int flags)
{
	int ret;
	int page_nr = 0;
	int do_wakeup = 0;
	struct pipe_buffer *bufs;
	struct fuse_copy_state cs;
	struct fuse_conn *fc = fuse_get_conn(in);
	struct fuse_numa_node *nn;
	if (!fc)
		return -EPERM;

	nn = fuse_get_numa_node(fc);
	bufs = kmalloc(pipe->buffers * sizeof(struct pipe_buffer), GFP_KERNEL);
	if (!bufs)
		return -ENOMEM;

	fuse_copy_init(&cs, nn, 1, NULL, 0);
	cs.pipebufs = bufs;
	cs.pipe = pipe;
	ret = fuse_dev_do_read(nn, in, &cs, len);
	if (ret < 0)
		goto out;

	ret = 0;
	pipe_lock(pipe);

	if (!pipe->readers) {
		send_sig(SIGPIPE, current, 0);
		if (!ret)
			ret = -EPIPE;
		goto out_unlock;
	}

	if (pipe->nrbufs + cs.nr_segs > pipe->buffers) {
		ret = -EIO;
		goto out_unlock;
	}

	while (page_nr < cs.nr_segs) {
		int newbuf = (pipe->curbuf + pipe->nrbufs) & (pipe->buffers - 1);
		struct pipe_buffer *buf = pipe->bufs + newbuf;

		buf->page = bufs[page_nr].page;
		buf->offset = bufs[page_nr].offset;
		buf->len = bufs[page_nr].len;
		/*
		 * Need to be careful about this.  Having buf->ops in module
		 * code can Oops if the buffer persists after module unload.
		 */
		buf->ops = &nosteal_pipe_buf_ops;

		pipe->nrbufs++;
		page_nr++;
		ret += buf->len;

		if (pipe->inode)
			do_wakeup = 1;
	}

out_unlock:
	pipe_unlock(pipe);

	if (do_wakeup) {
		smp_mb();
		if (waitqueue_active(&pipe->wait))
			wake_up_interruptible(&pipe->wait);
		kill_fasync(&pipe->fasync_readers, SIGIO, POLL_IN);
	}

out:
	for (; page_nr < cs.nr_segs; page_nr++)
		page_cache_release(bufs[page_nr].page);

	kfree(bufs);
	return ret;
}

static int fuse_notify_poll(struct fuse_conn *fc, unsigned int size,
			    struct fuse_copy_state *cs)
{
	struct fuse_notify_poll_wakeup_out outarg;
	int err = -EINVAL;

	if (size != sizeof(outarg))
		goto err;

	err = fuse_copy_one(cs, &outarg, sizeof(outarg));
	if (err)
		goto err;

	fuse_copy_finish(cs);
	return fuse_notify_poll_wakeup(fc, &outarg);

err:
	fuse_copy_finish(cs);
	return err;
}

static int fuse_notify_inval_inode(struct fuse_conn *fc, unsigned int size,
				   struct fuse_copy_state *cs)
{
	struct fuse_notify_inval_inode_out outarg;
	int err = -EINVAL;

	if (size != sizeof(outarg))
		goto err;

	err = fuse_copy_one(cs, &outarg, sizeof(outarg));
	if (err)
		goto err;
	fuse_copy_finish(cs);

	down_read(&fc->killsb);
	err = -ENOENT;
	if (fc->sb) {
		err = fuse_reverse_inval_inode(fc->sb, outarg.ino,
					       outarg.off, outarg.len);
	}
	up_read(&fc->killsb);
	return err;

err:
	fuse_copy_finish(cs);
	return err;
}

static int fuse_notify_inval_entry(struct fuse_conn *fc, unsigned int size,
				   struct fuse_copy_state *cs)
{
	struct fuse_notify_inval_entry_out outarg;
	int err = -ENOMEM;
	char *buf;
	struct qstr name;

	buf = kzalloc(FUSE_NAME_MAX + 1, GFP_KERNEL);
	if (!buf)
		goto err;

	err = -EINVAL;
	if (size < sizeof(outarg))
		goto err;

	err = fuse_copy_one(cs, &outarg, sizeof(outarg));
	if (err)
		goto err;

	err = -ENAMETOOLONG;
	if (outarg.namelen > FUSE_NAME_MAX)
		goto err;

	err = -EINVAL;
	if (size != sizeof(outarg) + outarg.namelen + 1)
		goto err;

	name.name = buf;
	name.len = outarg.namelen;
	err = fuse_copy_one(cs, buf, outarg.namelen + 1);
	if (err)
		goto err;
	fuse_copy_finish(cs);
	buf[outarg.namelen] = 0;
	name.hash = full_name_hash(name.name, name.len);

	down_read(&fc->killsb);
	err = -ENOENT;
	if (fc->sb)
		err = fuse_reverse_inval_entry(fc->sb, outarg.parent, 0, &name);
	up_read(&fc->killsb);
	kfree(buf);
	return err;

err:
	kfree(buf);
	fuse_copy_finish(cs);
	return err;
}

static int fuse_notify_delete(struct fuse_conn *fc, unsigned int size,
			      struct fuse_copy_state *cs)
{
	struct fuse_notify_delete_out outarg;
	int err = -ENOMEM;
	char *buf;
	struct qstr name;

	buf = kzalloc(FUSE_NAME_MAX + 1, GFP_KERNEL);
	if (!buf)
		goto err;

	err = -EINVAL;
	if (size < sizeof(outarg))
		goto err;

	err = fuse_copy_one(cs, &outarg, sizeof(outarg));
	if (err)
		goto err;

	err = -ENAMETOOLONG;
	if (outarg.namelen > FUSE_NAME_MAX)
		goto err;

	err = -EINVAL;
	if (size != sizeof(outarg) + outarg.namelen + 1)
		goto err;

	name.name = buf;
	name.len = outarg.namelen;
	err = fuse_copy_one(cs, buf, outarg.namelen + 1);
	if (err)
		goto err;
	fuse_copy_finish(cs);
	buf[outarg.namelen] = 0;
	name.hash = full_name_hash(name.name, name.len);

	down_read(&fc->killsb);
	err = -ENOENT;
	if (fc->sb)
		err = fuse_reverse_inval_entry(fc->sb, outarg.parent,
					       outarg.child, &name);
	up_read(&fc->killsb);
	kfree(buf);
	return err;

err:
	kfree(buf);
	fuse_copy_finish(cs);
	return err;
}

static int fuse_notify_store(struct fuse_conn *fc, unsigned int size,
			     struct fuse_copy_state *cs)
{
	struct fuse_notify_store_out outarg;
	struct inode *inode;
	struct address_space *mapping;
	u64 nodeid;
	int err;
	pgoff_t index;
	unsigned int offset;
	unsigned int num;
	loff_t file_size;
	loff_t end;

	err = -EINVAL;
	if (size < sizeof(outarg))
		goto out_finish;

	err = fuse_copy_one(cs, &outarg, sizeof(outarg));
	if (err)
		goto out_finish;

	err = -EINVAL;
	if (size - sizeof(outarg) != outarg.size)
		goto out_finish;

	nodeid = outarg.nodeid;

	down_read(&fc->killsb);

	err = -ENOENT;
	if (!fc->sb)
		goto out_up_killsb;

	inode = ilookup5(fc->sb, nodeid, fuse_inode_eq, &nodeid);
	if (!inode)
		goto out_up_killsb;

	mapping = inode->i_mapping;
	index = outarg.offset >> PAGE_CACHE_SHIFT;
	offset = outarg.offset & ~PAGE_CACHE_MASK;
	file_size = i_size_read(inode);
	end = outarg.offset + outarg.size;
	if (end > file_size) {
		file_size = end;
		fuse_write_update_size(inode, file_size);
	}

	num = outarg.size;
	while (num) {
		struct page *page;
		unsigned int this_num;

		err = -ENOMEM;
		page = find_or_create_page(mapping, index,
					   mapping_gfp_mask(mapping));
		if (!page)
			goto out_iput;

		this_num = min_t(unsigned, num, PAGE_CACHE_SIZE - offset);
		err = fuse_copy_page(cs, &page, offset, this_num, 0);
		if (!err && offset == 0 && (num != 0 || file_size == end))
			SetPageUptodate(page);
		unlock_page(page);
		page_cache_release(page);

		if (err)
			goto out_iput;

		num -= this_num;
		offset = 0;
		index++;
	}

	err = 0;

out_iput:
	iput(inode);
out_up_killsb:
	up_read(&fc->killsb);
out_finish:
	fuse_copy_finish(cs);
	return err;
}

static void fuse_retrieve_end(struct fuse_conn *fc, struct fuse_req *req)
{
	release_pages(req->pages, req->num_pages, 0);
}

static int fuse_retrieve(struct fuse_conn *fc, struct inode *inode,
			 struct fuse_notify_retrieve_out *outarg)
{
	int err;
	struct address_space *mapping = inode->i_mapping;
	struct fuse_req *req;
	pgoff_t index;
	loff_t file_size;
	unsigned int num;
	unsigned int offset;
	size_t total_len = 0;

	req = fuse_get_req(fc);
	if (IS_ERR(req))
		return PTR_ERR(req);

	offset = outarg->offset & ~PAGE_CACHE_MASK;

	req->in.h.opcode = FUSE_NOTIFY_REPLY;
	req->in.h.nodeid = outarg->nodeid;
	req->in.numargs = 2;
	req->in.argpages = 1;
	req->page_offset = offset;
	req->end = fuse_retrieve_end;

	index = outarg->offset >> PAGE_CACHE_SHIFT;
	file_size = i_size_read(inode);
	num = outarg->size;
	if (outarg->offset > file_size)
		num = 0;
	else if (outarg->offset + num > file_size)
		num = file_size - outarg->offset;

	while (num && req->num_pages < FUSE_MAX_PAGES_PER_REQ) {
		struct page *page;
		unsigned int this_num;

		page = find_get_page(mapping, index);
		if (!page)
			break;

		this_num = min_t(unsigned, num, PAGE_CACHE_SIZE - offset);
		req->pages[req->num_pages] = page;
		req->num_pages++;

		offset = 0;
		num -= this_num;
		total_len += this_num;
		index++;
	}
	req->misc.retrieve_in.offset = outarg->offset;
	req->misc.retrieve_in.size = total_len;
	req->in.args[0].size = sizeof(req->misc.retrieve_in);
	req->in.args[0].value = &req->misc.retrieve_in;
	req->in.args[1].size = total_len;

	err = fuse_request_send_notify_reply(fc, req, outarg->notify_unique);
	if (err)
		fuse_retrieve_end(fc, req);

	return err;
}

static int fuse_notify_retrieve(struct fuse_conn *fc, unsigned int size,
				struct fuse_copy_state *cs)
{
	struct fuse_notify_retrieve_out outarg;
	struct inode *inode;
	int err;

	err = -EINVAL;
	if (size != sizeof(outarg))
		goto copy_finish;

	err = fuse_copy_one(cs, &outarg, sizeof(outarg));
	if (err)
		goto copy_finish;

	fuse_copy_finish(cs);

	down_read(&fc->killsb);
	err = -ENOENT;
	if (fc->sb) {
		u64 nodeid = outarg.nodeid;

		inode = ilookup5(fc->sb, nodeid, fuse_inode_eq, &nodeid);
		if (inode) {
			err = fuse_retrieve(fc, inode, &outarg);
			iput(inode);
		}
	}
	up_read(&fc->killsb);

	return err;

copy_finish:
	fuse_copy_finish(cs);
	return err;
}

static int fuse_notify(struct fuse_conn *fc, enum fuse_notify_code code,
		       unsigned int size, struct fuse_copy_state *cs)
{
	switch (code) {
	case FUSE_NOTIFY_POLL:
		return fuse_notify_poll(fc, size, cs);

	case FUSE_NOTIFY_INVAL_INODE:
		return fuse_notify_inval_inode(fc, size, cs);

	case FUSE_NOTIFY_INVAL_ENTRY:
		return fuse_notify_inval_entry(fc, size, cs);

	case FUSE_NOTIFY_STORE:
		return fuse_notify_store(fc, size, cs);

	case FUSE_NOTIFY_RETRIEVE:
		return fuse_notify_retrieve(fc, size, cs);

	case FUSE_NOTIFY_DELETE:
		return fuse_notify_delete(fc, size, cs);

	default:
		fuse_copy_finish(cs);
		return -EINVAL;
	}
}

/* Look up request on processing list by unique ID */
static struct fuse_req *request_find(struct fuse_numa_node *nn, u64 unique)
{
	struct list_head *entry;

	list_for_each(entry, &nn->processing) {
		struct fuse_req *req;
		req = list_entry(entry, struct fuse_req, list);
		if (req->in.h.unique == unique || req->intr_unique == unique)
			return req;
	}
	return NULL;
}

static struct fuse_req *request_find_allnodes(struct fuse_conn *fc,
					      u64 unique)
{
	struct fuse_numa_node *nn;
	struct fuse_req *req;
	int i;

	for (i = 0; i < fc->nr_nodes; i++) {
		nn = fc->nn[i];
		spin_lock(&nn->lock);
		req = request_find(nn, unique);
		if (req)
			return req;
		spin_unlock(&nn->lock);
	}
	return NULL;
}

static int copy_out_args(struct fuse_copy_state *cs, struct fuse_out *out,
			 unsigned nbytes)
{
	unsigned reqsize = sizeof(struct fuse_out_header);

	if (out->h.error)
		return nbytes != reqsize ? -EINVAL : 0;

	reqsize += len_args(out->numargs, out->args);

	if (reqsize < nbytes || (reqsize > nbytes && !out->argvar))
		return -EINVAL;
	else if (reqsize > nbytes) {
		struct fuse_arg *lastarg = &out->args[out->numargs-1];
		unsigned diffsize = reqsize - nbytes;
		if (diffsize > lastarg->size)
			return -EINVAL;
		lastarg->size -= diffsize;
	}
	return fuse_copy_args(cs, out->numargs, out->argpages, out->args,
			      out->page_zeroing);
}

/*
 * Write a single reply to a request.  First the header is copied from
 * the write buffer.  The request is then searched on the processing
 * list by the unique ID found in the header.  If found, then remove
 * it from the list and copy the rest of the buffer to the request.
 * The request is finished by calling request_end()
 */
static ssize_t fuse_dev_do_write(struct fuse_numa_node *nn,
				 struct fuse_copy_state *cs, size_t nbytes)
{
	int err;
	struct fuse_req *req;
	struct fuse_out_header oh;
	struct fuse_conn *fc = nn->fc;

	if (nbytes < sizeof(struct fuse_out_header))
		return -EINVAL;

	err = fuse_copy_one(cs, &oh, sizeof(oh));
	if (err)
		goto err_finish;

	err = -EINVAL;
	if (oh.len != nbytes)
		goto err_finish;

	/*
	 * Zero oh.unique indicates unsolicited notification message
	 * and error contains notification code.
	 */
	if (!oh.unique) {
		err = fuse_notify(fc, oh.error, nbytes - sizeof(oh), cs);
		return err ? err : nbytes;
	}

	err = -EINVAL;
	if (oh.error <= -1000 || oh.error > 0)
		goto err_finish;

	spin_lock(&nn->lock);
	err = -ENOENT;
	if (!fc->connected)
		goto err_unlock;

	req = request_find(nn, oh.unique);
	if (!req) {
		/*
		 * responding process could be different from reaped one, so
		 * the responding process could be on a different NUMA node.
		 * Hence search all numa node queues for the request
		 */
		spin_unlock(&nn->lock);
		req = request_find_allnodes(fc, oh.unique);
		if (!req)
			goto err_finish;
		nn = fc->nn[req->numa_id];
	}

	if (req->aborted) {
		spin_unlock(&nn->lock);
		fuse_copy_finish(cs);
		spin_lock(&nn->lock);
		request_end(nn, req);
		return -ENOENT;
	}
	/* Is it an interrupt reply? */
	if (req->intr_unique == oh.unique) {
		err = -EINVAL;
		if (nbytes != sizeof(struct fuse_out_header))
			goto err_unlock;

		if (oh.error == -ENOSYS)
			fc->no_interrupt = 1;
		else if (oh.error == -EAGAIN)
			queue_interrupt(nn, req);

		spin_unlock(&nn->lock);
		fuse_copy_finish(cs);
		return nbytes;
	}

	req->state = FUSE_REQ_WRITING;
	list_move(&req->list, &nn->io);
	req->out.h = oh;
	req->locked = 1;
	cs->req = req;
	if (!req->out.page_replace)
		cs->move_pages = 0;
	spin_unlock(&nn->lock);

	err = copy_out_args(cs, &req->out, nbytes);
	fuse_copy_finish(cs);

	spin_lock(&nn->lock);
	req->locked = 0;
	if (!err) {
		if (req->aborted)
			err = -ENOENT;
	} else if (!req->aborted)
		req->out.h.error = -EIO;
	request_end(nn, req);

	return err ? err : nbytes;

 err_unlock:
	spin_unlock(&nn->lock);
 err_finish:
	fuse_copy_finish(cs);
	return err;
}

static ssize_t fuse_dev_write(struct kiocb *iocb, const struct iovec *iov,
			      unsigned long nr_segs, loff_t pos)
{
	struct fuse_copy_state cs;
	struct fuse_conn *fc = fuse_get_conn(iocb->ki_filp);
	struct fuse_numa_node *nn;
	if (!fc)
		return -EPERM;

	nn = fuse_get_numa_node(fc);
	fuse_copy_init(&cs, nn, 0, iov, nr_segs);

	return fuse_dev_do_write(nn, &cs, iov_length(iov, nr_segs));
}

static ssize_t fuse_dev_splice_write(struct pipe_inode_info *pipe,
				     struct file *out, loff_t *ppos,
				     size_t len, unsigned int flags)
{
	unsigned nbuf;
	unsigned idx;
	struct pipe_buffer *bufs;
	struct fuse_copy_state cs;
	struct fuse_numa_node *nn;
	struct fuse_conn *fc;
	size_t rem;
	ssize_t ret;

	fc = fuse_get_conn(out);
	if (!fc)
		return -EPERM;

	bufs = kmalloc(pipe->buffers * sizeof(struct pipe_buffer), GFP_KERNEL);
	if (!bufs)
		return -ENOMEM;

	pipe_lock(pipe);
	nbuf = 0;
	rem = 0;
	for (idx = 0; idx < pipe->nrbufs && rem < len; idx++)
		rem += pipe->bufs[(pipe->curbuf + idx) & (pipe->buffers - 1)].len;

	ret = -EINVAL;
	if (rem < len) {
		pipe_unlock(pipe);
		goto out;
	}

	rem = len;
	while (rem) {
		struct pipe_buffer *ibuf;
		struct pipe_buffer *obuf;

		BUG_ON(nbuf >= pipe->buffers);
		BUG_ON(!pipe->nrbufs);
		ibuf = &pipe->bufs[pipe->curbuf];
		obuf = &bufs[nbuf];

		if (rem >= ibuf->len) {
			*obuf = *ibuf;
			ibuf->ops = NULL;
			pipe->curbuf = (pipe->curbuf + 1) & (pipe->buffers - 1);
			pipe->nrbufs--;
		} else {
			ibuf->ops->get(pipe, ibuf);
			*obuf = *ibuf;
			obuf->flags &= ~PIPE_BUF_FLAG_GIFT;
			obuf->len = rem;
			ibuf->offset += obuf->len;
			ibuf->len -= obuf->len;
		}
		nbuf++;
		rem -= obuf->len;
	}
	pipe_unlock(pipe);

	nn = fuse_get_numa_node(fc);
	fuse_copy_init(&cs, nn, 0, NULL, nbuf);
	cs.pipebufs = bufs;
	cs.pipe = pipe;

	if (flags & SPLICE_F_MOVE)
		cs.move_pages = 1;

	ret = fuse_dev_do_write(nn, &cs, len);

	for (idx = 0; idx < nbuf; idx++) {
		struct pipe_buffer *buf = &bufs[idx];
		buf->ops->release(pipe, buf);
	}
out:
	kfree(bufs);
	return ret;
}

static unsigned fuse_dev_poll(struct file *file, poll_table *wait)
{
	unsigned mask = POLLOUT | POLLWRNORM;
	struct fuse_conn *fc = fuse_get_conn(file);
	struct fuse_numa_node *nn;
	int i;
	if (!fc)
		return POLLERR;

	poll_wait(file, &fc->poll_waitq, wait);

	spin_lock(&fc->lock);
	if (!fc->connected)
		mask = POLLERR;
	else {
		for (i = 0; i < fc->nr_nodes; i++) {
			nn = fc->nn[i];
			spin_lock(&nn->lock);
			if (request_pending(nn)) {
				mask |= POLLIN | POLLRDNORM;
				spin_unlock(&nn->lock);
				break;
			}
			spin_unlock(&nn->lock);
		}
	}
	spin_unlock(&fc->lock);

	return mask;
}

/*
 * Abort all requests on the given list (pending or processing)
 *
 * This function releases and reacquires fc->lock
 */
static void end_requests(struct fuse_numa_node *nn, struct list_head *head)
__releases(&nn->lock)
__acquires(&nn->lock)
{
	while (!list_empty(head)) {
		struct fuse_req *req;
		req = list_entry(head->next, struct fuse_req, list);
		req->out.h.error = -ECONNABORTED;
		request_end(nn, req);
		spin_lock(&nn->lock);
	}
}

/*
 * Abort requests under I/O
 *
 * The requests are set to aborted and finished, and the request
 * waiter is woken up.  This will make request_wait_answer() wait
 * until the request is unlocked and then return.
 *
 * If the request is asynchronous, then the end function needs to be
 * called after waiting for the request to be unlocked (if it was
 * locked).
 */
static void end_io_requests(struct fuse_numa_node *nn)
__releases(&nn->lock)
__acquires(&nn->lock)
{
	while (!list_empty(&nn->io)) {
		struct fuse_req *req =
			list_entry(nn->io.next, struct fuse_req, list);
		void (*end) (struct fuse_conn *, struct fuse_req *) = req->end;
		struct fuse_conn *fc = nn->fc;

		req->aborted = 1;
		req->out.h.error = -ECONNABORTED;
		req->state = FUSE_REQ_FINISHED;
		list_del_init(&req->list);
		wake_up(&req->waitq);
		if (end) {
			req->end = NULL;
			__fuse_get_request(req);
			spin_unlock(&nn->lock);
			wait_event(req->waitq, !req->locked);
			end(fc, req);
			fuse_put_request(fc, req);
			spin_lock(&nn->lock);
		}
	}
}

static void end_queued_requests(struct fuse_conn *fc)
__releases(nn->lock)
__acquires(nn->lock)
{
	int i;
	struct fuse_numa_node *nn;

	for (i = 0; i < fc->nr_nodes; i++) {
		nn = fc->nn[i];
		spin_lock(&nn->lock);
		nn->max_background = UINT_MAX;
		flush_bg_queue(nn);
		end_requests(nn, &nn->pending);
		end_requests(nn, &nn->processing);
		while (forget_pending(nn))
			kfree(dequeue_forget(nn, 1, NULL));
		spin_unlock(&nn->lock);
	}
}

static void end_polls(struct fuse_conn *fc)
{
	struct rb_node *p;

	p = rb_first(&fc->polled_files);

	while (p) {
		struct fuse_file *ff;
		ff = rb_entry(p, struct fuse_file, polled_node);
		wake_up_interruptible_all(&ff->poll_wait);

		p = rb_next(p);
	}
}

/*
 * Abort all requests.
 *
 * Emergency exit in case of a malicious or accidental deadlock, or
 * just a hung filesystem.
 *
 * The same effect is usually achievable through killing the
 * filesystem daemon and all users of the filesystem.  The exception
 * is the combination of an asynchronous request and the tricky
 * deadlock (see Documentation/filesystems/fuse.txt).
 *
 * During the aborting, progression of requests from the pending and
 * processing lists onto the io list, and progression of new requests
 * onto the pending list is prevented by req->connected being false.
 *
 * Progression of requests under I/O to the processing list is
 * prevented by the req->aborted flag being true for these requests.
 * For this reason requests on the io list must be aborted first.
 */
void fuse_abort_conn(struct fuse_conn *fc)
{
	int i;
	struct fuse_numa_node *nn;

	spin_lock(&fc->lock);
	if (fc->connected) {
		fc->connected = 0;
		for (i = 0; i < fc->nr_nodes; i++) {
			nn = fc->nn[i];
			nn->blocked = 0;
			end_io_requests(nn);
			wake_up_all(&nn->waitq);
			wake_up_all(&nn->blocked_waitq);
		}
		wake_up_all(&fc->poll_waitq);
		end_queued_requests(fc);
		end_polls(fc);
		kill_fasync(&fc->fasync, SIGIO, POLL_IN);
	}
	spin_unlock(&fc->lock);
}
EXPORT_SYMBOL_GPL(fuse_abort_conn);

int fuse_dev_release(struct inode *inode, struct file *file)
{
	int i;
	struct fuse_numa_node *nn;
	struct fuse_conn *fc = fuse_get_conn(file);

	if (fc) {
		spin_lock(&fc->lock);
		fc->connected = 0;
		end_queued_requests(fc);
		for (i = 0; i < fc->nr_nodes; i++) {
			nn = fc->nn[i];
			nn->blocked = 0;
			wake_up_all(&nn->blocked_waitq);
		}
		end_polls(fc);
		spin_unlock(&fc->lock);
		fuse_conn_put(fc);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(fuse_dev_release);

static int fuse_dev_fasync(int fd, struct file *file, int on)
{
	struct fuse_conn *fc = fuse_get_conn(file);
	if (!fc)
		return -EPERM;

	/* No locking - fasync_helper does its own locking */
	return fasync_helper(fd, file, on, &fc->fasync);
}

const struct file_operations fuse_dev_operations = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.read		= do_sync_read,
	.aio_read	= fuse_dev_read,
	.splice_read	= fuse_dev_splice_read,
	.write		= do_sync_write,
	.aio_write	= fuse_dev_write,
	.splice_write	= fuse_dev_splice_write,
	.poll		= fuse_dev_poll,
	.release	= fuse_dev_release,
	.fasync		= fuse_dev_fasync,
};
EXPORT_SYMBOL_GPL(fuse_dev_operations);

static struct miscdevice fuse_miscdevice = {
	.minor = FUSE_MINOR,
	.name  = "fuse",
	.fops = &fuse_dev_operations,
};

int __init fuse_dev_init(void)
{
	int err = -ENOMEM;
	fuse_req_cachep = kmem_cache_create("fuse_request",
					    sizeof(struct fuse_req),
					    0, 0, NULL);
	if (!fuse_req_cachep)
		goto out;

	err = misc_register(&fuse_miscdevice);
	if (err)
		goto out_cache_clean;

	return 0;

 out_cache_clean:
	kmem_cache_destroy(fuse_req_cachep);
 out:
	return err;
}

void fuse_dev_cleanup(void)
{
	misc_deregister(&fuse_miscdevice);
	kmem_cache_destroy(fuse_req_cachep);
}

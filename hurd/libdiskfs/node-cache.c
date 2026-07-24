/* Inode cache.

   Copyright (C) 1994-2015 Free Software Foundation, Inc.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   The GNU Hurd is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with the GNU Hurd.  If not, see <http://www.gnu.org/licenses/>.  */

#include <hurd/ihash.h>

#include "diskfs.h"

/* The node cache is implemented using a hash table.  Access to the
   cache is protected by nodecache_lock.

   Every node in the cache carries a light reference.  When we are
   asked to give up that light reference, we reacquire our lock
   momentarily to check whether someone else reacquired a reference
   through the cache.  */

/* The size of ino_t is larger than hurd_ihash_key_t on 32 bit
   platforms.  We therefore have to use libihashs generalized key
   interface.  */

/* This is the mix function of fasthash, see
   https://code.google.com/p/fast-hash/ for reference.  */
#define mix_fasthash(h) ({              \
        (h) ^= (h) >> 23;               \
        (h) *= 0x2127599bf4325c37ULL;   \
        (h) ^= (h) >> 47; })

static hurd_ihash_key_t
hash (const void *key)
{
  ino_t i;
  i = *(ino_t *) key;
  mix_fasthash (i);
  return (hurd_ihash_key_t) i;
}

static int
compare (const void *a, const void *b)
{
  return *(ino_t *) a == *(ino_t *) b;
}

static struct hurd_ihash nodecache =
  HURD_IHASH_INITIALIZER_GKI (offsetof (struct node, slot), NULL, NULL,
                              hash, compare);
static pthread_rwlock_t nodecache_lock = PTHREAD_RWLOCK_INITIALIZER;
static struct node *nodecache_list_head = NULL;
static struct node *nodecache_list_tail = NULL;

/* Unlinks a node from the active nodes doubly-linked list.
   The caller MUST hold nodecache_lock (for writing) before calling this. */
static void
unlink_list_node (struct node *np)
{
  if (np->cache_prev)
    np->cache_prev->cache_next = np->cache_next;
  if (np->cache_next)
    np->cache_next->cache_prev = np->cache_prev;

  if (nodecache_list_head == np)
    nodecache_list_head = np->cache_next;
  if (nodecache_list_tail == np)
    nodecache_list_tail = np->cache_prev;

  np->cache_prev = NULL;
  np->cache_next = NULL;
}

/* Adds a node to the tail of the active nodes doubly-linked list.
   The caller MUST hold nodecache_lock (for writing) before calling this. */
static void
link_list_node (struct node *np)
{
  np->cache_next = NULL;
  np->cache_prev = nodecache_list_tail;

  if (nodecache_list_tail)
    nodecache_list_tail->cache_next = np;
  else
    nodecache_list_head = np;

  nodecache_list_tail = np;
}

/* Fetch inode INUM, set *NPP to the node structure;
   gain one user reference and lock the node.  */
error_t __attribute__ ((weak))
diskfs_cached_lookup (ino_t inum, struct node **npp)
{
  return diskfs_cached_lookup_context (inum, npp, NULL);
}

/* Fetch inode INUM, set *NPP to the node structure;
   gain one user reference and lock the node.  */
error_t
diskfs_cached_lookup_context (ino_t inum, struct node **npp,
			      struct lookup_context *ctx)
{
  error_t err;
  struct node *np, *tmp;
  hurd_ihash_locp_t slot;

  pthread_rwlock_rdlock (&nodecache_lock);
  np = hurd_ihash_locp_find (&nodecache, (hurd_ihash_key_t) &inum, &slot);
  if (np)
    goto gotit;
  pthread_rwlock_unlock (&nodecache_lock);

  err = diskfs_user_make_node (&np, ctx);
  if (err)
    return err;

  np->cache_id = inum;
  pthread_mutex_lock (&np->lock);

  /* Put NP in NODEHASH.  */
  pthread_rwlock_wrlock (&nodecache_lock);
  tmp = hurd_ihash_locp_find (&nodecache, (hurd_ihash_key_t) &np->cache_id,
			      &slot);
  if (tmp)
    {
      /* We lost a race.  */
      diskfs_nput (np);
      np = tmp;
      goto gotit;
    }

  err = hurd_ihash_locp_add (&nodecache, slot,
			     (hurd_ihash_key_t) &np->cache_id, np);
  assert_perror_backtrace (err);
  link_list_node (np);
  diskfs_nref_light (np);
  pthread_rwlock_unlock (&nodecache_lock);

  /* Get the contents of NP off disk.  */
  err = diskfs_user_read_node (np, ctx);
  if (err)
   {
    pthread_rwlock_wrlock (&nodecache_lock);
    hurd_ihash_remove (&nodecache, (hurd_ihash_key_t) &np->cache_id);
    unlink_list_node (np);
    pthread_rwlock_unlock (&nodecache_lock);

    /* Don't delete from disk. */
    np->dn_stat.st_nlink = 1;
    np->allocsize = 0;
    np->dn_set_ctime = 0;
    np->dn_set_atime = 0;
    np->dn_set_mtime = 0;
    diskfs_nput (np);
    *npp = NULL;

    return err;
   }
  else
    {
      *npp = np;
      return 0;
    }

 gotit:
  diskfs_nref (np);
  pthread_rwlock_unlock (&nodecache_lock);
  pthread_mutex_lock (&np->lock);
  *npp = np;
  return 0;
}

/* Lookup node INUM (which must have a reference already) and return it
   without allocating any new references. */
struct node *
diskfs_cached_ifind (ino_t inum)
{
  struct node *np;

  pthread_rwlock_rdlock (&nodecache_lock);
  np = hurd_ihash_find (&nodecache, (hurd_ihash_key_t) &inum);
  pthread_rwlock_unlock (&nodecache_lock);

  assert_backtrace (np);
  return np;
}

void __attribute__ ((weak))
diskfs_try_dropping_softrefs (struct node *np)
{
  pthread_rwlock_wrlock (&nodecache_lock);
  if (np->slot != NULL)
    {
      /* Check if someone reacquired a reference through the
	 nodehash.  */
      struct references result;
      refcounts_references (&np->refcounts, &result);

      if (result.hard > 0)
	{
	  /* A reference was reacquired through a hash table lookup.
	     It's fine, we didn't touch anything yet. */
	  pthread_rwlock_unlock (&nodecache_lock);
	  return;
	}

      hurd_ihash_locp_remove (&nodecache, np->slot);
      np->slot = NULL;

      unlink_list_node (np);
      /* Flush node if needed, before forgetting it */
      diskfs_node_update (np, diskfs_synchronous);

      diskfs_nrele_light (np);
    }
  pthread_rwlock_unlock (&nodecache_lock);

  diskfs_user_try_dropping_softrefs (np);
}

/* For each active node, call FUN.  The node is to be locked around the call
   to FUN.  If FUN returns non-zero for any node, then immediately stop, and
   return that value.

   We iterate the list forwards (from head to tail). Since new nodes
   are appended to the tail, this means we process the oldest nodes first
   (FIFO order). This preserves the chronological order of file creation
   and modification, which allows the block layer and disk scheduler to
   coalesce I/O operations and perform sequential disk writes efficiently.
   Iterating backwards (LIFO) would cause severe disk thrashing. */
error_t __attribute__ ((weak))
diskfs_node_iterate (error_t (*fun)(struct node *))
{
  error_t err = 0;
  struct node *current, *next_node;

  pthread_rwlock_rdlock (&nodecache_lock);
  current = nodecache_list_head;

  /* Bootstrap the loop by grabbing a ref to the very first node */
  if (current)
    refcounts_ref (&current->refcounts, NULL);

  while (current != NULL)
    {
      /* Grab a next pointer so it doesn't disappear while we are processing
       * 'current' */
      next_node = current->cache_next;
      if (next_node)
        refcounts_ref (&next_node->refcounts, NULL);

      pthread_rwlock_unlock (&nodecache_lock);

      pthread_mutex_lock (&current->lock);
      err = (*fun)(current);
      pthread_mutex_unlock (&current->lock);

      /* We are done with 'current', drop the ref we grabbed */
      diskfs_nrele (current);
      if (err)
        {
          /* We don't need to traverse the rest of the list!
             Just drop the next_node ref if we grabbed it, and return. */
          if (next_node)
            diskfs_nrele (next_node);
          return err;
        }

      /* Re-acquire the global lock to loop around */
      pthread_rwlock_rdlock (&nodecache_lock);
      current = next_node;
    }

  pthread_rwlock_unlock (&nodecache_lock);
  return err;
}

/* The user must define this function if she wants to use the node
   cache.  Create and initialize a node.  */
error_t __attribute__ ((weak))
diskfs_user_make_node (struct node **npp, struct lookup_context *ctx)
{
  assert_backtrace (! "diskfs_user_make_node not implemented");
}

/* The user must define this function if she wants to use the node
   cache.  Read stat information out of the on-disk node.  */
error_t __attribute__ ((weak))
diskfs_user_read_node (struct node *np, struct lookup_context *ctx)
{
  assert_backtrace (! "diskfs_user_read_node not implemented");
}

/* The user must define this function if she wants to use the node
   cache.  The last hard reference to a node has gone away; arrange to
   have all the weak references dropped that can be.  */
void __attribute__ ((weak))
diskfs_user_try_dropping_softrefs (struct node *np)
{
  assert_backtrace (! "diskfs_user_try_dropping_softrefs not implemented");
}

/*
 * UnixFS
 *
 * A general-purpose file system layer for writing/reimplementing/porting
 * Unix file systems through MacFUSE.

 * Copyright (c) 2008 Amit Singh. All Rights Reserved.
 * http://osxbook.com
 */

/*
 * XXX: This is very ad hoc right now. I made it "work" only for the
 * demos. Do not rely on this for read-write support (yet).
 */

#include "unixfs_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

static int desirednodes = 65536;
static pthread_mutex_t ihash_lock;
static LIST_HEAD(ihash_head, inode) *ihash_table = NULL;
typedef struct ihash_head ihash_head;
static size_t ihash_count = 0;
static size_t iprivsize = 0;

static u_long ihash_mask;

static ihash_head*
unixfs_inodelayer_firstfromhash(ino_t ino)
{
    return (ihash_head*)&ihash_table[ino & ihash_mask];
}

int
unixfs_inodelayer_init(size_t privsize)
{
    if (!UNIXFS_ENABLE_INODEHASH)
        return 0;

    if (pthread_mutex_init(&ihash_lock, (const pthread_mutexattr_t*)0)) {    
        fprintf(stderr, "failed to initialize the inode layer lock\n");
        return -1;
    }

    iprivsize = privsize;

    int i;
    u_long hashsize;
    LIST_HEAD(generic, generic) *hashtbl;

    for (hashsize = 1; hashsize <= desirednodes; hashsize <<= 1)
            continue;

    hashsize >>= 1;

    hashtbl = (struct generic *)malloc(hashsize * sizeof(*hashtbl));
    if (hashtbl != NULL) {
        for (i = 0; i < hashsize; i++)
            LIST_INIT(&hashtbl[i]);
         ihash_mask = hashsize - 1;
         ihash_table = (struct ihash_head *)hashtbl;
    }

    if (ihash_table == NULL) {
        (void)pthread_mutex_destroy(&ihash_lock);
        return -1;
    }
    
    return 0;
}

void
unixfs_inodelayer_fini(void)
{
    if (!UNIXFS_ENABLE_INODEHASH)
        return;

    if (ihash_table != NULL) {
        if (ihash_count != 0) {
            fprintf(stderr,
                    "*** warning: ihash terminated when not empty (%lu)\n",
                    ihash_count);

            int node_index = 0;
            u_long ihash_index = 0;
            for (; ihash_index < ihash_mask; ihash_index++) {
                struct inode* ip;
                LIST_FOREACH(ip, &ihash_table[ihash_index], I_hashlink) {
                    fprintf(stderr, "*** warning: inode %llu still present\n",
                            ip->I_number);
                    node_index++;
                }
            }
        }

        u_long i;
        for (i = 0; i < (ihash_mask + 1); i++) {
            if (ihash_table[i].lh_first != NULL)
                fprintf(stderr,
                        "*** warning: found ihash_table[%lu].lh_first = %p\n",
                        i, ihash_table[i].lh_first);
        }
        free(ihash_table);
        ihash_table = NULL;
    }

    (void)pthread_mutex_destroy(&ihash_lock);
}

struct inode *
unixfs_inodelayer_iget(ino_t ino)
{
    if (!UNIXFS_ENABLE_INODEHASH) {
        struct inode* new_node = calloc(1, sizeof(struct inode) + iprivsize);
        if (new_node == NULL)
            return NULL;
        new_node->I_number = ino;
        if (iprivsize)
            new_node->I_private = (void*)&((struct inode *)new_node)[1];
        return new_node;
    }

    struct inode* this_node = NULL;
    struct inode* new_node = NULL;
    int needs_unlock = 1;
    int err;

    pthread_mutex_lock(&ihash_lock);

    do {
        err = EAGAIN;
        this_node = LIST_FIRST(unixfs_inodelayer_firstfromhash(ino));
        while (this_node != NULL) {
            if (this_node->I_number == ino)
                break;
            this_node = LIST_NEXT(this_node, I_hashlink);
        }

        if (this_node == NULL) {
            if (new_node == NULL) {
                pthread_mutex_unlock(&ihash_lock);
                new_node = calloc(1, sizeof(struct inode) + iprivsize);
                if (new_node == NULL) {
                    err = ENOMEM;
                } else {
                    new_node->I_number = ino;
                    if (iprivsize)
                        new_node->I_private =
                            (void*)&((struct inode *)new_node)[1];
                    (void)pthread_cond_init(&new_node->I_state_cond,
                                            (const pthread_condattr_t*)0);
                }
                pthread_mutex_lock(&ihash_lock);
            } else {
                LIST_INSERT_HEAD(unixfs_inodelayer_firstfromhash(ino),
                                 new_node, I_hashlink);
                ihash_count++;
                this_node = new_node;
                new_node = NULL;
            }
        }

        if (this_node != NULL) {
            if (this_node->I_attachoutstanding) {
                this_node->I_waiting = 1;
                this_node->I_count++; /* XXX See comment below. */
                while (this_node->I_attachoutstanding) {
                    int ret = pthread_cond_wait(&this_node->I_state_cond,
                                                 &ihash_lock);
                    if (ret) {
                        fprintf(stderr, "lock %p failed for inode %llu\n",
                                &this_node->I_state_cond, ino);
                        abort();
                    }
                }
                pthread_mutex_unlock(&ihash_lock); /* XXX See comment below. */
                err = needs_unlock = 0; /* XXX See comment below. */
                /*
                 * XXX Yes, this comment. There's a subtlety here. This logic
                 * will work only for a read-only file system. If the hash
                 * table could change while we were sleeping, we must loop
                 * again.
                 */
            } else if (this_node->I_initialized == 0) {
                this_node->I_count++;
                this_node->I_attachoutstanding = 1;
                pthread_mutex_unlock(&ihash_lock);
                err = needs_unlock = 0;
            } else {
                this_node->I_count++;
                pthread_mutex_unlock(&ihash_lock);
                err = needs_unlock = 0;
            }
        }

    } while (err == EAGAIN);

    if (needs_unlock)
        pthread_mutex_unlock(&ihash_lock);

    if (new_node != NULL)
        free(new_node);
        
    return this_node;
}

void
unixfs_inodelayer_isucceeded(struct inode* ip)
{
    if (!UNIXFS_ENABLE_INODEHASH)
        return;

    pthread_mutex_lock(&ihash_lock);
    ip->I_initialized = 1;
    ip->I_attachoutstanding = 0;
    if (ip->I_waiting) {
        ip->I_waiting = 0;
        pthread_cond_broadcast(&ip->I_state_cond);
    }
    pthread_mutex_unlock(&ihash_lock);
}

void
unixfs_inodelayer_ifailed(struct inode* ip)
{
    if (!UNIXFS_ENABLE_INODEHASH)
        return;

    pthread_mutex_lock(&ihash_lock);
    LIST_REMOVE(ip, I_hashlink);
    ip->I_initialized = 0;
    ip->I_attachoutstanding = 0;
    if (ip->I_waiting) {
        ip->I_waiting = 0;
        pthread_cond_broadcast(&ip->I_state_cond);
    }
    ihash_count--;
    pthread_mutex_unlock(&ihash_lock);
    (void)pthread_cond_destroy(&ip->I_state_cond);
    free(ip);
}

void
unixfs_inodelayer_iput(struct inode* ip)
{
    if (!UNIXFS_ENABLE_INODEHASH) {
        free(ip);
        return;
    }

    pthread_mutex_lock(&ihash_lock);
    ip->I_count--;
    if (ip->I_count == 0) {
        LIST_REMOVE(ip, I_hashlink);
        ihash_count--;
        pthread_mutex_unlock(&ihash_lock);
        (void)pthread_cond_destroy(&ip->I_state_cond);
        free(ip);
    } else
        pthread_mutex_unlock(&ihash_lock);
}

void
unixfs_inodelayer_dump(unixfs_inodelayer_iterator_t it)
{
    pthread_mutex_lock(&ihash_lock);

    int node_index = 0;
    u_long ihash_index = 0;

    for (; ihash_index < ihash_mask; ihash_index++) {
        struct inode* ip;
        LIST_FOREACH(ip, &ihash_table[ihash_index], I_hashlink) {
            if (it(ip, ip->I_private) != 0)
                goto out;
            node_index++;
        }
    }

out:
    pthread_mutex_unlock(&ihash_lock);
}

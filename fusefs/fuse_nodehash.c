#include "fuse.h"
#include "fuse_nodehash.h"
#include <sys/vnode.h>

// The following headers are only available to kernel code, which is the default 
// disposition.  To simplify testing, however, we compile this module for user 
// space.  In that case, we can't include these headers.  Rather, the relevant 
// declarations come from "UserSpaceKernel.h".

#if KERNEL
    #include <kern/assert.h>
#endif

/////////////////////////////////////////////////////////////////////

// The lck_mtx_assert routine is only available via the "com.apple.kpi.unsupported" 
// KPI.  I don't want to ship production code that uses this macro, so I 
// use a cover macro (LCK_MTX_ASSERT) that maps to lck_mtx_assert for debug builds 
// and nothing for production builds.  I also preprocess the KEXT's Info.plist 
// so that I only include "com.apple.kpi.unsupported" in the OSBundleLibraries 
// dictionary in the debug build.

#if MACH_ASSERT
    #define LCK_MTX_ASSERT lck_mtx_assert
#else
    #define LCK_MTX_ASSERT(gHashMutex, LCK_MTX_ASSERT_OWNED) do { } while (0)
#endif

/////////////////////////////////////////////////////////////////////

// The HNode structure represents an entry in the VFS plug-ins hash table.  See 
// the comments in the header file for a detailed description of the relationship 
// between this data structure and the vnodes that are maintained by VFS itself. 
//
// The HNode and the FSNode are intimately tied together.  In fact, they're allocated 
// from the same memory block.  When we allocate an HNode, we allocate extra space 
// (gFSNodeSize bytes) for the FSNode.
// 
// This data structure is effectively reference counted by the forkVNodesCount 
// field.  When the last vnode that references this HNode is reclaimed, the HNode 
// itself is reclaimed (along with the associated FSNode).

struct HNode {
    uint32_t            magic;                  // [1] -> gMagic, that is, client supplied magic number
    LIST_ENTRY(HNode)   hashLink;               // [2] next pointer for hash chain
    dev_t               dev;                    // [1] device number on which file system object (fsobj) resides
    uint64_t              ino;                  // [1] inode number of fsobj resides
    boolean_t           attachOutstanding;      // [2] [3]
    boolean_t           waiting;                // [2] true if someone is waiting for attachOutstanding to go false
    size_t              forkVNodesSize;         // [2] size of forkVNodes array, must be non-zero
    size_t              forkVNodesCount;        // [2] number of non-NULL vnodes in array (plus one if attachOutstanding is true)
    vnode_t *           forkVNodes;             // [2] array of vnodes, indexed by forkIndex
    union {
        vnode_t         internal;               // [2] if forkVNodesSize == 1, the vnode is stored internally
        vnode_t *       external;               // [2] if forkVNodesSize > 1, the vnodes are stored in a separately allocated array
    } forkVNodesStorage;
};
typedef struct HNode HNode;

// HNode Notes
// -----------
// [1] This field is immutable.  That is, it's set up as part of the process of 
//     creating an HNode, and is not modified after that.  Thus, it doesn't need 
//     to be protected from concurrent access.
//
// [2] The gHashMutex lock protects this field in /all/ HNodes.
//
// [3] This is true if HNodeLookupCreatingIfNecessary has return success but with a 
//     NULL vnode.  In this case, we're expecting the client to call either 
//     HNodeAttachVNodeSucceeded or HNodeAttachVNodeFailed at some time in the future. 
//     While this is true, forkVNodesCount is incremented to prevent the HNode from 
//     going away.

// The following client globals are set by the client when it calls HNodeInit.
// See the header comments for HNodeInit for more details.

static uint32_t         gMagic;
static lck_grp_t *      gLockGroup;
static size_t           gFSNodeSize;
static OSMallocTag      gOSMallocTag;

// gHashMutex is a single mutex that protects all fields (except the immutable ones) of 
// all HNodes, the hash table itself (all elements of the gHashTable array), and gHashNodeCount.

static lck_mtx_t *      gHashMutex;

// gHashNodeCount is a count of the number of HNodes in the hash table. 
// This is used solely for debugging (if it's non-zero when HNodeTerm is called, 
// the debug version of the code will panic).

static size_t           gHashNodeCount;

// gHashTable is a pointer to an array of HNodeHashHead structures that represent 
// the heads of all the hash chains.  This structure, and the associated 
// gHashTableMask, are all a consequence of my use of hashinit.  I wouldd point you 
// to the hashinit documentation, but there isn't any yet (-:

static LIST_HEAD(HNodeHashHead, HNode) *   gHashTable;
typedef struct HNodeHashHead HNodeHashHead;

static u_long           gHashTableMask;

static HNodeHashHead * HNodeGetFirstFromHashTable(dev_t dev, uint64_t ino)
    // Given a device number and an inode number, return a pointer to the 
    // hash chain head.
{
    return (HNodeHashHead *) &gHashTable[((uint64_t)dev + ino) & gHashTableMask];
}

extern errno_t HNodeInit(
    lck_grp_t *     lockGroup, 
    lck_attr_t *    lockAttr, 
    OSMallocTag     mallocTag, 
    uint32_t        magic, 
    size_t          fsNodeSize
)
    // See comments in header.
{
    errno_t     err;
    
    assert(lockGroup != NULL);
    // lockAttr may be NULL
    assert(mallocTag != NULL);
    assert(fsNodeSize != 0);

    gMagic       = magic;
    gFSNodeSize  = fsNodeSize;
    gOSMallocTag = mallocTag;
    gLockGroup   = lockGroup;

    gHashMutex = lck_mtx_alloc_init(lockGroup, lockAttr);
    gHashTable = hashinit(desiredvnodes, M_TEMP, &gHashTableMask);
    err = 0;
    if ( (gHashMutex == NULL) || (gHashTable == NULL) ) {
        HNodeTerm();                        // clean up any partial allocations
        err = ENOMEM;
    }
    return err;
}

extern void    HNodeTerm(void)
    // See comments in header.
{
    // Free the hash table.  Also, if there are any hash nodes left, we shouldn't 
    // be terminating.
    
    if (gHashTable != NULL) {
        assert(gHashNodeCount == 0);
        #if MACH_ASSERT
            {
                u_long      i;
                
                for (i = 0; i < (gHashTableMask + 1); i++) {
                    assert(gHashTable[i].lh_first == NULL);
                }
            }
        #endif
        FREE(gHashTable, M_TEMP);
        gHashTable = NULL;
    }
    
    if (gHashMutex != NULL) {
        assert(gLockGroup != NULL);
        
        lck_mtx_free(gHashMutex, gLockGroup);
        gHashMutex = NULL;
    }

    gLockGroup = NULL;
    gOSMallocTag = NULL;
    gFSNodeSize = 0;
    gMagic = 0;
}

extern void * FSNodeGenericFromHNode(HNodeRef hnode)
    // See comments in header.
{
    assert(hnode != NULL);
    assert(hnode->magic == gMagic);
    return (void *) &hnode[1];
}

extern HNodeRef HNodeFromFSNodeGeneric(void * fsNode)
    // See comments in header.
{
    assert(fsNode != NULL);
    return  &((HNodeRef) fsNode)[-1];
}

extern HNodeRef HNodeFromVNode(vnode_t vn)
    // See comments in header.
{
    HNodeRef hnode;
    
    assert(vn != NULL);
    hnode = vnode_fsnode(vn);
    assert(hnode != NULL);
    assert(hnode->magic = gMagic);
    
    return hnode;
}

extern void *   FSNodeGenericFromVNode(vnode_t vn)
    // See comments in header.
{
    assert(vn != NULL);
    return FSNodeGenericFromHNode(HNodeFromVNode(vn));
}

extern dev_t HNodeGetDevice(HNodeRef hnode)
    // See comments in header.
{
    assert(hnode != NULL);
    assert(hnode->magic == gMagic);
    return hnode->dev;
}

extern uint64_t HNodeGetInodeNumber(HNodeRef hnode)
    // See comments in header.
{
    assert(hnode != NULL);
    assert(hnode->magic == gMagic);
    return hnode->ino;
}

extern vnode_t HNodeGetVNodeForForkAtIndex(HNodeRef hnode, __unused size_t forkIndex)
    // See comments in header.
{
    vnode_t     vn;
    
    assert(hnode != NULL);
    assert(hnode->magic == gMagic);
    assert(forkIndex < hnode->forkVNodesSize);
    
    // Locking and unlocking gHashMutex /is/ needed, because another thread might 
    // be swapping in an expanded forkVNodes array.  Because of the multi-threaded 
    // nature of the kernel, no amount of clever ordering of this swap can prevent 
    // the possibility of us seeing inconsistent data.
    
    lck_mtx_lock(gHashMutex);

    vn = hnode->forkVNodes[forkIndex];

    lck_mtx_unlock(gHashMutex);
    
    return vn;
}

extern size_t HNodeGetForkIndexForVNode(vnode_t vn)
    // See comments in header.
    //
    // The fact that the caller must hold some sort of reference on the vnode 
    // prevents the vnode from being reclaimed, which means that we're 
    // guaranteed to find the vnode in the fork array.
{
    HNodeRef    hnode;
    size_t      forkCount;
    size_t      forkIndex;
    
    assert(vn != NULL);
    
    hnode = HNodeFromVNode(vn);         // HNodeFromVNode asserts the validity of its result
    
    // Locking and unlocking gHashMutex is needed, because another thread might 
    // be switching in an expanded forkVNodes array.
    
    lck_mtx_lock(gHashMutex);

    forkCount = hnode->forkVNodesSize;
    for (forkIndex = 0; forkIndex < forkCount; forkIndex++) {
        if (vn == hnode->forkVNodes[forkIndex]) {
            break;
        }
    }
    assert(forkIndex != forkCount);     // that is, that vn is in forkVNodes

    lck_mtx_unlock(gHashMutex);
    
    return forkIndex;
}

extern errno_t HNodeLookupCreatingIfNecessary(dev_t dev, uint64_t ino, size_t forkIndex, HNodeRef *hnodePtr, vnode_t *vnPtr)
    // See comments in header.
{
    errno_t         err;
    HNodeRef        thisNode;
    HNodeRef        newNode;
    vnode_t *       newForkBuffer;
    boolean_t       needsUnlock;
    vnode_t         resultVN;
    uint32_t        vid;
    
    assert( hnodePtr != NULL);
    assert(*hnodePtr == NULL);
    assert( vnPtr    != NULL);
    assert(*vnPtr    == NULL);

    // If you forget to call HNodeInit, it's likely that the first call you'll make is 
    // HNodeLookupCreatingIfNecessary (to set up your root vnode), and this assert will 
    // fire (rather than you dying inside with a memory access exception inside lck_mtx_lock).
    
    assert(gHashMutex != NULL);

    newNode = NULL;
    newForkBuffer = NULL;
    needsUnlock = TRUE;
    resultVN = NULL;
    
    lck_mtx_lock(gHashMutex);
    
    do {
        LCK_MTX_ASSERT(gHashMutex, LCK_MTX_ASSERT_OWNED);

        err = EAGAIN;
        
        // First look it up in the hash table.
        
        thisNode = LIST_FIRST(HNodeGetFirstFromHashTable(dev, ino));
        while (thisNode != NULL) {
            assert(thisNode->magic == gMagic);
            
            if ( (thisNode->dev == dev) && (thisNode->ino == ino) ) {
                break;
            }
            thisNode = LIST_NEXT(thisNode, hashLink);
        }
        
        // If we didn't find it, we're creating a new HNode.  If we haven't already 
        // allocated newNode, we must do so.  This drops the mutex, so the hash table 
        // might have been changed by someone else, so we have to loop.
        //
        // If we do have a newNode at hand, use that.
        
        if (thisNode == NULL) {
            if (newNode == NULL) {
                lck_mtx_unlock(gHashMutex);

                // Allocate a new node.
                
                newNode = FUSE_OSMalloc(sizeof(*newNode) + gFSNodeSize, gOSMallocTag);
                if (newNode == NULL) {
                    err = ENOMEM;
                } else {
                    // Fill it in.
                    
                    memset(newNode, 0, sizeof(*newNode) + gFSNodeSize);
                    
                    newNode->magic          = gMagic;
                    newNode->dev            = dev;
                    newNode->ino            = ino;
                    
                    // If we're dealing with the first fork, use the internal buffer.  
                    // Otherwise allocate an external buffer.
                    
                    if (forkIndex == 0) {
                        newNode->forkVNodesSize = 1;
                        newNode->forkVNodes     = &newNode->forkVNodesStorage.internal;
                        newNode->forkVNodesStorage.internal = NULL;
                    } else {
                        newNode->forkVNodesSize = forkIndex + 1;
                        newNode->forkVNodesStorage.external = FUSE_OSMalloc(sizeof(*newNode->forkVNodesStorage.external) * (forkIndex + 1), gOSMallocTag);
                        newNode->forkVNodes = newNode->forkVNodesStorage.external;
                        if (newNode->forkVNodesStorage.external == NULL) {
                            // If this allocation fails, we don't have to clean up newNode, 
                            // because we'll fall out of the loop and newNode will get cleaned 
                            // up at the end.
                            err = ENOMEM;
                        } else {
                            memset(newNode->forkVNodesStorage.external, 0, sizeof(*newNode->forkVNodesStorage.external) * (forkIndex + 1));
                        }
                    }
                }

                lck_mtx_lock(gHashMutex);
            } else {
                LIST_INSERT_HEAD(HNodeGetFirstFromHashTable(dev, ino), newNode, hashLink);
                gHashNodeCount += 1;

                // Set thisNode to the node that we inserted, and clear newNode so it 
                // doesn't get freed.
                
                thisNode = newNode;
                newNode = NULL;
                
                // IMPORTANT:
                // There's a /really/ subtle point here.  Once we've inserted the new node 
                // into the hash table, it can be discovered by other threads.  This would 
                // be bad, because it's only partially constructed at this point.  We prevent 
                // this problem by not dropping gHashMutex from this point to the point that 
                // we're done.  This only works because we allocate the new node with a fork 
                // buffer that's adequate to meet our needs.
            }
        }

        // If we found a hash node (including the case where we've used one that we previously 
        // allocated), check its status.
        
        if (thisNode != NULL) {
        
            if (thisNode->attachOutstanding) {
                // If there are outstanding attaches, wait for them to complete.  
                // This means that there can be only one outstanding attach at a time, 
                // which is important because we don't want two threads trying to 
                // fill in the same fork's vnode entry.
                //
                // In theory we might keep an array of outstanding attach flags, one 
                // for each fork, but that's difficult and probably overkill.

                thisNode->waiting = TRUE;
                
                (void) msleep(thisNode, gHashMutex, PINOD, "HNodeLookupCreatingIfNecessary", NULL);
                
                // msleep drops and reacquires the mutex; the hash table may have changed, 
                // so we loop.
            } else if (forkIndex >= thisNode->forkVNodesSize) {
                // If the fork vnode array (a buffer described by thisNode->forkVNodes 
                // and thisNode->forkVNodesSize) is too small, install a new buffer, 
                // big enough to hold the vnode fork forkIndex'th fork.
                
                if (newForkBuffer == NULL) {
                    // If we don't already have a new fork buffer, allocate one.  
                    // Because this drops the mutex, we have to loop and start again 
                    // from scratch.

                    lck_mtx_unlock(gHashMutex);

                    newForkBuffer = FUSE_OSMalloc(sizeof(*newForkBuffer) * (forkIndex + 1), gOSMallocTag);
                    if (newForkBuffer == NULL) {
                        err = ENOMEM;
                    } else {
                        memset(newForkBuffer, 0, sizeof(*newForkBuffer) * (forkIndex + 1));
                    }
                    
                    lck_mtx_lock(gHashMutex);
                } else {
                    // Insert the newForkBuffer into theNode.  This only works because 
                    // readers of the thisNode->forkVNodes array (notably this routine 
                    // and HNodeGetVNodeForForkAtIndex) always take gHashMutex.  If that 
                    // wasn't the case, you could get into some subtle race conditions 
                    // as thread A brings a copy of thisNode->forkVNodes into a register 
                    // and then gets blocked, then thread B runs and expands the array 
                    // and frees the buffer that is being pointed to be thread A's 
                    // register.
                    
                    vnode_t *   oldForkBuffer;
                    size_t      oldForkBufferSize;
                    
                    oldForkBufferSize = 0;          // quieten a false warning
                    
                    // We only free the old fork buffer if it was external, rather than 
                    // the single vnode buffer embedded in the HNode.
                    
                    oldForkBuffer = NULL;
                    if (thisNode->forkVNodesSize > 1) {
                        oldForkBuffer = thisNode->forkVNodesStorage.external;
                        oldForkBufferSize = thisNode->forkVNodesSize;
                    }
                    memcpy(newForkBuffer, thisNode->forkVNodes, sizeof(*thisNode->forkVNodes) * thisNode->forkVNodesSize);
                    thisNode->forkVNodes = newForkBuffer;
                    thisNode->forkVNodesSize = (forkIndex + 1);
                    thisNode->forkVNodesStorage.external = newForkBuffer;
                    
                    newForkBuffer = NULL;           // so we don't free it at the end

                    // Drop the mutex around the free, and then start again from scratch.

                    lck_mtx_unlock(gHashMutex);
                    
                    if (oldForkBuffer != NULL) {
                        FUSE_OSFree(oldForkBuffer, sizeof(*oldForkBuffer) * oldForkBufferSize, gOSMallocTag);
                    }
                    
                    lck_mtx_lock(gHashMutex);                       
                }
            } else if (thisNode->forkVNodes[forkIndex] == NULL) {
                // If there's no existing vnode associated with this fork of the HNode, 
                // we're done.  The caller is responsible for attaching a vnode for 
                // this fork.  Setting attachOutstanding will block any other threads 
                // from using the HNode until the caller is done attaching.  Also, 
                // we artificially increment the reference count to prevent the HNode 
                // from being freed until the caller has finished with it.
                
                thisNode->attachOutstanding = TRUE;
                thisNode->forkVNodesCount += 1;
                
                // Results for the caller.
                
                assert(thisNode != NULL);
                assert(resultVN == NULL);
                
                err = 0;
            } else {
                vnode_t     candidateVN;
                
                // If there is an existing vnode, get a reference on it and return 
                // that to the caller.  This vnode reference prevents the vnode from 
                // being reclaimed, which prevents the HNode from being freed.
                
                candidateVN = thisNode->forkVNodes[forkIndex];
                assert(candidateVN != NULL);
                
                // Check that our vnode hasn't been recycled.  If this succeeds, it 
                // acquires a reference on the vnode, which is the one we return to 
                // our caller.  We do this with gHashMutex unlocked to avoid any 
                // deadlock concerns.
                
                vid = vnode_vid(candidateVN);
                
                lck_mtx_unlock(gHashMutex);
                
                err = vnode_getwithvid(candidateVN, vid);

                if (err == 0) {
                    // All ok; return the HNode/vnode to the caller.
                    assert(thisNode != NULL);
                    assert(resultVN == NULL);
                    resultVN = candidateVN;
                    needsUnlock = FALSE;
                } else {
                    // We're going to loop and retry, so relock the mutex.
                    
                    lck_mtx_lock(gHashMutex);

                    err = EAGAIN;
                }
            }
        }
    } while (err == EAGAIN);

    // On success, pass results back to the caller.
    
    if (err == 0) {
        *hnodePtr = thisNode;
        *vnPtr    = resultVN;
    }
    
    // Clean up.
    
    if (needsUnlock) {
        lck_mtx_unlock(gHashMutex);
    }

    // Free newForkBuffer if we allocated it but didn't use it.
    
    if (newForkBuffer != NULL) {
        FUSE_OSFree(newForkBuffer, sizeof(*newForkBuffer) * (forkIndex + 1), gOSMallocTag);
    }

    // Free newNode if we allocated it but didn't put it into the table.
    
    if (newNode != NULL) {
        FUSE_OSFree(newNode, sizeof(*newNode) + gFSNodeSize, gOSMallocTag); 
    }
    
    assert( (err == 0) == (*hnodePtr != NULL) );
    
    return err;
}

static void HNodeAttachComplete(HNodeRef hnode)
    // An attach operate has completed.  If there is someone waiting for 
    // the HNode, wake them up.
{
    assert(hnode != NULL);
    assert(hnode->magic == gMagic);
    
    LCK_MTX_ASSERT(gHashMutex, LCK_MTX_ASSERT_OWNED);

    assert(hnode->attachOutstanding);
    hnode->attachOutstanding = FALSE;

    if (hnode->waiting) {
        wakeup(hnode);
        hnode->waiting = FALSE;
    }
}

static boolean_t HNodeForkVNodeDecrement(HNodeRef hnode)
    // Decrement the number of fork vnodes for this HNode.  If it hits zero, 
    // the HNode is gone and we remove it from the hash table and return 
    // true indicating to our caller that they need to clean it up.
{
    boolean_t   scrubIt;

    assert(hnode != NULL);
    assert(hnode->magic == gMagic);

    LCK_MTX_ASSERT(gHashMutex, LCK_MTX_ASSERT_OWNED);

    scrubIt = FALSE;

    hnode->forkVNodesCount -= 1;
    assert(hnode->forkVNodesCount >= 0);
    if (hnode->forkVNodesCount == 0) {
        LIST_REMOVE(hnode, hashLink);
        assert(gHashNodeCount > 0);     // we test for this case before decrementing it because it's unsigned
        gHashNodeCount -= 1;

        scrubIt = TRUE;
    }

    return scrubIt;
}

extern void HNodeAttachVNodeSucceeded(HNodeRef hnode, size_t forkIndex, vnode_t vn)
    // See comments in header.
{
    errno_t     junk;
    
    lck_mtx_lock(gHashMutex);

    assert(hnode != NULL);
    assert(hnode->magic == gMagic);
    assert(forkIndex < hnode->forkVNodesSize);
    assert(vn != NULL);
    assert(vnode_fsnode(vn) == hnode);
    
    // If someone is waiting for the HNode, wake them up.  They won't actually 
    // start running until we drop gHashMutex.
    
    HNodeAttachComplete(hnode);

    // Record the vnode's association with this HNode.
    
    assert(hnode->forkVNodes[forkIndex] == NULL);
    hnode->forkVNodes[forkIndex] = vn;
    junk = vnode_addfsref(vn);
    assert(junk == 0);
    
    lck_mtx_unlock(gHashMutex);
}

extern boolean_t HNodeAttachVNodeFailed(HNodeRef hnode, __unused size_t forkIndex)
    // See comments in header.
{
    boolean_t   scrubIt;
    
    lck_mtx_lock(gHashMutex);

    assert(hnode != NULL);
    assert(hnode->magic == gMagic);
    assert(forkIndex < hnode->forkVNodesSize);
    
    // If someone is waiting for the HNode, wake them up.  They won't actually 
    // start running until we drop gHashMutex.
    
    HNodeAttachComplete(hnode);

    // Decrement the number of fork vnodes referencing the HNode, freeing 
    // the HNode if it hits zero.

    scrubIt = HNodeForkVNodeDecrement(hnode);

    lck_mtx_unlock(gHashMutex);

    return scrubIt;
}

extern boolean_t HNodeDetachVNode(HNodeRef hnode, vnode_t vn)
    // See comments in header.
{
    errno_t     junk;
    size_t      forkIndex;
    boolean_t   scrubIt;
    
    lck_mtx_lock(gHashMutex);

    assert(hnode != NULL);
    assert(hnode->magic == gMagic);
    assert(vn != NULL);

    // Find the fork index for vn.
    
    for (forkIndex = 0; forkIndex < hnode->forkVNodesSize; forkIndex++) {
        if (hnode->forkVNodes[forkIndex] == vn) {
            break;
        }
    }
    assert(forkIndex < hnode->forkVNodesSize);      // if this trips, vn isn't in the forkVNodes array

    // Disassociate the vnode with this fork of the HNode.
    
    hnode->forkVNodes[forkIndex] = NULL;
    junk = vnode_removefsref(vn);
    assert(junk == 0);
    vnode_clearfsnode(vn);
    
    // Decrement the number of fork vnodes referencing the HNode, 
    // freeing the HNode if it hits zero.

    scrubIt = HNodeForkVNodeDecrement(hnode);

    lck_mtx_unlock(gHashMutex);

    return scrubIt;
}

extern void HNodeScrubDone(HNodeRef hnode)
    // See comments in header.
{
    assert(hnode != NULL);
    assert(hnode->magic == gMagic);
    
    if (hnode->forkVNodesSize > 1) {
        FUSE_OSFree(hnode->forkVNodesStorage.external, sizeof(*hnode->forkVNodesStorage.external) * hnode->forkVNodesSize, gOSMallocTag);
    }

    // If anyone is waiting on this HNode, that would be bad.
    // It would be easy to fix this (we could wake them up at this 
    // point) but, as I don't think it can actually happen, I'd rather 
    // have this assert tell me whether the code is necessary than 
    // just add it blindly.

    assert( ! hnode->waiting );
    FUSE_OSFree(hnode, sizeof(*hnode) + gFSNodeSize, gOSMallocTag);
}

extern void HNodePrintState(void)
    // See comments in header.
    //
    // There's a significant thread safety bug here.  Specifically, the forkVNodes 
    // array is not included as part of the snapshot.  If we switch the HNode's 
    // forkVNodes array between the point where we take the snapshot at the point 
    // where we print it, bad things will happen.
{
    int     err;
    size_t  nodeCount;
    size_t  nodeIndex;
    HNode * nodes;
    u_long  hashBucketIndex;
    
    // Take a snapshot.
    
    do {
        err = 0;
        
        nodeCount = gHashNodeCount;
        
        nodes = FUSE_OSMalloc(sizeof(*nodes) * nodeCount, gOSMallocTag);
        if (nodes == NULL) {
            err = ENOMEM;
        }
        
        if (err == 0) {
            lck_mtx_lock(gHashMutex);
            
            if (gHashNodeCount > nodeCount) {
                // Whoops, it changed size, let's try again.
                FUSE_OSFree(nodes, sizeof(*nodes) * nodeCount, gOSMallocTag);
                err = EAGAIN;
            } else {
                nodeIndex = 0;
                for (hashBucketIndex = 0; hashBucketIndex < gHashTableMask; hashBucketIndex++) {
                    HNode *     thisNode;
                    
                    LIST_FOREACH(thisNode, &gHashTable[hashBucketIndex], hashLink) {
                        assert(nodeIndex < nodeCount);
                        
                        nodes[nodeIndex] = *thisNode;
                        nodeIndex += 1;
                    }
                }
                assert(nodeIndex == nodeCount);
            }
            
            lck_mtx_unlock(gHashMutex);
        }
    } while (err == EAGAIN);

    assert( (err == 0) == (nodes != NULL) );
    
    // Print the snapshot.
    
    if (err == 0) {
        printf("HNodePrintState\n");
        for (nodeIndex = 0; nodeIndex < nodeCount; nodeIndex++) {
            HNode *     thisNode;
            size_t      forkIndex;
            
            thisNode = &nodes[nodeIndex];
            
            printf("{%d.%lld %c%c ", thisNode->dev, thisNode->ino, " A"[thisNode->attachOutstanding], " W"[thisNode->waiting]);
            for (forkIndex = 0; forkIndex < thisNode->forkVNodesSize; forkIndex++) {
                if (forkIndex > 0) {
                    printf(" ");
                }
                printf("%p", thisNode->forkVNodes[forkIndex]);
            }
            printf("}");

            if (nodes[nodeIndex].hashLink.le_next == NULL) {
                printf("\n");
            }
        }
    }

    if (nodes != NULL) {
        FUSE_OSFree(nodes, sizeof(*nodes) * nodeCount, gOSMallocTag);
    }
}

// == XXX ==

extern errno_t
HNodeLookupRealQuickIfExists(dev_t     dev,
                             uint64_t  ino,
                             size_t    forkIndex,
                             HNodeRef *hnodePtr,
                             vnode_t  *vnPtr)
{
    errno_t   err = EAGAIN;
    HNodeRef  thisNode;
    boolean_t needsUnlock;
    vnode_t   resultVN;
    uint32_t  vid;
    
    assert(hnodePtr != NULL);
    assert(*hnodePtr == NULL);
    assert(vnPtr != NULL);
    assert(*vnPtr == NULL);
    assert(gHashMutex != NULL);

    needsUnlock = TRUE;
    resultVN = NULL;
    
    lck_mtx_lock(gHashMutex);
    
    LCK_MTX_ASSERT(gHashMutex, LCK_MTX_ASSERT_OWNED);

    thisNode = LIST_FIRST(HNodeGetFirstFromHashTable(dev, ino));

    while (thisNode != NULL) {
        assert(thisNode->magic == gMagic);
        if ((thisNode->dev == dev) && (thisNode->ino == ino)) {
            break;
        }
        thisNode = LIST_NEXT(thisNode, hashLink);
    }
        
    if (thisNode == NULL) {
        return ENOENT;
    } else {
        if (thisNode->attachOutstanding) {
            return EAGAIN;
        } else if (forkIndex >= thisNode->forkVNodesSize) {
            return ENOENT;
        } else if (thisNode->forkVNodes[forkIndex] == NULL) {
            return ENOENT;
        } else {
            vnode_t candidateVN = thisNode->forkVNodes[forkIndex];
            assert(candidateVN != NULL);
            vid = vnode_vid(candidateVN);
            lck_mtx_unlock(gHashMutex);
            err = vnode_getwithvid(candidateVN, vid);
            if (err == 0) {
                assert(thisNode != NULL);
                assert(resultVN == NULL);
                resultVN = candidateVN;
                needsUnlock = FALSE;
            } else {
                return EAGAIN;
            }
        }
    }

    if (err == 0) {
        *hnodePtr = thisNode;
        *vnPtr    = resultVN;
    }
    
    if (needsUnlock) {
        lck_mtx_unlock(gHashMutex);
    }

    assert((err == 0) == (*hnodePtr != NULL));
    
    return err;
}

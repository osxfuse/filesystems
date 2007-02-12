#ifndef _FUSE_NODEHASH_H_
#define _FUSE_NODEHASH_H_

#include <stdint.h>
#include <sys/systm.h>
#include <libkern/OSMalloc.h>

typedef struct HNode * HNodeRef;

extern errno_t HNodeInit(lck_grp_t   *lockGroup, 
                         lck_attr_t  *lockAttr, 
                         OSMallocTag  mallocTag, 
                         uint32_t     magic, 
                         size_t       fsNodeSize);
extern void HNodeTerm(void);

extern void *    FSNodeGenericFromHNode(HNodeRef hnode);
extern HNodeRef  HNodeFromFSNodeGeneric(void *fsNode);
extern HNodeRef  HNodeFromVNode(vnode_t vn);
extern void *    FSNodeGenericFromVNode(vnode_t vn);

extern dev_t     HNodeGetDevice(HNodeRef hnode);
extern uint64_t  HNodeGetInodeNumber(HNodeRef hnode);
extern vnode_t   HNodeGetVNodeForForkAtIndex(HNodeRef hnode, size_t forkIndex);
extern size_t    HNodeGetForkIndexForVNode(vnode_t vn);

extern errno_t   HNodeLookupRealQuickIfExists(dev_t     dev,
                                              uint64_t  ino,
                                              size_t    forkIndex,
                                              HNodeRef *hnodePtr,
                                              vnode_t  *vnPtr);
extern errno_t   HNodeLookupCreatingIfNecessary(dev_t     dev,
                                                uint64_t  ino,
                                                size_t    forkIndex,
                                                HNodeRef *hnodePtr,
                                                vnode_t  *vnPtr);
extern void      HNodeAttachVNodeSucceeded(HNodeRef hnode,
                                           size_t   forkIndex,
                                           vnode_t  vn);
extern boolean_t HNodeAttachVNodeFailed(HNodeRef hnode, size_t forkIndex);
extern boolean_t HNodeDetachVNode(HNodeRef hnode, vnode_t vn);
extern void      HNodeScrubDone(HNodeRef hnode);

void             HNodePrintState(void);

#endif /* _FUSE_NODEHASH_H_ */

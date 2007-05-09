/*
 * Copyright 2003 IBM
 * Source License: GNU GENERAL PUBLIC LICENSE (GPL)
 */

#ifndef _PROCFS_TPM_H_
#define _PROCFS_TPM_H_

#include <sys/types.h>
#include <tpmfunc.h>

uint32_t
TPM_GetCapability_Version(int *major, int *minor, int *version, int *rev);

uint32_t
TPM_GetCapability_Slots(uint32_t *slots);

uint32_t
TPM_GetCapability_Pcrs(uint32_t *pcrs);

uint32_t
TPM_GetCapability_Key_Handle(uint16_t *num, uint32_t keys[]);

#endif /* _PROCFS_TPM_H_ */

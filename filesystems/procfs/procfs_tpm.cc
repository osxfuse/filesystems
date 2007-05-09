/*
 * Copyright 2003 IBM
 * Source License: GNU GENERAL PUBLIC LICENSE (GPL)
 */

extern "C" {

#include "procfs_tpm.h"

uint32_t
TPM_GetCapability_Version(int *major, int *minor, int *version, int *rev)
{
	unsigned char blob[4096] = {
		0, 193,		/* TPM_TAG_RQU_COMMAND */
		0, 0, 0, 18,	/* blob length, bytes */
		0, 0, 0, 101,	/* TPM_ORD_GetCapability */
		0, 0, 0, 6,	/* TCPA_CAP_VERSION */
		0, 0, 0, 0	/* no sub capability */
	};
	uint32_t ret;
	ret = TPM_Transmit(blob, "TPM_GetCapability_Version");
	if (ret)
		return (ret);
	*major = (int) (blob[14]);
	*minor = (int) (blob[15]);
	*version = (int) (blob[16]);
	*rev = (int) (blob[17]);
	return (ret);
}

uint32_t
TPM_GetCapability_Slots(uint32_t * slots)
{
	unsigned char blob[4096] = {
		0, 193,		/* TPM_TAG_RQU_COMMAND */
		0, 0, 0, 22,	/* blob length, bytes */
		0, 0, 0, 101,	/* TPM_ORD_GetCapability */
		0, 0, 0, 5,	/* TCPA_CAP_PROPERTY */
		0, 0, 0, 4,	/* SUB_CAP size, bytes */
		0, 0, 1, 4	/* TCPA_CAP_PROP_SLOTS */
	};
	uint32_t ret;
	ret = TPM_Transmit(blob, "TPM_GetCapability_Slots");
	if (ret)
		return (ret);
	*slots = ntohl(*(uint32_t *) (blob + 14));
	return (ret);
}

uint32_t
TPM_GetCapability_Pcrs(uint32_t * pcrs)
{
	unsigned char blob[4096] = {
		0, 193,		/* TPM_TAG_RQU_COMMAND */
		0, 0, 0, 22,	/* blob length, bytes */
		0, 0, 0, 101,	/* TPM_ORD_GetCapability */
		0, 0, 0, 5,	/* TCPA_CAP_PROPERTY */
		0, 0, 0, 4,	/* SUB_CAP size, bytes */
		0, 0, 1, 1	/* TCPA_CAP_PROP_PCR */
	};
	uint32_t ret;
	ret = TPM_Transmit(blob, "TPM_GetCapability_Pcrs");
	if (ret)
		return (ret);
	*pcrs = ntohl(*(uint32_t *) (blob + 14));
	return (ret);
}

uint32_t
TPM_GetCapability_Key_Handle(uint16_t * num, uint32_t keys[])
{
	unsigned char blob[4096] = {
		0, 193,		/* TPM_TAG_RQU_COMMAND */
		0, 0, 0, 18,	/* blob length, bytes */
		0, 0, 0, 101,	/* TPM_ORD_GetCapability */
		0, 0, 0, 7,	/* TCPA_CAP_KEY_HANDLE */
		0, 0, 0, 0	/* no sub capability */
	};
	uint32_t ret;
	int i;
	ret = TPM_Transmit(blob, "TPM_GetCapability_Handle_List");
	if (ret)
		return (ret);
	*num = ntohs(*(uint16_t *) (blob + 14));
	for (i = 0; i < *num; i++)
		keys[i] = ntohl(*(uint32_t *) (blob + 16 + 4 * i));
	return (ret);
}

}

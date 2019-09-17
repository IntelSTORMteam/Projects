/*
 *  Intel provides this code “as-is” and disclaims all express and implied warranties, including without 
 *  limitation, the implied warranties of merchantability, fitness for a particular purpose, and non-infringement, 
 *  as well as any warranty arising from course of performance, course of dealing, or usage in trade. No license 
 *  (express or implied, by estoppel or otherwise) to any intellectual property rights is granted by Intel providing 
 *  this code.
 *  This code is preliminary, may contain errors and is subject to change without notice. 
 *  Intel technologies' features and benefits depend on system configuration and may require enabled hardware, 
 *  software or service activation. Performance varies depending on system configuration.  Any differences in your 
 *  system hardware, software or configuration may affect your actual performance.  No product or component can be 
 *  absolutely secure.
 *  Intel and the Intel logo are trademarks of Intel Corporation in the United States and other countries. 
 *  *Other names and brands may be claimed as the property of others.
 *  © Intel Corporation
 */

//
//  Licensed under the GPL v2
//

#include <stdint.h>

#pragma once

#define IDT_DBG_TRAPFAULT_IDX        0x01
#define IDT_PAGE_FAULT_IDX           0x0E

typedef union _KIDTENTRY64
{
	struct
	{
		USHORT OffsetLow;
		USHORT Selector;
		USHORT IstIndex : 3;
		USHORT Reserved0 : 5;
		USHORT Type : 5;
		USHORT Dpl : 2;
		USHORT Present : 1;
		USHORT OffsetMiddle;
		ULONG  OffsetHigh;
		ULONG  Reserved1;
	};

	ULONG64 Alignment;
} KIDTENTRY64, *PKIDTENTRY64;

#define KUSER_SHARED_PAGE               0xFFFFF78000000000
#define KUSER_MAX_FMT_STRING_LENGTH     0x30
#define KUSER_OUT_BUF_LENGTH            128

typedef struct _PATCH_INFO
{
	uint64_t  AreaAddress;
	uint64_t  HooksAddress;
	uint64_t  HooksSize;
	uint64_t  KiPageFault;
	uint64_t  KiPageFaultHookAddress;
	uint64_t  KiPageFaultHookSize;
	uint64_t  KiDebugTrapOrFault;
	uint64_t  KiDebugTrapOrFaultHookAddress;
	uint64_t  KiDebugTrapOrFaultHookSize;
	uint64_t  InfoAddress;
	uint64_t  FmtStringAddress;
	uint64_t  vsnprintf_s;
	uint64_t  DbgPrintEx;
	uint64_t  MmPteBase;
	uint64_t  FilteredModuleBase;
	uint64_t  FilteredModuleSize;
	uint64_t  FilteredModuleEnd;
	uint64_t  CurrentCpuOffset;
	uint64_t  CpuCount;
} PATCH_INFO, *PPATCH_INFO;

typedef struct _SMAP_EVENT_INFO_ENTRY
{
	uint64_t  FaultingAddress;
	uint64_t  TrapRip;
	uint64_t  Tsc;
	uint64_t  ErrorCode;
	uint64_t  TrapCr3;
	uint64_t  TrapRsp;
	uint64_t  TrapFlags;
	uint64_t  ResumeFlag;
	char      OutputBuffer[KUSER_OUT_BUF_LENGTH];
} SMAP_EVENT_INFO_ENTRY, *PSMAP_EVENT_INFO_ENTRY;

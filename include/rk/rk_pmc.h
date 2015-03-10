/*
 * Real-Time and Multimedia Systems Laboratory
 * Copyright (c) 2000-2013 Carnegie Mellon University
 * All Rights Reserved.
 * 
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 * 
 *  Real-Time and Multimedia Systems Laboratory
 *  Attn: Prof. Raj Rajkumar
 *  Electrical and Computer Engineering, and Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 *  or via email to raj@ece.cmu.edu
 * 
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 */

/* 
 * rk_pmc.h 
 * - Hardware Performance Counter
 */ 

#ifndef RK_PMC_H
#define RK_PMC_H

// Intel Performance Counter (for Sandy Bridge) 

// architectural on-core events
#define ARCH_LLC_REFERENCE_EVTNR        (0x2E)
#define ARCH_LLC_REFERENCE_UMASK        (0x4F)
#define ARCH_LLC_MISS_EVTNR     (0x2E)
#define ARCH_LLC_MISS_UMASK     (0x41)

// Sandy Bridge on-core events
#define MEM_LOAD_UOPS_MISC_RETIRED_LLC_MISS_EVTNR (0xD4)
#define MEM_LOAD_UOPS_MISC_RETIRED_LLC_MISS_UMASK (0x02)

#define MEM_LOAD_UOPS_LLC_HIT_RETIRED_EVTNR (0xD2)
#define MEM_LOAD_UOPS_LLC_HIT_RETIRED_XSNP_NONE_UMASK (0x08)
#define MEM_LOAD_UOPS_LLC_HIT_RETIRED_XSNP_HITM_UMASK (0x04)
//#define MEM_LOAD_UOPS_LLC_HIT_RETIRED_L3_UMASK (0x08 | 0x04)
#define MEM_LOAD_UOPS_LLC_HIT_RETIRED_L3_UMASK (0x08 | 0x04 | 0x02)

#define MEM_LOAD_UOPS_RETIRED_EVTNR (0xD1)
#define MEM_LOAD_UOPS_RETIRED_L1_HIT_UMASK (0x01)
#define MEM_LOAD_UOPS_RETIRED_L2_HIT_UMASK (0x02)
#define MEM_LOAD_UOPS_RETIRED_L3_HIT_UMASK (0x04) // excluding HitM

#define MEM_UOPS_RETIRED_EVTNR (0xD0)
#define MEM_UOPS_RETIRED_ALL_LOAD_UMASK (0x81)
#define MEM_UOPS_RETIRED_ALL_LOAD_STORE_UMASK (0x83)

#define UNC_QMC_NORMAL_READ_EVTNR (0x2C)
#define UNC_QMC_NORMAL_READ_ANY_UMASK (0x07)
#define UNC_QMC_WRITES_FULL_EVTNR (0x2F)
#define UNC_QMC_WRITES_FULL_ANY_UMASK (0x07)

#if !defined(_LINUX_TYPES_H) && !defined(_STDINT_H)
typedef int int32_t;
typedef unsigned long long uint64_t;
#endif

struct core_event_desc {
	int32_t event_number, umask_value;
};
struct fixed_event_ctrl_reg {
	union {   
		struct {
			// CTR0
			uint64_t os0 : 1;
			uint64_t usr0 : 1;
			uint64_t any_thread0 : 1;
			uint64_t enable_pmi0 : 1;
			// CTR1
			uint64_t os1 : 1;
			uint64_t usr1 : 1;
			uint64_t any_thread1 : 1;
			uint64_t enable_pmi1 : 1;
			// CTR2
			uint64_t os2 : 1;
			uint64_t usr2 : 1;
			uint64_t any_thread2 : 1;
			uint64_t enable_pmi2 : 1;

			uint64_t reserved1 : 52;
		} fields;
		uint64_t value;
	};
};
struct event_select_reg {         
	union {     
		struct { 
			uint64_t event_select : 8;
			uint64_t umask : 8;
			uint64_t usr : 1;
			uint64_t os : 1;
			uint64_t edge : 1;
			uint64_t pin_control : 1;
			uint64_t apic_int : 1;
			uint64_t any_thread : 1;
			uint64_t enable : 1;
			uint64_t invert : 1;
			uint64_t cmask : 8;
			uint64_t reserved1 : 32;
		} fields;
		uint64_t value;
	};
};

struct pmc_counter {
	uint64_t inst_retired_any;
	uint64_t cpu_clk_unhalted;
	uint64_t cpu_clk_unhalted_ref;

	uint64_t l1_hit;
	uint64_t l2_hit;
	uint64_t l3_hit;
	uint64_t l3_miss;
	uint64_t invariant_tsc;
};

void get_pmc_info(struct pmc_counter *pmc);

#endif /* RK_PMC_H */


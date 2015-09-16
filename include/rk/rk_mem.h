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
 * rk_mem.h 
 * - Memory reservation configuration
 */ 

#ifndef RK_MEM_H
#define RK_MEM_H

#ifdef CONFIG_RK_MEM

// Memory pool configuration: mem-pool size and cache/bank coloring
// - When the RK module is loaded, it creates a global memory pool having 
//   free physical pages to be used for memory reservations.
// - As memory reservations are created by using the free pages of the 
//   memory pool, the total size of memory reservations cannot exceed
//   the size of the memory pool (MEM_RSV_DEFAULT_POOL_SIZE).
// - If MEM_RSV_DEFAULT_POOL_SIZE is 0, the memory pool is not created.

#if defined(RK_X86_SANDYBRIDGE)
	// Bank coloring configuration
	//
	#if 0
	// Intel Sandy Bridge i7-2600 + Single DIMM 8GB (2 Ranks)
	// : 16 bank colors
	//#define MEM_RSV_DEFAULT_POOL_SIZE	(7 * 1024) // MBytes
	//#define MEM_RSV_DEFAULT_POOL_SIZE	(6 * 1024) // MBytes
	#define MEM_RSV_BANK_COLORS	16
	#define MEM_RSV_BANK_COLORIDX(p)			\
	({							\
		unsigned long pfn = page_to_pfn(p) << 12;	\
		(((pfn >> 17) ^ (pfn >> 13)) & 1)		\
		| ((((pfn >> 18) ^ (pfn >> 14)) & 1) << 1)	\
		| ((((pfn >> 19) ^ (pfn >> 15)) & 1) << 2)	\
		| ((((pfn >> 20) ^ (pfn >> 16)) & 1) << 3);	\
	})
	#endif
	#if 0
	// Intel Sandy Bridge i7-2600 + 8GB (4 x 2GB Single-rank DIMMs)
	// : 16 bank colors
	#define MEM_RSV_DEFAULT_POOL_SIZE	(6 * 1024) // MBytes
	#define MEM_RSV_BANK_COLORS	16
	#define MEM_RSV_BANK_COLORIDX(p)			\
	({							\
		unsigned long pfn = page_to_pfn(p) << 12;	\
		(((pfn >> 18) ^ (pfn >> 14)) & 1)		\
		| ((((pfn >> 19) ^ (pfn >> 15)) & 1) << 1)	\
		| (((              (pfn >> 16)) & 1) << 2)	\
		| ((((pfn >> 20) ^ (pfn >> 17)) & 1) << 3);	\
	})
	#endif

#elif defined(RK_X86_YORKFIELD)
	// Bank coloring configuration
	//
	// Intel Core 2 Quad (Yorkfield) Q9700 + Thinkpad 4GB RAM
	// : 64 cache colors and 16 bank colors
	//#define MEM_RSV_DEFAULT_POOL_SIZE	(2 * 1024) // MBytes
	//#define RK_ARCH_LLC_SIZE	(3 * 1024) // 2 x 3MB L2 Caches
	//#define RK_ARCH_LLC_WAYS	(12)
	#define MEM_RSV_BANK_COLORS	16
	#define MEM_RSV_BANK_COLORIDX(p)			\
	({							\
		unsigned long pfn = page_to_pfn(p) << 12;	\
		(((pfn >> 18) ^ (pfn >> 14)) & 1)		\
		| ((((pfn >> 20) ^ (pfn >> 15)) & 1) << 1)	\
		| ((((pfn >> 19) ^ (pfn >> 16)) & 1) << 2)	\
		| ((((pfn >> 21) ^ (pfn >> 17)) & 1) << 3);	\
	})

#elif defined (RK_ARM_EXYNOS)
	// Cache coloring configuration
	//
	// Samsung Exynos 4412 processor + 1GB RAM
	#define MEM_RSV_DEFAULT_POOL_SIZE	512 // MBytes
	#define RK_ARCH_LLC_SIZE	(1 * 1024) // 1024KB L2 Cache
	#define RK_ARCH_LLC_WAYS	(16)
        /*
	#define MEM_RSV_BANK_COLORS	8
	#define MEM_RSV_BANK_COLORIDX(p)			\
	({							\
		unsigned long pfn = page_to_pfn(p) << 12;	\
		((pfn >> 13) & 1)		\
		| (((pfn >> 14) & 1) << 1)	\
		| (((pfn >> 15) & 1) << 2);	\
	})
        */

#elif defined (RK_ARM_iMX6)
	// Cache coloring configuration
	//
	// Freescale iMX6 Quad processor + 1GB RAM
	#define MEM_RSV_DEFAULT_POOL_SIZE	256 // MBytes
	#define RK_ARCH_LLC_SIZE	(1 * 1024) // 1024KB L2 Cache
	#define RK_ARCH_LLC_WAYS	(16)
        /*
	#define MEM_RSV_BANK_COLORS	8
	#define MEM_RSV_BANK_COLORIDX(p)			\
	({							\
		unsigned long pfn = page_to_pfn(p) << 12;	\
		((pfn >> 13) & 1)		\
		| (((pfn >> 14) & 1) << 1)	\
		| (((pfn >> 15) & 1) << 2);	\
	})
        */
#endif


#ifndef MEM_RSV_DEFAULT_POOL_SIZE
	#define MEM_RSV_DEFAULT_POOL_SIZE	0 // MBytes
#endif

#ifndef RK_ARCH_LLC_SIZE
	#define RK_ARCH_LLC_SIZE	0
#endif
#ifndef RK_ARCH_LLC_WAYS 
	#define RK_ARCH_LLC_WAYS	1
#endif

extern int mem_rsv_cache_colors;
#define MEM_RSV_COLORS		(mem_rsv_cache_colors)
#define MEM_RSV_COLORIDX(pg)	(int)((page_to_pfn(pg)) & (MEM_RSV_COLORS - 1))

#ifndef MEM_RSV_BANK_COLORS
	#define MEM_RSV_BANK_COLORS		1
	#define MEM_RSV_BANK_COLORIDX(p)	0
#endif

#endif
#endif /* RK_MEM_H */


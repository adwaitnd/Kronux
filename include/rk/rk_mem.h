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

// Memory pool configuration: size and cache/bank coloring
// - When the RK module is loaded, it creates a global memory pool having 
//   free physical pages to be used for memory reservations.
// - As memory reservations are created by using the free pages of the 
//   memory pool, the total size of memory reservations cannot exceed
//   the size of the memory pool (MEM_RSV_TOTAL_SIZE).
// - If MEM_RSV_TOTAL_SIZE is 0, the memory pool is not created.
#ifdef RK_MEM_COLORING
#if defined(RK_X86_SANDYBRIDGE)
	#if 1
	// Intel Sandy Bridge i7-2600 + Single DIMM 8GB (2 Ranks)
	// : 32 cache colors and 16 bank colors
	//#define MEM_RSV_TOTAL_SIZE	(7 * 1024 * 1024 * 1024LL)
	//#define MEM_RSV_TOTAL_SIZE	(6 * 1024 * 1024 * 1024LL)
	//#define MEM_RSV_TOTAL_SIZE	(1 * 1024 * 1024 * 1024LL)
	#define MEM_RSV_TOTAL_SIZE	0
	#define RK_ARCH_LLC_SIZE	(8 * 1024 * 1024 / 4) 
	#define RK_ARCH_LLC_WAYS	(16)
	#define RK_ARCH_LLC_CACHELINE	(64) // Not used
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
	// : 32 cache colors and 16 bank colors
	#define MEM_RSV_TOTAL_SIZE	(6 * 1024 * 1024 * 1024LL)
	#define RK_ARCH_LLC_SIZE	(8 * 1024 * 1024 / 4) 
	#define RK_ARCH_LLC_WAYS	(16)
	#define RK_ARCH_LLC_CACHELINE	(64) // Not used
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
	#if 0
	// Intel Sandy Bridge i5-2540M: 32 cache colors
	#define MEM_RSV_TOTAL_SIZE	(2 * 1024 * 1024 * 1024LL)
	#define RK_ARCH_LLC_SIZE	(3 * 1024 * 1024 / 2)
	#define RK_ARCH_LLC_WAYS	(12)
	#define RK_ARCH_LLC_CACHELINE	(64)
	#endif

#elif defined(RK_X86_YORKFIELD)
	// Intel Core 2 Quad (Yorkfield) Q9700 + Thinkpad 4GB RAM
	// : 64 cache colors and 16 bank colors
	#define MEM_RSV_TOTAL_SIZE	(2 * 1024 * 1024 * 1024LL)
	#define RK_ARCH_LLC_SIZE	(3 * 1024 * 1024) // 2 x 3MB L2 Caches
	#define RK_ARCH_LLC_WAYS	(12)
	#define RK_ARCH_LLC_CACHELINE	(64) // Not used
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
	// Samsung Exynos 4412 processor + 1GB RAM
	#define MEM_RSV_TOTAL_SIZE	(512 * 1024 * 1024LL)
	#define RK_ARCH_LLC_SIZE	(1 * 1024 * 1024) // 1MB L2 Cache
	#define RK_ARCH_LLC_WAYS	(16)
	#define RK_ARCH_LLC_CACHELINE	(32) // Not used
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
	// Freescale iMX6 Quad processor + 1GB RAM
	#define MEM_RSV_TOTAL_SIZE	(256 * 1024 * 1024LL)
	#define RK_ARCH_LLC_SIZE	(1 * 1024 * 1024) // 1MB L2 Cache
	#define RK_ARCH_LLC_WAYS	(16)
	#define RK_ARCH_LLC_CACHELINE	(32) // Not used
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

#endif // RK_MEM_COLORING

#ifndef MEM_RSV_TOTAL_SIZE
	#define MEM_RSV_TOTAL_SIZE	(1024 * 1024 * 1024LL) // Default rsv size
	//#define MEM_RSV_TOTAL_SIZE	0
#endif

#ifndef RK_ARCH_LLC_SIZE
	#define MEM_RSV_COLORS		1
	#define MEM_RSV_COLORIDX(pg)	0
#else
	#define MEM_RSV_COLORS		(int)((RK_ARCH_LLC_SIZE / RK_ARCH_LLC_WAYS) / PAGE_SIZE)
	#define MEM_RSV_COLORIDX(pg)	(int)((page_to_pfn(pg)) & (MEM_RSV_COLORS - 1))
#endif

#ifndef MEM_RSV_BANK_COLORS
	#define MEM_RSV_BANK_COLORS		1
	#define MEM_RSV_BANK_COLORIDX(p)	0
#endif


#endif
#endif /* RK_MEM_H */


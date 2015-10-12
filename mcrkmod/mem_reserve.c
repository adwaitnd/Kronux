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
 * mem_reserve.c: code to manage memory reservations
 *
 * Current Assumptions/Limitations
 * - RK Resource set applies to process-level, not to individual threads 
 */

#include <rk/rk_mc.h>
#include <rk/rk_mem.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/rmap.h>
#include <linux/mempolicy.h>
#include <linux/moduleparam.h>

#ifdef CONFIG_RK_MEM

static int mem_reserve_read_proc(rk_reserve_t rsv, char *buf);
struct rk_reserve_ops mem_reserve_ops = {
	mem_reserve_read_proc,
};

// Kernel memory management functions
int isolate_lru_page(struct page *page);
void putback_lru_page(struct page *page);
int page_evictable(struct page *page);
int page_referenced(struct page *page, int is_locked,
				  struct mem_cgroup *cnt,
				  unsigned long *vm_flags);
int try_to_unmap(struct page *page, enum ttu_flags flags);

// RK memory reservation function declarations
int rk_page_list_out(struct zone* zone, struct list_head *page_list, int n);
int evict_reserved_pages(mem_reserve_t mem, int n_pages);
int rk_migrate_page(struct page *from, struct page *to);
int sys_rk_mem_reserve_show_color_info(int color_idx);
static inline void __free_page_to_pagebins(struct page *page);
void mem_reserves_cleanup(void);


//#define VERBOSE_MEM_RSV
#ifdef VERBOSE_MEM_RSV
	#define mem_dbg(...) printk(__VA_ARGS__)
#else
	#define mem_dbg(...)
#endif

// Memory pool configuration: # of pages
#define MEM_RSV_MAX_PAGES	((mem_size * 1024 * 1024LL) / PAGE_SIZE)

// Swapping parameters
#define MEM_LOW_WATERMARK	0
#define PF_LOOK_BACK_WINDOW	1000
#define MEM_RSV_EVICT_SIZE	128

LIST_HEAD(mem_reserves_head);
int mem_reserve_max_capacity; // Maximum memory pool size in pages
int mem_reserve_usage = 0; // Current memory usage in pages
raw_spinlock_t mem_reserve_lock;

struct list_head **memrsv_pagebins = NULL;
int **memrsv_pagebins_counter = NULL;
int mem_rsv_cache_colors;

// Currently, we do not use local lock (We just use a global lock for simplicity)
//#ifdef MEM_RSV_LOCAL_LOCK 
#ifdef MEM_RSV_LOCAL_LOCK
#define MEM_LOCK(a) raw_spin_lock(a)
#define MEM_UNLOCK(a) raw_spin_unlock(a)
#else
#define MEM_LOCK(a) 
#define MEM_UNLOCK(a)
#endif

enum {
	PAGE_PRIVATE_DATA,
	PAGE_PRIVATE_TEXT,
	PAGE_SHARED_DATA,
	PAGE_SHARED_TEXT,
	// number of page categories
	PAGE_NR_CATEGORY,
};

const char category_str[][10]={
	"P-Data",
	"P-Text",
	"S-Data",
	"S-Text",
};

//#define RSV_NO_SHARED_MEM
//#define RSV_NO_PAGE_CACHE
//#define RSV_NO_SHARED_PAGE_CONSERVATION


// Module parameters for memory reservation
static int mem_size = MEM_RSV_DEFAULT_POOL_SIZE; // Maximum memory pool size in MBytes
module_param(mem_size, int, 0644);
MODULE_PARM_DESC(mem_size, "Memory pool size for memrsv (in Mbytes, default = " STR(MEM_RSV_DEFAULT_POOL_SIZE) ")");


// Init cache coloring information
#define CACHE_PATH_PREFIX "/sys/devices/system/cpu/cpu0/cache/index"
#define FNAME_MAX 256
int llc_level, llc_ways, llc_size;
void init_cache_coloring_info(void)
{
	struct file *f = NULL;
	mm_segment_t oldfs;
	int ret;
	int level, cache_ways, cache_size;
	char buf[FNAME_MAX];


	// Default LLC value
	llc_level = 0;
	llc_size = RK_ARCH_LLC_SIZE;
	llc_ways = RK_ARCH_LLC_WAYS;

	oldfs = get_fs();
	set_fs(KERNEL_DS);
	for (level = llc_level; ; level++) {
		sprintf(buf, CACHE_PATH_PREFIX "%d/size", level);
		f = filp_open(buf, O_RDONLY, 0);
		if (IS_ERR(f)) break;
		ret = f->f_op->read(f, buf, 10, &f->f_pos);
		filp_close(f, NULL);
		if (ret <= 0) break;
		sscanf(buf, "%dK", &cache_size);

		sprintf(buf, CACHE_PATH_PREFIX "%d/ways_of_associativity", level);
		f = filp_open(buf, O_RDONLY, 0);
		if (IS_ERR(f)) break;		
		ret = f->f_op->read(f, buf, 10, &f->f_pos);
		filp_close(f, NULL);
		if (ret <= 0) break;
		sscanf(buf, "%d", &cache_ways);

		llc_level = level;
		llc_size = cache_size;
		llc_ways = cache_ways;
	}
	set_fs(oldfs);

	// The shared LLC (last-level cache) of Intel Sandy Bridge or newer 
	// architectures consists of multiple cache slices, the number of 
	// which is the same as that of physical cores [1, 2]. 
	// Cache coloring on such architectures can be implemented per 
	// cache-slice basis [3]. 
	//   
	// References:
	// [1] O.Lempel, "2nd Generation Intel Core Processor Family: 
	//     Intel Core i7, i5 and i3", Hot Chips, 2011.
	// [2] P.Hammarlund, "4th Generation Intel Core Processor, codenamed
	//     Haswell", Hot Chips, 2013.
	// [3] H.Kim et al., "A Coordinated Approach for Practical OS-Level 
	//     Cache Management in Multi-Core Real-Time Systems", ECRTS, 2013.
#ifdef CONFIG_X86
	{
		struct cpuinfo_x86 *c = &cpu_data(0);
		if (!strcmp(c->x86_vendor_id, "GenuineIntel") 
			&& c->x86 == 6 && c->x86_model >= 42) {
			llc_size /= c->booted_cores;
			printk("Intel SandyBridge or newer architecture: LLC slice size = %dK\n", llc_size);
		}
	}
#endif

	MEM_RSV_COLORS = (llc_size * 1024 / llc_ways / PAGE_SIZE);
	if (MEM_RSV_COLORS == 0) MEM_RSV_COLORS = 1;

	if (MEM_RSV_COLORS > RK_MEM_MAX_COLOR) {
		printk("RK: Warning: Too small RK_MEM_MAX_COLOR. System color %d is reduced to %d\n", MEM_RSV_COLORS, RK_MEM_MAX_COLOR);
		MEM_RSV_COLORS = RK_MEM_MAX_COLOR;
	}
}

// for procfs
int rk_mempool_read_proc(char *buf)
{
	char *p = buf;
	
	p += sprintf(p, "page_size: %lu\n", PAGE_SIZE);
	p += sprintf(p, "max_capacity: %d\n", mem_reserve_max_capacity);
	p += sprintf(p, "cur_usage: %d\n", mem_reserve_usage);
	p += sprintf(p, "llc_level: %d\n", llc_level);
	p += sprintf(p, "llc_size: %d\n", llc_size);
	p += sprintf(p, "cache_colors: %d\n", MEM_RSV_COLORS);
	p += sprintf(p, "bank_colors: %d\n", MEM_RSV_BANK_COLORS);

	return (p - buf);
}

void mem_reserves_init(void)
{
	int i, j;

	INIT_LIST_HEAD(&mem_reserves_head);
	mem_reserve_max_capacity = MEM_RSV_MAX_PAGES; 
	mem_reserve_usage = 0;
	raw_spin_lock_init(&mem_reserve_lock);
	
	// Initialize cache coloring info
	init_cache_coloring_info();

	// Preallocating pages from global memory management
	memrsv_pagebins = kmalloc(sizeof(struct list_head*) * MEM_RSV_COLORS, GFP_ATOMIC);
	memrsv_pagebins_counter = kmalloc(sizeof(int*) * MEM_RSV_COLORS, GFP_ATOMIC);
	for (i = 0; i < MEM_RSV_COLORS; i++) {
		memrsv_pagebins[i] = kmalloc(sizeof(struct list_head) * MEM_RSV_BANK_COLORS, GFP_ATOMIC);
		memrsv_pagebins_counter[i] = kmalloc(sizeof(int) * MEM_RSV_BANK_COLORS, GFP_ATOMIC);
		for (j = 0; j < MEM_RSV_BANK_COLORS; j++) {
			INIT_LIST_HEAD(&memrsv_pagebins[i][j]);
			memrsv_pagebins_counter[i][j] = 0;		
		}
	}
	for (i = 0; i < mem_reserve_max_capacity; i++) {
		struct page *page = alloc_page(GFP_HIGHUSER_MOVABLE);
		if (!page) {
			// FAIL: dealloc and return
			printk("mem_reserves_init: Failed to allocate page for page entry pool\n");
			mem_reserves_cleanup();
			return;
		}

		SetPageMemReserve(page); // no need to page_lock()
		__free_page_to_pagebins(page);
	}
	printk("Mem Reserve : %d cache colors / %d bank colors\n", MEM_RSV_COLORS, MEM_RSV_BANK_COLORS);
	sys_rk_mem_reserve_show_color_info(-1);
}

void mem_reserves_cleanup(void)
{
	struct page *page, *tmp;
	int i, j;
	if (!memrsv_pagebins || !memrsv_pagebins_counter) return;
	for (i = 0; i < MEM_RSV_COLORS; i++) {
		for (j = 0; j < MEM_RSV_BANK_COLORS; j++) {
			list_for_each_entry_safe(page, tmp, &memrsv_pagebins[i][j], lru) {
				ClearPageMemReserve(page);
				ClearPageEvictionLock(page);
				page->rsv = NULL;

				__free_page(page);
			}
		}
		kfree(memrsv_pagebins[i]);
		kfree(memrsv_pagebins_counter[i]);
	}
	kfree(memrsv_pagebins);
	kfree(memrsv_pagebins_counter);
	memrsv_pagebins = NULL;
	memrsv_pagebins_counter = NULL;
}

static inline void __free_page_to_pagebins(struct page *page)
{
	int idx, bank_idx;
	if (!page) return;

	idx = MEM_RSV_COLORIDX(page);
	bank_idx = MEM_RSV_BANK_COLORIDX(page);
	list_add_tail(&page->lru, &memrsv_pagebins[idx][bank_idx]);
	memrsv_pagebins_counter[idx][bank_idx]++;
}

void free_page_to_pagebins(struct page *page)
{
	raw_spin_lock(&mem_reserve_lock);
	__free_page_to_pagebins(page);
	raw_spin_unlock(&mem_reserve_lock);
}

static inline struct page* __get_page_from_pagebins(int idx, int bank_idx)
{
	struct page *ret = NULL;
	if (idx < 0 || idx >= MEM_RSV_COLORS) return NULL;
	if (bank_idx < 0 || bank_idx >= MEM_RSV_BANK_COLORS) return NULL;
	if (memrsv_pagebins_counter[idx][bank_idx] <= 0) return NULL;

	ret = list_first_entry(&memrsv_pagebins[idx][bank_idx], struct page, lru);
		
	list_del(&ret->lru);
	memrsv_pagebins_counter[idx][bank_idx]--;
	return ret;
}

int is_nr_pages_in_pagebins(mem_reserve_attr_t attr, int nr_pages)
{
	int total = 0, i, j;
	raw_spin_lock(&mem_reserve_lock);
	for (i = 0; i < attr->nr_colors; i++) {
		for (j = 0; j < attr->nr_bank_colors; j++) {
			total += memrsv_pagebins_counter[attr->colors[i]][attr->bank_colors[j]];
		}
		if (total > nr_pages) {
			raw_spin_unlock(&mem_reserve_lock);
			return TRUE;
		}
	}
	raw_spin_unlock(&mem_reserve_lock);
	return FALSE;
}

// 'mem' should be inactive, or 
// 'entry' should be detached from free/used (active or inactive) list
struct page* alloc_page_from_pagebins(mem_reserve_t mem, struct mem_reserve_page *entry)
{
	mem_reserve_attr_t attr;
	struct page *page = NULL;
	int i, entry_idx;

	if (!mem || !entry) return NULL;

	// Check which attr should be used
	entry_idx = entry - &mem->reserved_pages[0];
	if (entry_idx < mem->mem_total_size - mem->mem_aux_size)
		attr = &mem->mem_res_attr;
	else 
		attr = &mem->aux_res_attr;

	raw_spin_lock(&mem_reserve_lock);
	for (i = 0; i < attr->nr_colors * attr->nr_bank_colors; i++) {
		page = __get_page_from_pagebins(attr->colors[mem->next_color_from_pagebins], 
			attr->bank_colors[mem->next_bank_color_from_pagebins]);
		if (++(mem->next_color_from_pagebins) >= attr->nr_colors) {
			mem->next_color_from_pagebins = 0;
			if (++(mem->next_bank_color_from_pagebins) >= attr->nr_bank_colors) 
				mem->next_bank_color_from_pagebins = 0;
		}
		if (page) break;
	}
	raw_spin_unlock(&mem_reserve_lock);
	if (!page) {
		page = alloc_page(GFP_HIGHUSER_MOVABLE);
		printk(KERN_ALERT "*** No color ***\n");
	}
	SetPageMemReserve(page); // no need to page_lock()
	entry->page = page;
	return page;
}

void set_reserve_hot_page(struct page *page)
{
	lock_page(page);
	SetPageMemReserve(page);
	unlock_page(page);
	if (!isolate_lru_page(page)) 
		putback_lru_page(page);
}

void clear_reserve_hot_page(struct page *page)
{
	lock_page(page);
	ClearPageMemReserve(page);
	ClearPageEvictionLock(page);
	unlock_page(page);
	if (!isolate_lru_page(page))
		putback_lru_page(page);
}

int page_category(struct mem_reserve_page *entry)
{
	// shared?
	if (page_mapcount(entry->page) > 1) {
		if (entry->executable) return PAGE_SHARED_TEXT;
		return PAGE_SHARED_DATA;
	}
	// private data or text
	if (entry->executable) return PAGE_PRIVATE_TEXT;
	return PAGE_PRIVATE_DATA;
}

// The caller needs to hold mem_list_lock 
void move_to_mem_used_list(struct mem_reserve_page *entry, mem_reserve_t mem) 
{
	int idx, bank_idx;
	idx = MEM_RSV_COLORIDX(entry->page);
	bank_idx = MEM_RSV_BANK_COLORIDX(entry->page);

	list_move_tail(&entry->list, &mem->mem_active_list);
	mem->mem_free_size--;
	mem->mem_free_size_detail[idx][bank_idx]--;
	mem->mem_used_size++;
	mem->mem_active_size++;
	if (mem->mem_used_size > mem->mem_peak_size) mem->mem_peak_size = mem->mem_used_size;

	entry->active_used = 1;
	entry->access_count = 1;
}
// The caller needs to hold mem_list_lock 
void add_page_to_mem_used_list(struct mem_reserve_page *entry, mem_reserve_t mem) 
{
	list_add_tail(&entry->list, &mem->mem_active_list);
	mem->mem_used_size++;
	mem->mem_active_size++;
	if (mem->mem_used_size > mem->mem_peak_size) mem->mem_peak_size = mem->mem_used_size;

	entry->active_used = 1;
	entry->access_count = 1;
}
// The caller needs to hold mem_list_lock 
void move_to_mem_free_list(struct mem_reserve_page *entry, mem_reserve_t mem) 
{
	int idx, bank_idx;
	idx = MEM_RSV_COLORIDX(entry->page);
	bank_idx = MEM_RSV_BANK_COLORIDX(entry->page);

	list_move_tail(&entry->list, &mem->mem_free_list[idx][bank_idx]);
	mem->mem_free_size++;
	mem->mem_free_size_detail[idx][bank_idx]++;
	mem->mem_used_size--;

	if (entry->active_used) mem->mem_active_size--;
	else mem->mem_inactive_size--;

	entry->active_used = 0;
	entry->executable = false;
	entry->access_count = 0;
}
// The caller needs to hold mem_list_lock 
void add_page_to_mem_free_list(struct mem_reserve_page *entry, mem_reserve_t mem)
{
	int idx, bank_idx;
	idx = MEM_RSV_COLORIDX(entry->page);
	bank_idx = MEM_RSV_BANK_COLORIDX(entry->page);

	list_add_tail(&entry->list, &mem->mem_free_list[idx][bank_idx]);
	mem->mem_free_size++;
	mem->mem_free_size_detail[idx][bank_idx]++;

	entry->active_used = 0;
	entry->executable = false;
	entry->access_count = 0;
}
// The caller needs to hold mem_list_lock 
// - Caller also needs to make sure that mem_free_size > 0
void del_page_from_mem_free_list(struct mem_reserve_page *entry, mem_reserve_t mem)
{
	int idx, bank_idx;
	idx = MEM_RSV_COLORIDX(entry->page);
	bank_idx = MEM_RSV_BANK_COLORIDX(entry->page);

	list_del(&entry->list);
	mem->mem_free_size--;
	mem->mem_free_size_detail[idx][bank_idx]--;
}
// The caller needs to hold mem_list_lock 
static inline struct mem_reserve_page* __page_in_mem_free_list(mem_reserve_t mem, int idx, int bank_idx)
{
	if (mem->mem_free_size_detail[idx][bank_idx] <= 0) return NULL;
	return list_first_entry(&mem->mem_free_list[idx][bank_idx], struct mem_reserve_page, list);
}
// The caller needs to hold mem_list_lock 
// - Caller also needs to make sure that mem_free_size > 0
struct mem_reserve_page* get_page_from_mem_free_list(mem_reserve_t mem)
{
	mem_reserve_attr_t attr;
	struct mem_reserve_page *entry = NULL;
	int idx, bank_idx, i;
	attr = &mem->mem_res_attr;
	for (i = 0; i < attr->nr_colors * attr->nr_bank_colors; i++) {
		idx = attr->colors[mem->next_color_to_tasks];
		bank_idx = attr->bank_colors[mem->next_bank_color_to_tasks];
		entry = __page_in_mem_free_list(mem, idx, bank_idx);
		if (++(mem->next_color_to_tasks) >= attr->nr_colors) {
			mem->next_color_to_tasks = 0;
			if (++(mem->next_bank_color_to_tasks) >= attr->nr_bank_colors) 
				mem->next_bank_color_to_tasks = 0;
		}
		if (entry) break;
	}
	if (entry) {
		list_del(&entry->list);
		mem->mem_free_size--;
		mem->mem_free_size_detail[idx][bank_idx]--;
		return entry;
	}
	// Search if there is any page with other colors in the memrsv
	for (idx = 0; idx < MEM_RSV_COLORS; idx++) {
		for (bank_idx = 0; bank_idx < MEM_RSV_BANK_COLORS; bank_idx++) {
			if (mem->mem_free_size_detail[idx][bank_idx] <= 0) continue;
			entry = list_first_entry(&mem->mem_free_list[idx][bank_idx], struct mem_reserve_page, list);
			list_del(&entry->list);
			mem->mem_free_size--;
			mem->mem_free_size_detail[idx][bank_idx]--;
			return entry;
		}
	}
	// Should not reach here
	printk(KERN_ALERT "*** No free page in mem_free_list ***\n");
	return NULL;
}

struct mem_reserve_page* get_task_page_ownership(mem_reserve_t mem, struct mem_reserve_page *entry)
{
	struct list_head *head, *shared_list;
	if (entry == NULL) return NULL;
	head = shared_list = &entry->shared;
	do {
		if (entry->mem == mem) return entry;

		shared_list = entry->shared.next;
		entry = list_entry(shared_list, struct mem_reserve_page, shared);
	} while (shared_list != head);
	return NULL;
}

void add_task_page_ownership(struct page *page, struct mem_reserve_page *entry)
{
	if (page->rsv == NULL) {
		page->rsv = entry;
		INIT_LIST_HEAD(&entry->shared);
	}
	else {
		list_add_tail(&entry->shared, 
			&((struct mem_reserve_page*)page->rsv)->shared);
	}
}

// Called by mm/rmap.c::page_remove_rmap()
// - Caller holds pte lock
void rk_remove_page_rmap(struct page *page, mem_reserve_t mem)
{
#ifndef RSV_NO_SHARED_PAGE_CONSERVATION
	// Shared Page Conservation
	struct mem_reserve_page *entry;
	struct page *tmp_page;

	entry = get_task_page_ownership(mem, page->rsv);
	if (!mem || !entry) return;

	if (PageEvictionLock(page)) return; 

	raw_spin_lock(&mem_reserve_lock);
	if (entry->access_count > 0) entry->access_count--;

	// Page is not shared with other reserves
	// (Unmapped private page will be freed by rk_free_pages)
	if (list_empty(&entry->shared)) goto unlock;

	// Page is shared with other reserves.
	// If access_count > 0, then we need to retain page entry info because
	// the entry is being used by other tasks in the same reserve.
	if (entry->access_count > 0) goto unlock;

	// Page is allocated from current mem reserve
	if (page->rsv == entry) {
		mem_reserve_t shr;

		mem_dbg("remove_rmap: shared page(owner) entry:%lx - page:%lx\n", (unsigned long)entry, (unsigned long)page);

		// Setup page link to another mem_reserve_page entry
		MEM_LOCK(&mem->mem_list_lock);
		page->rsv = list_entry(entry->shared.next, struct mem_reserve_page, shared);
		shr = ((struct mem_reserve_page*)page->rsv)->mem;
		VM_BUG_ON(shr == NULL);

		// Remove mem_reserve_page entry from shared list
		list_del(&entry->shared);
		INIT_LIST_HEAD(&entry->shared);
		MEM_UNLOCK(&mem->mem_list_lock);

		// Move one conserved page from shr_mem(one of other reserves) to current mem.
		// As this page now belong to shr_mem,
		// we need to get one free page(in mem_conserved_list) from shr_mem.
		MEM_LOCK(&shr->mem_list_lock);
		VM_BUG_ON(shr->mem_conserved_size <= 0);
		shr->mem_conserved_size--;
		tmp_page = list_first_entry(&shr->mem_conserved_list, struct page, lru);
		list_del(&tmp_page->lru);
		MEM_UNLOCK(&shr->mem_list_lock);

		MEM_LOCK(&mem->mem_list_lock);
	}
	// Else, we have used another reserve's page
	else {
		mem_dbg("remove_rmap: shared page entry:%lx - page:%lx\n", (unsigned long)entry, (unsigned long)page);

		// Remove mem_reserve_page entry from shared list
		MEM_LOCK(&mem->mem_list_lock);
		list_del(&entry->shared);
		INIT_LIST_HEAD(&entry->shared);

		// Need to move one conserved page to mem_free_list
		VM_BUG_ON(mem->mem_conserved_size <= 0);
		mem->mem_conserved_size--;
		tmp_page = list_first_entry(&mem->mem_conserved_list, struct page, lru);
		list_del(&tmp_page->lru);
	}
	// Move tmp_page to mem_free_list of current mem.
	entry->page = tmp_page;
	entry->page->rsv = entry;
	move_to_mem_free_list(entry, mem);

	MEM_UNLOCK(&mem->mem_list_lock);

unlock:
	raw_spin_unlock(&mem_reserve_lock);
#endif
}

enum rk_attach_page_result {
	PAGE_ATTACHED,
	PAGE_ATTACHED_BUT_NOT_MIGRATED,
	PAGE_ATTACHED_ALREADY,
	PAGE_NOT_ATTACHED,
};

static struct mem_reserve_page* rk_attach_single_page(mem_reserve_t mem, struct page *page, enum rk_attach_page_result *res)
{
	struct page *tmp;
	struct mem_reserve_page *entry;
	bool shared_reserved_page = false;
	LIST_HEAD(entry_list);

	raw_spin_lock(&mem_reserve_lock);
	MEM_LOCK(&mem->mem_list_lock);
	
	// Check if page has been already attached
	if (PageMemReserve(page)) {
		// Check page ownership
		entry = get_task_page_ownership(mem, page->rsv);
		if (entry) {
			// Increase access count
			entry->access_count++;		
			MEM_UNLOCK(&mem->mem_list_lock);
			raw_spin_unlock(&mem_reserve_lock);
			if (res) *res = PAGE_ATTACHED_ALREADY;
			return entry;
		}
		// Need to add ownership to this shared page
		shared_reserved_page = true;
	}

	// Check remaining free pages
	if (mem->mem_free_size <= 0) {
		MEM_UNLOCK(&mem->mem_list_lock);
		raw_spin_unlock(&mem_reserve_lock);
		if (res) *res = PAGE_NOT_ATTACHED;
		return NULL;
	}
	
	if (res) *res = PAGE_ATTACHED_BUT_NOT_MIGRATED; // Default result
	
	// Detach one free mem_reserve_entry from mem_free_list
	// and attach mem_reserve_entry to mem_used_list	
	entry = get_page_from_mem_free_list(mem);
	list_add(&entry->list, &entry_list);
	tmp = entry->page;

	if (shared_reserved_page == false) {
		int ret = -1;
		// Migration for private page
		//if (page_mapcount(page) <= 1 && !isolate_lru_page(page)) {
		if (!isolate_lru_page(page)) {
			LIST_HEAD(page_list);

			list_add(&page->lru, &page_list);
			SetPageMemReserve(tmp);
			MEM_UNLOCK(&mem->mem_list_lock);
			raw_spin_unlock(&mem_reserve_lock);

			// No need to worry about race here
			// - No other tasks can attach the same page while we release spinlock
			// - The callers of this function, rk_add_page_rmap() and 
			//   attach_pages_to_mem_reserve(), protect against other tasks by
			//   pte_lock and mm->mmap_sem, respectively
			if ((ret = rk_migrate_page(page, tmp)) == 0) {
				// If rk_migrate_page() succeeds, it calls free_hot_cold_page(page) 
				// to free 'page'. We set 'tmp' as NULL not to free it later on.
				tmp = NULL;

				// The 'entry' will be moved to mem_used_list later in this function. 
				raw_spin_lock(&mem_reserve_lock);
				MEM_LOCK(&mem->mem_list_lock);
				if (res) *res = PAGE_ATTACHED;
			}
			else {
				// If rk_migrate_page() fails, it calls rk_free_pages(tmp) 
				// that in turn calls move_to_mem_free_list(entry). 
				// As 'entry' was not in mem_used_list, we need to fix mem_used_size here.
				raw_spin_lock(&mem_reserve_lock);
				MEM_LOCK(&mem->mem_list_lock);
				mem->mem_used_size++;
				mem->mem_inactive_size++; // cuz entry->active_used == 0

				// Delete 'entry' from mem_free_list for the later code.
				del_page_from_mem_free_list(entry, mem);
				list_add(&entry->list, &entry_list);

				//printk("rk_attach_single_page: rk_migrate_page failed. p:%lx, f:%lx\n", (unsigned long)page, page->flags);
			}
			mem_dbg("rk_migrate_page : from:%lx, to:%lx, err:%d, free:%d, used:%d\n", 
				(unsigned long)page, (unsigned long)entry->page, ret, mem->mem_free_size, mem->mem_used_size);
			//printk("rk_attach_single_page: p:%lx, f:%lx\n", (unsigned long)page, page->flags);
		}
		else {
			//printk("rk_attach_single_page: cannot isolate from lru. p:%lx, f:%lx\n", (unsigned long)page, page->flags);
		}
		// For migration fail cases
		if (ret) {
			// Setup a link to page. ('page' substitutes 'tmp', and tmp will not be used anymore)
			entry->page = page;
			page->rsv = entry;
			SetPageMemReserve(page);
		}
	}
	else {
		// Setup a link to shared page
		entry->page = page;

		add_task_page_ownership(page, entry);
#ifndef RSV_NO_SHARED_PAGE_CONSERVATION
		// Shared Page Conservation
		if (!PageEvictionLock(page)) {
			// Move the free page of mem_reserve_entry to conserved list
			list_add_tail(&tmp->lru, &mem->mem_conserved_list);
			mem->mem_conserved_size++;
			tmp = NULL;
		}
#endif
	}
	// Add entry to mem_used_list
	add_page_to_mem_used_list(entry, mem);
	MEM_UNLOCK(&mem->mem_list_lock);
	raw_spin_unlock(&mem_reserve_lock);

	if (tmp) {
		// tmp is not part of this reserve anymore
		ClearPageMemReserve(tmp);
		tmp->rsv = NULL;
		// Now 'page' is given to our reserve, 
		// so we need to return 'tmp' to global memory manager.
		// (not pagebin-pool)
		//free_page_to_pagebins(tmp);
		__free_page(tmp);
	}

	return entry;
}

// This function is called by mm/rmap.c
// - Caller holds pte lock
void rk_add_page_rmap(struct page *page, bool is_anon)
{
	mem_reserve_t mem = current->rk_resource_set->mem_reserve->reserve;
	enum rk_attach_page_result res;

	mem_dbg("add_rmap: %s, %lx(%x), mc:%d\n", is_anon ? "Anon" : "File", (unsigned long)page, (unsigned int)page->flags, page_mapcount(page));

	if (PagePrivate(page) || PageReserved(page)) return;
	if (page->mapping == NULL) return;
#ifdef RSV_NO_PAGE_CACHE
	if (PageMappedToDisk(page) || !PageAnon(page)) return;
#endif

	rk_attach_single_page(mem, page, &res);
	if (res == PAGE_ATTACHED_ALREADY) {
		mem_dbg("add_rmap: page %lx already owned by %d. ac:%d\n", 
			(unsigned long)page, current->pid, entry->access_count + 1);
	}
	else if (res == PAGE_NOT_ATTACHED) {
		// This will not be happened because we confirm enough freed pages in handle_mm_fault()
		printk("WARNING: not enough RK pages - add_rmap (pid: %d)\n", current->pid);
		//dump_stack();
	}
	else {
		//printk("add_rmap: f:%d\n", mem->mem_free_size);
	}
}


// Called by mm/memory.c::handle_mm_fault()
void rk_check_enough_pages(mem_reserve_t mem)
{
	// Make sure to have some free pages (for rk_add_page_rmap)
	
	if (mem == NULL) return;
	//printk("check: f %d\n", mem->mem_free_size);
	if (mem->mem_free_size > 0) return;

	if (evict_reserved_pages(mem, MEM_RSV_EVICT_SIZE) == 0) {
		evict_reserved_pages(mem, MEM_RSV_EVICT_SIZE); 
	}
}

// Swap-out reserved pages to disk. Called by rk_alloc_pages() 
int evict_reserved_pages(mem_reserve_t mem, int n_pages)
{
	struct mem_reserve_page *entry;
	struct zone *last_zone = NULL, *zone;
	LIST_HEAD(page_list);
	int n_list_now, n_evicted = 0, ret;
	int n_size;
	int i, nr_referenced;
	struct page *page;
	unsigned long vm_flags;
	
	if (mem->mem_used_size == 0) {
		printk("evict_reserved_pages: no pages to evict!!\n");
		return -1;
	}

	// Refill inactive list
	raw_spin_lock(&mem_reserve_lock);
	MEM_LOCK(&mem->mem_list_lock);
	if (mem->mem_inactive_size < n_pages + MEM_RSV_EVICT_SIZE) {
		n_size = n_pages + MEM_RSV_EVICT_SIZE;
		if (n_size > mem->mem_active_size) n_size = mem->mem_active_size;

		for (i = 0; i < n_size; i++) {
			entry = list_first_entry(&mem->mem_active_list,
				struct mem_reserve_page, list);
			page = entry->page;
			if (!page) goto move_active_tail;
			if (PageLocked(page)) goto move_active_tail;
			if (PageEvictionLock(page)) goto move_active_tail;
			if (PageReserved(page)) goto move_active_tail;

			// Must release mem_reserve_lock before calling page_referenced()
			// to avoid deadlock
			// - page_referenced() holds pte_lock internally
			MEM_UNLOCK(&mem->mem_list_lock);
			raw_spin_unlock(&mem_reserve_lock);

			nr_referenced = page_referenced(page, false, NULL, &vm_flags);
			nr_referenced += TestClearPageReferenced(page) != 0;
			//printk("act - page:%lx, f:%lx, ref:%d\n", (unsigned long)page, (unsigned long)page->flags, nr_referenced);

			raw_spin_lock(&mem_reserve_lock);
			MEM_LOCK(&mem->mem_list_lock);
			if (nr_referenced > 0) goto move_active_tail;
			
			// Move page to inactive list
			list_move_tail(&entry->list, &mem->mem_inactive_list);
			entry->active_used = 0;
			mem->mem_inactive_size++;
			mem->mem_active_size--;
			mem_dbg("evict page: To inactive - page:%lx\n", (unsigned long)page);

			continue;
move_active_tail:
			list_move_tail(&entry->list, &mem->mem_active_list);
		}
	}
	MEM_UNLOCK(&mem->mem_list_lock);
	raw_spin_unlock(&mem_reserve_lock);

	// Scan inactive list
	raw_spin_lock(&mem_reserve_lock);
	MEM_LOCK(&mem->mem_list_lock);
	n_size = mem->mem_inactive_size;
	n_list_now = 0;
	for (i = 0; i < n_size; i++) {
		entry = list_first_entry(&mem->mem_inactive_list,
			struct mem_reserve_page, list);
		page = entry->page;

		if (!page) goto move_to_active;
		if (page->mapping == NULL) goto move_to_active;
		if (PageWriteback(page)) goto move_to_tail; // page under writeback
		if (PageLocked(page)) goto move_to_active;
		if (PageEvictionLock(page)) goto move_to_active;
#ifdef RSV_NO_PAGE_CACHE
		if (PageMappedToDisk(page) || !PageAnon(page)) 
			goto move_to_tail; // file mapped
#endif
#ifdef RSV_NO_SHARED_MEM
		if (page_mapcount(page) > 1) goto move_to_tail; // shared page
#endif
		// Must release mem_reserve_lock before calling page_referenced()
		// to avoid deadlock
		// - page_referenced() holds pte_lock internally
		MEM_UNLOCK(&mem->mem_list_lock);
		raw_spin_unlock(&mem_reserve_lock);

		nr_referenced = page_referenced(page, false, NULL, &vm_flags);
		nr_referenced += TestClearPageReferenced(page) != 0;
		//printk("inact - page:%lx, f:%lx, ref:%d\n", (unsigned long)page, (unsigned long)page->flags, nr_referenced);

		raw_spin_lock(&mem_reserve_lock);
		MEM_LOCK(&mem->mem_list_lock);
		if (nr_referenced > 0) goto move_to_active;

#ifndef RSV_NO_SHARED_PAGE_CONSERVATION
		// Shared Page Conservation
		// - Check if this page is shared
		if (!list_empty(&entry->shared)) {
			if (!trylock_page(page)) goto move_to_active;

			MEM_UNLOCK(&mem->mem_list_lock);
			raw_spin_unlock(&mem_reserve_lock);
			if (SWAP_SUCCESS == try_to_unmap(page, TTU_UNMAP | TTU_RK_UNMAP | TTU_IGNORE_ACCESS)) {
				mem_dbg("evict page: UNMAP!! (page:%lx)\n", (unsigned long)page);
				n_evicted++;
			}
			else {
				mem_dbg("evict page: UNMAP failed!! (page:%lx)\n", (unsigned long)page);
			}
			unlock_page(page);

			raw_spin_lock(&mem_reserve_lock);
			MEM_LOCK(&mem->mem_list_lock);
			if (n_list_now + n_evicted >= n_pages) break;

			continue;
		}
#endif

		if (isolate_lru_page(page)) goto move_to_active; 

		ClearPageActive(page);
		ClearPageReferenced(page);

		zone = page_zone(page);
		if (last_zone && last_zone != zone) {
			// do previous list first
			MEM_UNLOCK(&mem->mem_list_lock);
			raw_spin_unlock(&mem_reserve_lock);

			ret = rk_page_list_out(last_zone, &page_list, n_list_now);
			//printk("req: %d, evicted:%d\n", n_list_now, ret);
			n_evicted += ret;
			if (ret != n_list_now) {
				mem_dbg("evict_page: UNFREED %d pages\n", n_list_now - ret);
			}
			n_list_now = 0;
			last_zone = NULL;

			raw_spin_lock(&mem_reserve_lock);
			MEM_LOCK(&mem->mem_list_lock);
		}
		last_zone = zone;
		mem_dbg("evict page:%lx, f:%x, c:%d, mc:%d, rsv:%d, %s\n", 
			(unsigned long)page, 
			(unsigned int)page->flags, 
			page_count(page), 
			page_mapcount(page), 
			PageMemReserve(page), 
			page->mapping == NULL ? "N/A" 
				: (((unsigned long)page->mapping & 0x1) ? "Anon" 
				: "File"));

		// Clear unevictable flag (because it's isolated from LRU)
		lock_page(page);
		ClearPageUnevictable(page);
		unlock_page(page);

		list_add_tail(&page->lru, &page_list);
		if (++n_list_now + n_evicted >= n_pages) break;

		continue;
move_to_active:
		// Move page to active list
		list_move_tail(&entry->list, &mem->mem_active_list);
		entry->active_used = 1;
		mem->mem_inactive_size--;
		mem->mem_active_size++;
		mem_dbg("evict page: To Active - page:%lx\n", (unsigned long)page);
		continue;
move_to_tail:
		list_move_tail(&entry->list, &mem->mem_inactive_list);
	}
	MEM_UNLOCK(&mem->mem_list_lock);
	raw_spin_unlock(&mem_reserve_lock);

	//printk("<<< evict (%d)\n", current->pid);
	if (n_list_now) {
		ret = rk_page_list_out(last_zone, &page_list, n_list_now);
		//printk("req: %d, evicted:%d\n", n_list_now, ret);
		n_evicted += ret;
		if (ret != n_list_now) {
			mem_dbg("evict_page: UNFREED %d pages\n", n_list_now - ret);
		}
	}
	//printk("evict_reserved_page: %d (free:%d, ac:%d, ina:%d)\n", n_evicted, mem->mem_free_size, mem->mem_active_size, mem->mem_inactive_size);
	
	return n_evicted;
}

int attach_pages_to_mem_reserve(mem_reserve_t mem, struct task_struct *p, bool second_try)
{
	struct mm_struct *mm;
	struct vm_area_struct *mmap;
	struct page *page;
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	int n_pages = 0, n_attached_pages = 0;
	struct mem_reserve_page* entry;
	
	mm = p->active_mm;
	down_read(&mm->mmap_sem);
	mmap = mm->mmap;

	while(mmap) {
		unsigned long n = mmap->vm_start;
		if (mmap->vm_flags & (VM_IO | VM_PFNMAP)) goto next_vma;
			
		//bool executable = (mmap->vm_flags & VM_EXEC) != 0;
		while(n < mmap->vm_end) {
			enum rk_attach_page_result res;

			pgd = pgd_offset(mmap->vm_mm, n);
			if (pgd_none(*pgd) || !pgd_present(*pgd)) goto find_next_page;
			pud = pud_offset(pgd, n);
			if (pud_none(*pud) || !pud_present(*pud)) goto find_next_page;
			pmd = pmd_offset(pud, n);
			if (pmd_none(*pmd) || !pmd_present(*pmd)) goto find_next_page;
			pte = pte_offset_map(pmd, n);
			if (pte_none(*pte) || !pte_present(*pte)) goto unmap;

			page = pte_page(*pte);
			if (PagePrivate(page) || PageReserved(page)) goto unmap;
			if (page->mapping == NULL) goto unmap;
#ifdef RSV_NO_PAGE_CACHE
			if (PageMappedToDisk(page) || !PageAnon(page)) goto unmap;
#endif
#ifdef RSV_NO_SHARED_MEM
			if (page_mapcount(page) > 1) goto unmap;
#endif
			n_pages++;

			entry = rk_attach_single_page(mem, page, &res);
			if (res == PAGE_ATTACHED_ALREADY) {
				if (!second_try) {
					mem_dbg("attach: page %lx already owned by %d. ac:%d\n", 
						(unsigned long)page, p->pid, entry->access_count + 1);
					// account RK-reserved page as attached
					n_attached_pages++;
				}
				goto unmap;
			}
			else if (res == PAGE_NOT_ATTACHED) {
				pte_unmap(pte);
				break;
			}

			n_attached_pages++;
			page = entry->page;
			mem_dbg("attach page:%lx, f:%x, c:%d, mc:%d, rsv:%d, %s -> entry %d\n", 
				(unsigned long)page, 
				(unsigned int)page->flags, 
				page_count(page), 
				page_mapcount(page), 
				PageMemReserve(page), 
				page->mapping == NULL ? "N/A" 
					: (((unsigned long)page->mapping & 0x1) 
					? "Anon" : "File"),
				page_category(entry));
unmap:
			pte_unmap(pte);
find_next_page:
			n += PAGE_SIZE;
		}
next_vma:
		mmap = mmap->vm_next;
		if (mem->mem_free_size <= 0) break;
	}
	up_read(&mm->mmap_sem);

	printk("attach: free_list:%d, used_list:%d, total:%d, attached:%d\n", 
		mem->mem_free_size, mem->mem_used_size, n_pages, n_attached_pages);

	return n_pages - n_attached_pages;
}

// Swap-out all non-reserved pages of task p. Called by mem_reserve_attach_process()
int evict_nonreserved_pages(struct task_struct *p)
{
	struct mm_struct *mm;
	struct vm_area_struct *mmap;
	struct page *page;
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	int n_list_max = MEM_RSV_EVICT_SIZE, n_list_now = 0;
	int n_evicted = 0;
	struct zone *last_zone = NULL, *zone;
	LIST_HEAD(page_list);
	
	mm = p->active_mm;
	down_read(&mm->mmap_sem);
	mmap = mm->mmap; 

	if (get_nr_swap_pages() <= 0) return -1;

	while(mmap)
	{
		unsigned long n = mmap->vm_start;
		if (mmap->vm_flags & (VM_IO | VM_PFNMAP)) goto next_vma;
		while(n < mmap->vm_end)
		{
			pgd = pgd_offset(mmap->vm_mm, n);
			if (pgd_none(*pgd) || !pgd_present(*pgd)) goto find_next_page;
			pud = pud_offset(pgd, n);
			if (pud_none(*pud) || !pud_present(*pud)) goto find_next_page;
			pmd = pmd_offset(pud, n);
			if (pmd_none(*pmd) || !pmd_present(*pmd)) goto find_next_page;
			pte = pte_offset_map(pmd, n);
			if (pte_none(*pte) || !pte_present(*pte)) goto unmap;

			page = pte_page(*pte);
			if (PageWriteback(page)) 
				goto unmap; // page under writeback
#ifdef RSV_NO_PAGE_CACHE
			if (PageMappedToDisk(page) || !PageAnon(page)) 
				goto unmap; // file mapped
#endif
#ifdef RSV_NO_SHARED_MEM
			if (page_mapcount(page) > 1) 
				goto unmap; // shared page
#endif
			if (!page_evictable(page))  
				goto unmap; // unevictable

			if (!PageLRU(page)) {
				mem_dbg("shrink page:%lx, f:%x, c:%d, mc:%d, rsv:%d, %s -> NOT LRU\n", 
					(unsigned long)page, 
					(unsigned int)page->flags, 
					page_count(page), 
					page_mapcount(page), 
					PageMemReserve(page), 
					page->mapping == NULL ? "N/A" 
						: (((unsigned long)page->mapping & 0x1) ? "Anon" 
						: "File"));
				goto unmap; // not in LRU
			}

			zone = page_zone(page);
			if ((last_zone && last_zone != zone) 
				|| n_list_now >= n_list_max) {
				// do previous list first
				n_evicted += rk_page_list_out(last_zone, 
						&page_list, n_list_now);
				n_list_now = 0;
			}
			last_zone = zone;

			if (isolate_lru_page(page)) {
				mem_dbg("  ---- failed to isolate from lru\n");
				goto unmap;
			}

			ClearPageActive(page);
			ClearPageReferenced(page);
			mem_dbg("shrink page:%lx, f:%x, c:%d, mc:%d, rsv:%d, %s\n", 
				(unsigned long)page, 
				(unsigned int)page->flags, 
				page_count(page), 
				page_mapcount(page), 
				PageMemReserve(page), 
				page->mapping == NULL ? "N/A" 
					: (((unsigned long)page->mapping & 0x1) ? "Anon" 
					: "File"));

			list_add(&page->lru, &page_list);
			n_list_now++;
unmap:
			pte_unmap(pte);
find_next_page:
			n += PAGE_SIZE;
		}
next_vma:
		mmap = mmap->vm_next;
	}
	up_read(&mm->mmap_sem);
	if (n_list_now) {
		n_evicted += rk_page_list_out(last_zone, 
				&page_list, n_list_now);
	}

	mem_dbg("evict_nonreserved_pages: pageout:%d\n", n_evicted);
	return n_evicted;	
}

// Performs admission test for a task
int do_task_admission_test(mem_reserve_t mem, struct task_struct *p)
{
	return mem->mem_total_size;
}

void mem_reserve_attach_process(mem_reserve_t mem, struct task_struct *p)
{
	// Note: the task to be attached should have been suspended by the caller
	int n_remaining;

	if (mem == NULL || p == NULL) return;
	if (!thread_group_leader(p)) {
		// printk("mem_reserve_attach_process: child thread %d\n", p->pid);
		return;
	}

	//printk("mem_reserve_attach_process: pid %d\n", p->pid);
	//printk("======== BEFORE ATTACHING (AFTER MAKING PRESENT) =========\n");
	//sys_rk_mem_reserve_show_task_vminfo(p->pid);

	n_remaining = attach_pages_to_mem_reserve(mem, p, false);
	//printk("======== AFTER ATTACHING  =========\n");
	//sys_rk_mem_reserve_show_task_vminfo(p->pid);
	if (n_remaining && mem->mem_res_attr.reserve_mode == RSV_HARD) {
		evict_nonreserved_pages(p);
		attach_pages_to_mem_reserve(mem, p, true);
	}
	//printk("======== AFTER EVICTING NON-RESERVED PAGES =========\n");
	//sys_rk_mem_reserve_show_task_vminfo(p->pid);
}

void mem_reserve_detach_process(mem_reserve_t mem, struct task_struct *p)
{
	struct mm_struct *mm; 
	struct vm_area_struct *mmap;
	struct page *page;
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	struct mem_reserve_page* entry, *safe;
	int n_free = 0;
	LIST_HEAD(page_list);
	mem_reserve_t cur_mem = NULL;
	int fix_used_size, fix_active_size, fix_inactive_size;

	cpu_tick_data_t t1, t2;
	rk_rdtsc(&t1);

	if (mem == NULL || p == NULL) return;
	if (!thread_group_leader(p)) {
		// printk("mem_reserve_detach_process: child thread %d\n", p->pid);
		return;
	}

	mm = p->active_mm;
	down_read(&mm->mmap_sem);
	mmap = mm->mmap;

	printk("mem_reserve_detach_process: pid %d (cur free:%d, used:%d)\n", p->pid, mem->mem_free_size, mem->mem_used_size);
	while(mmap) {
		unsigned long n = mmap->vm_start;
		if (mmap->vm_flags & (VM_IO | VM_PFNMAP)) goto next_vma;

		while(n < mmap->vm_end) {
			bool need_detach = false;
			pgd = pgd_offset(mmap->vm_mm, n);
			if (pgd_none(*pgd) || !pgd_present(*pgd)) goto find_next_page;
			pud = pud_offset(pgd, n);
			if (pud_none(*pud) || !pud_present(*pud)) goto find_next_page;
			pmd = pmd_offset(pud, n);
			if (pmd_none(*pmd) || !pmd_present(*pmd)) goto find_next_page;
			pte = pte_offset_map(pmd, n);
			if (pte_none(*pte) || !pte_present(*pte)) goto unmap;

			page = pte_page(*pte);
			if (!PageMemReserve(page)) goto unmap;
	
			raw_spin_lock(&mem_reserve_lock);
			MEM_LOCK(&mem->mem_list_lock);
			entry = get_task_page_ownership(mem, page->rsv);
			if (entry) {			
				if (entry->access_count > 0) entry->access_count--;
				if (entry->access_count == 0) {
					// access_count shows the number of tasks using the page within the same mem-reserve.
					// "access_count > 0" means other tasks in this reserve are using the page.
					need_detach = true;
				}
			}
			else if (page->rsv == NULL) {
				// something wrong.. 
				printk("detach: ERROR. Page reserved, but no entry info (page:%lx/entry:%lx)\n", (unsigned long)page, 
					(unsigned long)page->rsv);
				clear_reserve_hot_page(page);
			}

			if (need_detach == false) {
				MEM_UNLOCK(&mem->mem_list_lock);
				raw_spin_unlock(&mem_reserve_lock);
				goto unmap;
			}
			// Move this entry from mem_used_list to to page_list
			list_move_tail(&entry->list, &page_list);
			mem->mem_used_size--;
			if (entry->active_used) mem->mem_active_size--;
			else mem->mem_inactive_size--;

			mem_dbg("detach: entry:%lx, page:%lx\n", (unsigned long)entry, (unsigned long)page);
			// Page is not shared with other reserves
			if (list_empty(&entry->shared)) {
				MEM_UNLOCK(&mem->mem_list_lock);
				raw_spin_unlock(&mem_reserve_lock);
			}
			// Page is shared with other reserves
			else {
				struct page *tmp_page = NULL;

				// Page is allocated from current mem reserve
				if (page->rsv == entry) {
					mem_dbg("detach: shared page (owner) entry:%lx - page:%lx\n", (unsigned long)entry, (unsigned long)page);

					// Setup page link to another mem_reserve_page entry
					page->rsv = list_entry(entry->shared.next, struct mem_reserve_page, shared);

					// Remove mem_reserve_page entry from shared list
					list_del(&entry->shared);
					INIT_LIST_HEAD(&entry->shared);
				
					MEM_UNLOCK(&mem->mem_list_lock);

#ifndef RSV_NO_SHARED_PAGE_CONSERVATION
					// Shared Page Conservation
					if (!PageEvictionLock(page)) {
						mem_reserve_t shr;
						shr = ((struct mem_reserve_page*)page->rsv)->mem;
						VM_BUG_ON(shr == NULL);
						// Remove one conserved page from shr_mem(one of other reserves).
						// As this page now belongs to shr_mem, 
						// we need to remove one free page(in mem_conserved_pages) from shr_mem.
						MEM_LOCK(&shr->mem_list_lock);

						VM_BUG_ON(shr->mem_conserved_size <= 0);
						shr->mem_conserved_size--;
						tmp_page = list_first_entry(&shr->mem_conserved_list, struct page, lru);
						list_del(&tmp_page->lru);

						MEM_UNLOCK(&shr->mem_list_lock);
					}
#endif
				}
				// Else, we have used another reserve's page. 
				else {
					mem_dbg("detach: shared page entry:%lx - page:%lx\n", (unsigned long)entry, (unsigned long)page);

					// Remove mem_reserve_page entry from shared list
					list_del(&entry->shared);
					INIT_LIST_HEAD(&entry->shared);

#ifndef RSV_NO_SHARED_PAGE_CONSERVATION
					// Shared Page Conservation
					if (!PageEvictionLock(page)) {
						// Need to remove one conserved from current reserve.
						VM_BUG_ON(mem->mem_conserved_size <= 0);
						mem->mem_conserved_size--;
						tmp_page = list_first_entry(&mem->mem_conserved_list, struct page, lru);
						list_del(&tmp_page->lru);
					}
#endif
					MEM_UNLOCK(&mem->mem_list_lock);
				}
				entry->page = tmp_page;
				raw_spin_unlock(&mem_reserve_lock);
			}
unmap:
			pte_unmap(pte);
find_next_page:
			n += PAGE_SIZE;
		}
next_vma:
		mmap = mmap->vm_next;
	}
	up_read(&mm->mmap_sem);
	// temporarily disable mem reserve during this (for alloc_page)
	if (current->rk_resource_set && current->rk_resource_set->mem_reserve) {
		cur_mem = current->rk_resource_set->mem_reserve->reserve;
		current->rk_resource_set->mem_reserve = NULL;
	}		

	fix_used_size = fix_active_size = fix_inactive_size = 0;
	list_for_each_entry_safe(entry, safe, &page_list, list) {
		// realloc page for mem_free_list
		struct page *newpage = NULL;

		page = entry->page;
		if (page == NULL) {
			newpage = alloc_page_from_pagebins(mem, entry);
		}
		else {
			int ret = -1;
			int is_active_used = entry->active_used;
			if (!isolate_lru_page(page)) {
				LIST_HEAD(migrate_list);
				list_add_tail(&page->lru, &migrate_list);
				newpage = alloc_page(GFP_HIGHUSER_MOVABLE);

				if ((ret = rk_migrate_page(page, newpage)) == 0) {
					mem_dbg("detach: migration ok : old-%lx(f:%lx, rsv:%lx), new-%lx(f:%lx, rsv:%lx)\n", 
						(unsigned long)page, page->flags, (unsigned long)page->rsv,
						(unsigned long)newpage, newpage->flags, (unsigned long)newpage->rsv);
					// If rk_migrate_page() succeeds, it calls rk_free_pages(page) 
					// which in turn calls move_to_mem_free_list().
					// But since this page was not in mem_used_list, we should recover the counters here.
					fix_used_size++;
					if (is_active_used) fix_active_size++;
					else fix_inactive_size++;

					continue;
				}
				else {
					// If rk_migrate_page() fails, it will call free_hot_cold_page(newpage).
				}
			}
			if (ret) {
				// Clear unmigratable page flags
				page->rsv = NULL;
				clear_reserve_hot_page(page);
				// Reallocate a page for entry ('newpage' has been freed by rk_migrate_page when it fails)
				// - here, we use alloc_page_from_pagebins, because it will be kept in mem_reserve.
				alloc_page_from_pagebins(mem, entry); 

				// Fill the pagebin-pool 
				newpage = alloc_page(GFP_HIGHUSER_MOVABLE);
				SetPageMemReserve(newpage); // no need to page_lock()
				free_page_to_pagebins(newpage);
			}
		}
		mem_dbg("detach: new alloc entry:%lx - page:%lx\n", (unsigned long)entry, (unsigned long)entry->page);
		if (entry->page) {
			entry->page->rsv = entry;
			SetPageMemReserve(entry->page);
		}
		else {
			printk(" -> NULL\n");
		}
		entry->mem = mem;
		entry->active_used = 0;
		entry->executable = false;
		entry->access_count = 0;
		
		n_free++;
	}
	//printk("detach_task 3\n"); 

	// restore temporarily disabled mem reserve
	if (cur_mem) current->rk_resource_set->mem_reserve = cur_mem->rsv;

	raw_spin_lock(&mem_reserve_lock);
	MEM_LOCK(&mem->mem_list_lock);

	while (!list_empty(&page_list)) {
		entry = list_first_entry(&page_list, struct mem_reserve_page, list);
		list_del(&entry->list);
		add_page_to_mem_free_list(entry, mem);
	}

	// Fix mem_used_list sizes
	mem->mem_used_size += fix_used_size;
	mem->mem_active_size += fix_active_size;
	mem->mem_inactive_size += fix_inactive_size;

	MEM_UNLOCK(&mem->mem_list_lock);
	raw_spin_unlock(&mem_reserve_lock);
	
	rk_rdtsc(&t2);
	printk("detach_task: free_list:%d, used_list:%d, total_size:%d (maj_flt:%lu, min_flt:%lu), time:%lumsec\n", 
		mem->mem_free_size, mem->mem_used_size, mem->mem_total_size, p->maj_flt, p->min_flt, (unsigned long)(t2 - t1) / 1000000);
}

struct page* rk_alloc_pages(gfp_t gfp_mask, unsigned int order, bool* ret)
{
	struct mem_reserve_page* entry;
	mem_reserve_t mem;

	if (!(((gfp_mask & GFP_HIGHUSER_MOVABLE) == GFP_HIGHUSER_MOVABLE)
		&& current->rk_resource_set && current->rk_resource_set->mem_reserve)) {
		*ret = false; // let the kernel allocate a page
		return NULL;
	}
	if (order > 0) {
		printk("alloc_pages_hook: does not support order > 0 (gfp:%x)\n", gfp_mask);
		*ret = false; // let the kernel allocate a page
		return NULL;
	}
	mem = current->rk_resource_set->mem_reserve->reserve;
	if (!mem) {
		*ret = false; // let the kernel allocate a page
		return NULL;
	}

	if (mem->mem_free_size <= MEM_LOW_WATERMARK) {
		int n_evict = MEM_RSV_EVICT_SIZE;

		if (mem->mem_res_attr.reserve_mode == RSV_FIRM) {
			*ret = false; // let the kernel allocate a page
			goto ret_null;
		}

		// for hard reservation
		if (n_evict > mem->mem_used_size) n_evict = mem->mem_used_size;
		evict_reserved_pages(mem, n_evict);
		if (mem->mem_free_size == 0) {
			// Try one more time (As reference bit is cleared)
			evict_reserved_pages(mem, n_evict);
			if (mem->mem_free_size == 0) {
				printk("WARNING: not enough RK pages - use a kernel page (pid: %d)\n", current->pid);
				// TODO: kill this process w/o causing oom?
				*ret = false; // let the kernel allocate a page
				//*ret = true; // causes the kernel out-of-memory
				goto ret_null;
			}
		}
	}

	raw_spin_lock(&mem_reserve_lock);
	MEM_LOCK(&mem->mem_list_lock);
	if (current->rk_virt_gfn == 0) {
		entry = get_page_from_mem_free_list(mem);
	}
	else {
		mem_reserve_attr_t attr;
		int virt_color_idx, i;
		attr = &mem->mem_res_attr;
		virt_color_idx = (int)(current->rk_virt_gfn & (attr->nr_colors - 1));
		for (i = 0, entry = NULL; i < attr->nr_bank_colors; i++) {
			entry = __page_in_mem_free_list(mem, attr->colors[virt_color_idx], 
				attr->bank_colors[mem->next_bank_color_to_tasks]);
			if (++(mem->next_bank_color_to_tasks) >= attr->nr_bank_colors)
				mem->next_bank_color_to_tasks = 0;
			if (entry) break;
		}
		if (entry) 
			del_page_from_mem_free_list(entry, mem);
		else
			entry = get_page_from_mem_free_list(mem);
	}
 	add_page_to_mem_used_list(entry, mem); 
	// Reset access_count to 0 because it will be increased by rk_add_page_rmap().
	entry->access_count = 0;
	MEM_UNLOCK(&mem->mem_list_lock);
	raw_spin_unlock(&mem_reserve_lock);

	if (!entry->page) {
		printk("rk_alloc_pages: null page in mem_free_list\n");
		*ret = false; // let the kernel allocate a page
		goto ret_null;
	}
	ClearPageUnevictable(entry->page);
	SetPageMemReserve(entry->page);
	set_page_private(entry->page, 0);
	//atomic_set(&entry->page->_count, 1);
	if (gfp_mask & __GFP_ZERO) { // clear_highpage in highmem.h
		void *kaddr = kmap_atomic(entry->page);
		clear_page(kaddr);
		kunmap_atomic(kaddr);
	}

	mem_dbg("rk_alloc_pages pid:%d (e:%lx, p:%lx, f:%x, c:%d - gfp:%x)\n", 
		current->pid, 
		(unsigned long)entry,
		(unsigned long)entry->page, 
		(unsigned int)entry->page->flags, entry->page->_count.counter, gfp_mask);
	*ret = true;

	return entry->page;

ret_null:
	return NULL;
}
	
int rk_free_pages(struct page *page, unsigned int order)
{
	struct mem_reserve_page* entry;
	mem_reserve_t mem;
	struct list_head *head, *shared_list;
	bool need_realloc= false;
	LIST_HEAD(entry_list);

	entry = page->rsv;
	mem_dbg("rk_free_pages (e:%lx, p:%lx, order:%d, f:%x, c:%d, ac:%d)%s\n", 
			(unsigned long)entry, (unsigned long)page, order, (unsigned int)page->flags, page->_count.counter,
			entry->access_count,
			list_empty(&entry->shared) ? "" : " - SHARED");

	/*if (order > 0) {
		printk("free_pages_hook: does not support > 1\n");
	}*/
	if (entry == NULL || entry->page != page) {
		printk("rk_free_pages: page is reserved but does not have correct entry addr\n");
		return -1;
	}
	// FIXME: EvictionLock flag should have been cleared before calling this function. 
	// Need to add an input parameter to check if eviction lock is used or not.
	if (PageEvictionLock(page)) { 
		need_realloc = true;
		ClearPageEvictionLock(page);
		mem_dbg("rk_free_pages: EVICTION LOCKED - page %lx\n", (unsigned long)page);
	}

	SetPageMemReserve(page);
	atomic_set(&page->_count, 1);

	// Check if entry is shared
	// - As the page is being freed now, it has been already unmapped from all reserves
#ifdef RSV_NO_SHARED_PAGE_CONSERVATION 
	need_realloc = true;
#endif
	raw_spin_lock(&mem_reserve_lock);
	head = shared_list = &entry->shared;
	do {
		mem = entry->mem;
		MEM_LOCK(&mem->mem_list_lock);
		if (entry != page->rsv) {
			// The page does not belong to entry->mem
			// The entry has only a link to the page
			if (need_realloc == false) {
				// Remove one page from mem_conserved_list,
				// and insert it to entry
				VM_BUG_ON(mem->mem_conserved_size <= 0);

				mem->mem_conserved_size--;
				entry->page = list_first_entry(&mem->mem_conserved_list, struct page, lru);
				entry->page->rsv = entry;
				list_del(&entry->page->lru);
				move_to_mem_free_list(entry, mem);
				mem_dbg("    - recover page: e:%lx, p:%lx\n", (unsigned long)entry, (unsigned long)entry->page);
			} 
			else {
				// Move entry from mem_used_list to entry_list for now.
				// This entry can be added to mem_free_list after allocating a new page.
				list_move_tail(&entry->list, &entry_list);
				mem->mem_used_size--;
				if (entry->active_used) mem->mem_active_size--;
				else mem->mem_inactive_size--;
			}
		}
		else {
			// The page is from entry->mem
			// Move it to mem_free_list
			move_to_mem_free_list(entry, mem);
		}
		MEM_UNLOCK(&mem->mem_list_lock);

		shared_list = entry->shared.next;
		INIT_LIST_HEAD(&entry->shared);
		entry = list_entry(shared_list, struct mem_reserve_page, shared);
	} while (shared_list != head);
	raw_spin_unlock(&mem_reserve_lock);

	// Allocate pages for entries in entry_list
	if (need_realloc) {
		mem_reserve_t cur_mem = NULL;
		// temporarily disable mem reserve during this (for alloc_page)
		if (current->rk_resource_set && current->rk_resource_set->mem_reserve) {
			cur_mem = current->rk_resource_set->mem_reserve->reserve;
			current->rk_resource_set->mem_reserve = NULL;
		}
		// realloc pages
		while (!list_empty(&entry_list)) {
			entry = list_first_entry(&entry_list, struct mem_reserve_page, list);
			mem = entry->mem;
			alloc_page_from_pagebins(mem, entry);

			raw_spin_lock(&mem_reserve_lock);
			MEM_LOCK(&mem->mem_list_lock);
			mem_dbg("rk_free_pages: alloc for EVICTION LOCK entry:%lx - page:%lx\n", (unsigned long)entry, (unsigned long)entry->page);
			if (entry->page) {
				entry->page->rsv = entry;
				// move to free list (member variables are already initialized)
				add_page_to_mem_free_list(entry, mem);
			}
			else {
				mem_dbg(" -> NULL\n");
				// This should not happen. 
				// Leave the entry alone so that it would not be used for any task.
				// The entry will be cleaned up when the reserve is deleted.
			}
			MEM_UNLOCK(&mem->mem_list_lock);
			raw_spin_unlock(&mem_reserve_lock);
		}
		// restore temporarily disabled mem reserve
		if (cur_mem) current->rk_resource_set->mem_reserve = cur_mem->rsv;
	}
	//atomic_inc(&page->_count);
	return 0;
}

asmlinkage int sys_rk_mem_reserve_create(int rd, mem_reserve_attr_t usr_mem_attr, mem_reserve_attr_t usr_aux_attr)
{
	rk_reserve_t		rsv;
	rk_resource_set_t	rset;
	mem_reserve_t		mem;
	mem_reserve_attr_data_t	mem_attr_data, aux_attr_data;
	mem_reserve_attr_t      attr;
	int mem_req_pages, aux_req_pages;
	int i, j;

	rk_sem_down();
	rset = resource_set_descriptor[rd];

	// Check input params
	// - usr_mem_attr: Parameters for memory reserve
	// - usr_aux_attr: Optional. Parameters for auxiliary memory allocation in memory reserve
	if (rset==NULL) {
		printk("sys_rk_mem_reserve_create: Mem reserves cannot be created for a Null resource set.\n");
		goto unlock_error;
	}
	if (usr_mem_attr == NULL) {
		printk("sys_rk_mem_reserve_create: Mem attributes must be specified for creating a mem reserve.\n");
		goto unlock_error;
	}
	if (copy_from_user(&mem_attr_data, usr_mem_attr, sizeof(mem_reserve_attr_data_t))) {
		printk("sys_rk_mem_reserve_create: Could not copy mem_attr into kernel space\n");
		goto unlock_error;
	}
	if (usr_aux_attr && copy_from_user(&aux_attr_data, usr_aux_attr, sizeof(mem_reserve_attr_data_t))) {
		printk("sys_rk_mem_reserve_create: Could not copy aux_attr into kernel space\n");
		goto unlock_error;
	}
	if (usr_aux_attr == NULL) {
		memset(&aux_attr_data, 0, sizeof(mem_reserve_attr_data_t));
	}
	if (mem_attr_data.mem_size == 0) {// || mem_attr_data.mem_size > mem_reserve_max_capacity) {
		printk("sys_rk_mem_reserve_create: Invalid memory reservation size\n");
		goto unlock_error;
	}
	if (mem_attr_data.reserve_mode != RSV_HARD && mem_attr_data.reserve_mode != RSV_FIRM) {
		printk("sys_rk_mem_reserve_create: Mem reserve mode should be RSV_HARD or RSV_FIRM\n");
		goto unlock_error;
	}
	mem_req_pages = (mem_attr_data.mem_size + PAGE_SIZE - 1) / PAGE_SIZE;
	if (mem_req_pages < MEM_RSV_EVICT_SIZE * 2) {
		printk("sys_rk_mem_reserve_create: Size of mem rsv is too small. (Min = %lu)\n", MEM_RSV_EVICT_SIZE * 2 * PAGE_SIZE);
		goto unlock_error;
	}
	aux_req_pages = (aux_attr_data.mem_size + PAGE_SIZE - 1) / PAGE_SIZE;
	if (usr_aux_attr && aux_req_pages < MEM_RSV_EVICT_SIZE * 2) {
		printk("sys_rk_mem_reserve_create: Size of aux rsv is too small. (Min = %lu)\n", MEM_RSV_EVICT_SIZE * 2 * PAGE_SIZE);
		goto unlock_error;
	}
	if (mem_reserve_max_capacity - mem_reserve_usage < mem_req_pages + aux_req_pages) {
		printk("sys_rk_mem_reserve_create: admission test for mem reserve failed (max pages:%d, current usage:%d, req:%d)\n",
			mem_reserve_max_capacity, mem_reserve_usage, mem_req_pages + aux_req_pages);
		goto unlock_error;
	}
	// Check color indices
	attr = &mem_attr_data;
	do {
		int req_pages;
		if (attr->nr_colors <= 0 || attr->nr_colors > MEM_RSV_COLORS) {
			attr->nr_colors = MEM_RSV_COLORS;
			for (i = 0; i < MEM_RSV_COLORS; i++) attr->colors[i] = i;
		}
		for (i = 0; i < attr->nr_colors; i++) {
			if (attr->colors[i] < MEM_RSV_COLORS) continue;

			printk("sys_rk_mem_reserve_create: invalid color (%s - colors[%d]:%d)\n",
				attr == &mem_attr_data ? "memrsv" : "auxrsv",
				i, attr->colors[i]);
			goto unlock_error;
		}
		if (attr->nr_bank_colors <= 0 || attr->nr_bank_colors > MEM_RSV_BANK_COLORS) {
			attr->nr_bank_colors = MEM_RSV_BANK_COLORS;
			for (i = 0; i < MEM_RSV_BANK_COLORS; i++) attr->bank_colors[i] = i;
		}
		for (i = 0; i < attr->nr_bank_colors; i++) {
			if (attr->bank_colors[i] < MEM_RSV_BANK_COLORS) continue;

			printk("sys_rk_mem_reserve_create: invalid bank color value(%s - bank_colors[%d]:%d)\n",
				attr == &mem_attr_data ? "memrsv" : "auxrsv",
				i, attr->bank_colors[i]);
			goto unlock_error;
		}
		req_pages = (attr == &mem_attr_data) ? mem_req_pages : aux_req_pages;
		if (!is_nr_pages_in_pagebins(attr, req_pages)) {
			printk("sys_rk_mem_reserve_create: not enough pages in pagebins (%s)\n", 
				attr == &mem_attr_data ? "memrsv" : "auxrsv");
			goto unlock_error;
		}
		attr = (attr == &mem_attr_data) ? &aux_attr_data : NULL;
	} while (attr && attr->mem_size > 0);

    	/* create mem reserve object */
	mem = kmalloc(sizeof(struct mem_reserve), GFP_ATOMIC);
        memset(mem, 0, sizeof(struct mem_reserve));

	mem->mem_res_attr = mem_attr_data;
	mem->aux_res_attr = aux_attr_data;
	mem->mem_total_size = mem_req_pages + aux_req_pages; 
	mem->mem_aux_size = aux_req_pages;
	mem->reserved_pages = vmalloc(sizeof(struct mem_reserve_page) * mem->mem_total_size);
	if (!mem->reserved_pages) {
		printk("sys_rk_mem_reserve_create: Failed to create mem reserve pool\n");
		kfree(mem);
		goto unlock_error;
	}
	mem->mem_free_list = kmalloc(sizeof(struct list_head*) * MEM_RSV_COLORS, GFP_ATOMIC);
	mem->mem_free_size_detail = kmalloc(sizeof(int*) * MEM_RSV_COLORS, GFP_ATOMIC);
	if (!mem->mem_free_list || !mem->mem_free_size_detail) {
		printk("sys_rk_mem_reserve_create: Failed to create mem free list\n");
		if (mem->mem_free_list) kfree(mem->mem_free_list);
		if (mem->mem_free_size_detail) kfree(mem->mem_free_size_detail);
		vfree(mem->reserved_pages);
		kfree(mem);
		goto unlock_error;
	}
	for (i = 0; i < MEM_RSV_COLORS; i++) {
		mem->mem_free_list[i] = kmalloc(sizeof(struct list_head) * MEM_RSV_BANK_COLORS, GFP_ATOMIC);
		mem->mem_free_size_detail[i] = kmalloc(sizeof(int) * MEM_RSV_BANK_COLORS, GFP_ATOMIC);
		if (!mem->mem_free_list[i] || !mem->mem_free_size_detail[i]) {
			printk("sys_rk_mem_reserve_create: Failed to create mem free list\n");
			for (j = 0; j <= i; j++) {
				if (mem->mem_free_list[j]) kfree(mem->mem_free_list[j]);
				if (mem->mem_free_size_detail[j]) kfree(mem->mem_free_size_detail[j]);
			}
			kfree(mem->mem_free_list);
			kfree(mem->mem_free_size_detail);
			vfree(mem->reserved_pages);
			kfree(mem);
			goto unlock_error;
		}
		for (j = 0; j < MEM_RSV_BANK_COLORS; j++) {
			INIT_LIST_HEAD(&mem->mem_free_list[i][j]);
			mem->mem_free_size_detail[i][j] = 0;
		}
	}

	INIT_LIST_HEAD(&mem->mem_link);
	INIT_LIST_HEAD(&mem->mem_active_list);
	INIT_LIST_HEAD(&mem->mem_inactive_list);
	INIT_LIST_HEAD(&mem->mem_conserved_list);

	raw_spin_lock_init(&mem->mem_list_lock);
	
	// Print allocated color info
	attr = &mem_attr_data;
	do {
		printk("%sCOLOR(NR:%d) : ", attr == &mem_attr_data ? "" : "AUX ", attr->nr_colors);
		for (i = 0; i < attr->nr_colors; i++) {
			printk("%d ", attr->colors[i]);
		}
		printk("\n");

		printk("%sBANK COLOR(NR:%d) : ", attr == &mem_attr_data ? "" : "AUX ", attr->nr_bank_colors);
		for (i = 0; i < attr->nr_bank_colors; i++) {
			printk("%d ", attr->bank_colors[i]);
		}
		printk("\n");
		attr = (attr == &mem_attr_data) ? &aux_attr_data : NULL;
	} while (attr && attr->mem_size > 0);

	// Allocate page frames and add them to free list
	for (i = 0; i < mem->mem_total_size; i++) {
		alloc_page_from_pagebins(mem, &mem->reserved_pages[i]);
		if (!mem->reserved_pages[i].page) {
			// FAIL: dealloc and return
			printk("sys_rk_mem_reserve_create: Failed to allocate page for mem reserve pool\n");

			for (j = 0; j < i; j++) {
				ClearPageMemReserve(mem->reserved_pages[i].page);
				free_page_to_pagebins(mem->reserved_pages[i].page);
			}
			for (j = 0; j < MEM_RSV_COLORS; j++) {
				kfree(mem->mem_free_list[j]);
				kfree(mem->mem_free_size_detail[j]);
			}			
			kfree(mem->mem_free_list);
			kfree(mem->mem_free_size_detail);
			vfree(mem->reserved_pages);
			kfree(mem);
			goto unlock_error;
		}
		// PageMemReserve : test
		// ClearPageMemReserve : clear
		mem->reserved_pages[i].mem = mem;
		mem->reserved_pages[i].active_used = 0;
		mem->reserved_pages[i].executable = false;
		mem->reserved_pages[i].access_count = 0;
		mem->reserved_pages[i].page->rsv = &mem->reserved_pages[i];
		INIT_LIST_HEAD(&mem->reserved_pages[i].shared);

		add_page_to_mem_free_list(&mem->reserved_pages[i], mem);
	}

    	/* create generic reserve object */
    	rsv = rk_reserve_create(rset, RSV_MEM);
    	rsv->reserve = mem;
    	rsv->operations = &mem_reserve_ops;
    	mem->rsv = rsv;

	rset->mem_reserve = rsv;
	list_add_tail(&mem->mem_link, &mem_reserves_head);

	mem_reserve_usage += mem->mem_total_size;

	rk_procfs_reserve_create(rsv, 0);
	rk_sem_up();

	printk("sys_rk_mem_reserve_create: %dpages (max:%d, current reserves usage:%d)\n", 
		mem->mem_total_size, mem_reserve_max_capacity, mem_reserve_usage);
	return RK_SUCCESS;

unlock_error:
	rk_sem_up();
	return RK_ERROR;
}


void rk_mem_reserve_delete(mem_reserve_t mem)
{
	struct mem_reserve_page *entry, *safe;
	struct page *page, *newpage;
	mem_reserve_t cur_mem = NULL;
	int i, j;

	if (mem == NULL) {
		printk("rk_mem_reserve_delete: Deleting a NULL reserve\n");
		return;
	}
	printk("rk_mem_reserve_delete: (now) free_list:%d, used_list:%d, total_size:%d \n", 
		mem->mem_free_size, mem->mem_used_size, mem->mem_total_size);

	// sys_rk_mem_reserve_show_color_info(-1);
	mem->rsv->reserve = NULL;	/* After this step, no way to reach the reserve */
	list_del(&mem->mem_link);

	// temporarily disable mem reserve during this (for alloc_page)
	if (current->rk_resource_set && current->rk_resource_set->mem_reserve) {
		cur_mem = current->rk_resource_set->mem_reserve->reserve;
		current->rk_resource_set->mem_reserve = NULL;
	}
	for (i = 0; i < 2; i++) {
		struct list_head *mem_list;
		if (i == 0) mem_list = &mem->mem_active_list;
		else mem_list = &mem->mem_inactive_list;

		list_for_each_entry_safe(entry, safe, mem_list, list) {
			int ret = -1;
			page = entry->page;
			if (!page) continue;

			mem_dbg("delete page:%lx, f:%x, c:%d, mc:%d, rsv:%d, %s -> entry %d\n", 
				(unsigned long)page, 
				(unsigned int)page->flags, 
				page_count(page), 
				page_mapcount(page), 
				PageMemReserve(page), 
				page->mapping == NULL ? "N/A" 
					: (((unsigned long)page->mapping & 0x1) 
					? "Anon" : "File"),
				page_category(entry));

			if (!PageReserved(page) && !isolate_lru_page(page)) {
				LIST_HEAD(migrate_list);
				list_add_tail(&page->lru, &migrate_list);
				newpage = alloc_page(GFP_HIGHUSER_MOVABLE);

				if ((ret = rk_migrate_page(page, newpage)) == 0) {
					mem_dbg("detach: migration ok : old-%lx(f:%lx, rsv:%lx), new-%lx(f:%lx, rsv:%lx)\n", 
						(unsigned long)page, page->flags, (unsigned long)page->rsv,
						(unsigned long)newpage, newpage->flags, (unsigned long)newpage->rsv);
					// If rk_migrate_page() succeeds, it will call rk_free_pages(page) 
					// which calls move_to_mem_free_list().
					// But this page is not in used list,
					// so we recover counters here.
					continue;
				}
				else {
					// If rk_migrate_page() fails, it will call free_hot_cold_page(newpage).
					newpage = NULL;
				}
			}
			if (ret) {
				// clear unmigratable page
				page->rsv = NULL;
				ClearPageMemReserve(page);
				ClearPageEvictionLock(page);
				// fill the pagebin-pool
				newpage = alloc_page(GFP_HIGHUSER_MOVABLE);
				SetPageMemReserve(newpage); // no need to page_lock()
				free_page_to_pagebins(newpage);
			}
		}
	}
	// restore temporarily disabled mem reserve
	if (cur_mem) current->rk_resource_set->mem_reserve = cur_mem->rsv;
	
	for (i = 0; i < MEM_RSV_COLORS; i++) {
		for (j = 0; j < MEM_RSV_BANK_COLORS; j++) {
			list_for_each_entry(entry, &mem->mem_free_list[i][j], list) {
				page = entry->page;
				if (!page) continue;

				ClearPageMemReserve(page);
				ClearPageEvictionLock(page);
				page->rsv = NULL;

				/*printk("rsv_delete: %lx, f:%lx, c:%d\n", 
					(unsigned long)page, 
					page->flags, 
					page->_count.counter);*/
				free_page_to_pagebins(page);
			}
		}
	}

	for (i = 0; i < MEM_RSV_COLORS; i++) {
		kfree(mem->mem_free_list[i]);
		kfree(mem->mem_free_size_detail[i]);
	}
	kfree(mem->mem_free_list);
	kfree(mem->mem_free_size_detail);
	vfree(mem->reserved_pages);

	mem_reserve_usage -= mem->mem_total_size;
	printk("rk_mem_reserve_delete: %dpages, peak usage:%d (max:%d, current reserves usage:%d)\n", 
		mem->mem_total_size, mem->mem_peak_size, mem_reserve_max_capacity, mem_reserve_usage);
	memset(mem, 0, sizeof(struct mem_reserve));
	kfree(mem);
	//sys_rk_mem_reserve_show_color_info(-1);
}

asmlinkage int sys_rk_mem_reserve_delete(int rd)
{
	rk_resource_set_t	rset;
	rk_reserve_t 		mem;

	rk_sem_down();
	rset = resource_set_descriptor[rd];

	if(rset == NULL) {
		printk("sys_rk_mem_reserve_delete: cannot find resource set\n");
		rk_sem_up();
		return RK_ERROR;
	}	
	mem = rset->mem_reserve;
	rset->mem_reserve = NULL;

	rk_delete_reserve(mem, 0);
	rk_sem_up();

	return RK_SUCCESS;
}


//////////////////////////////////////////////////////////////////////////////
// 
// RK Shared Page Eviction Lock
//
// TODO: This code was initially developed for Linux 2.6.32.38, but not 
// tested on 3.x.x. Need to test it before use.
// 
//////////////////////////////////////////////////////////////////////////////

// Helper function of make_task_pages_present()
long make_task_pages_present_vma_range(struct task_struct *p,
					struct vm_area_struct *vma, 
					unsigned long start, unsigned long end)
{
        struct mm_struct *mm = vma->vm_mm;
        unsigned long addr = start;
        //struct page *pages[16]; /* 16 gives a reasonable batch */
        int nr_pages = (end - start) / PAGE_SIZE;
        int ret = 0;
        int gup_flags;

        VM_BUG_ON(start & ~PAGE_MASK);
        VM_BUG_ON(end   & ~PAGE_MASK);
        VM_BUG_ON(start < vma->vm_start);
        VM_BUG_ON(end   > vma->vm_end);
        VM_BUG_ON(!rwsem_is_locked(&mm->mmap_sem));

        gup_flags = FOLL_TOUCH;
        if (vma->vm_flags & VM_WRITE)
                gup_flags |= FOLL_WRITE;

        /*
         * We want mlock to succeed for regions that have any permissions
         * other than PROT_NONE.
         */
        if (vma->vm_flags & (VM_READ | VM_WRITE | VM_EXEC))
                gup_flags |= FOLL_FORCE;

        while (nr_pages > 0) {
                cond_resched();

                /*
                 * get_user_pages makes pages present if we are
                 * setting mlock. and this extra reference count will
                 * disable migration of this page.  However, page may
                 * still be truncated out from under us.
                 */
		ret = get_user_pages(p, mm, addr,
				//min_t(int, nr_pages, ARRAY_SIZE(pages)),
				min_t(int, nr_pages, 16),
				gup_flags & FOLL_WRITE, gup_flags & FOLL_FORCE,
				//pages, NULL);
				NULL, NULL);
                /*
                 * This can happen for, e.g., VM_NONLINEAR regions before
                 * a page has been allocated and mapped at a given offset,
                 * or for addresses that map beyond end of a file.
                 * We'll mlock the pages if/when they get faulted in.
                 */
                if (ret < 0)
                        break;

                lru_add_drain();        /* push cached pages to LRU */

		/*
                for (i = 0; i < ret; i++) {
                        struct page *page = pages[i];
                        put_page(page); // ref from get_user_pages() 
                }*/

                addr += ret * PAGE_SIZE;
                nr_pages -= ret;
                ret = 0;
        }

        return ret;     /* 0 or negative error code */
}

// Refer to mlock_fixup() 
int make_task_pages_present_fixup(struct task_struct *p, struct vm_area_struct *vma, 
	struct vm_area_struct **prev, unsigned long start, unsigned long end)
{
	struct mm_struct *mm;
	pgoff_t pgoff;
	int ret = 0;

	mm = p->mm;

	if ((vma->vm_flags & VM_SPECIAL) 
		|| is_vm_hugetlb_page(vma) || vma == get_gate_vma(p->mm))
		goto out;

	pgoff = vma->vm_pgoff + ((start - vma->vm_start) >> PAGE_SHIFT);
	*prev = vma_merge(mm, *prev, start, end, vma->vm_flags, vma->anon_vma,
		vma->vm_file, pgoff, vma_policy(vma));
	if (*prev) {
		vma = *prev;
		goto success;
	}
	if (start != vma->vm_start) {
		if ((ret = split_vma(mm, vma, start, 1)))
			goto out;
	}
	if (end != vma->vm_end) {
		if ((ret = split_vma(mm, vma, end, 0)))
			goto out;
	}
success:
	//nr_pages = (end - start) >> PAGE_SHIFT;
	// refer to __mlock_vma_pages_range(vma, start, end);
	ret = make_task_pages_present_vma_range(p, vma, start, end);
out:
	*prev = vma;
	return ret;
}

// Make pages present (reference fn: do_mlockall, mlock_fixup)
// Called by mem_reserve_attach_process()
void make_task_pages_present(struct task_struct *p)
{
	struct mm_struct *mm;
	struct vm_area_struct *vma, *prev = NULL;
	unsigned long start, end;

	down_write(&p->mm->mmap_sem);
	
	mm = p->mm;
	for (vma = mm->mmap; vma; vma = prev->vm_next) {
		start = vma->vm_start;
		end = vma->vm_end;
		
		make_task_pages_present_fixup(p, vma, &prev, start, end);
	}
	up_write(&p->mm->mmap_sem);
}

// Refer to do_mlock
int make_task_pages_present_range(struct task_struct *p, unsigned long start, size_t len)
{
	unsigned long nstart, end, nend;
	struct vm_area_struct *vma, *prev;
	int ret = 0;

	VM_BUG_ON(start & ~PAGE_MASK);
	VM_BUG_ON(len != PAGE_ALIGN(len));
	end = start + len;
	if (end < start) return -1;

	down_write(&p->mm->mmap_sem);
	vma = find_vma_prev(p->mm, start, &prev);
	if (!vma || vma->vm_start > start) {
		ret = -1;
		goto sem_unlock;
	}
	if (start > vma->vm_start) prev = vma;

	for (nstart = start ; ; ) {
		// vma->vm_start <= nstart < vma->vm_end 
		nend = vma->vm_end;
		if (nend > end) nend = end;
		ret = make_task_pages_present_fixup(p, vma, &prev, nstart, nend);
		if (ret) break;

		if (nstart < prev->vm_end) nstart = prev->vm_end;
		if (nstart >= end) break;

		vma = prev->vm_next;
		if (!vma || vma->vm_start != nstart) {
			ret = -1;
			break;
		}
	}
sem_unlock: 
	up_write(&p->mm->mmap_sem);
	return ret;
}

asmlinkage int sys_rk_mem_reserve_eviction_lock(pid_t pid, 
			unsigned long vaddr, size_t size, bool lock)
{
	struct mm_struct *mm;
	struct vm_area_struct *mmap;
	struct page *page;
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	struct mem_reserve_page* entry;
	struct task_struct *p;
	mem_reserve_t mem;
	int n_req_pages, n_locked_pages, i;
	unsigned long aligned_start, aligned_end, aligned_len;

	p = find_task_by_pid_ns(pid, &init_pid_ns);
	if (p == NULL) {
		printk("sys_rk_mem_reserve_eviction_lock: cannot find task with pid %d\n", pid);
		return -1;
	}
	if (p->rk_resource_set == NULL) {
		printk("sys_rk_mem_reserve_eviction_lock: pid %d does not have a valid resource set\n", pid);
		return -1;
	}
	if (p->rk_resource_set->mem_reserve == NULL || p->rk_resource_set->mem_reserve->reserve == NULL) {
		printk("sys_rk_mem_reserve_eviction_lock: pid %d does not have a valid memory reservation\n", pid);
		return -1;
	}
	if (size <= 0) {
		printk("sys_rk_mem_reserve_eviction_lock: requested size %lu is invalid\n", (unsigned long)size);
		return -1;
	}
	mem = p->rk_resource_set->mem_reserve->reserve;
	aligned_start = vaddr & PAGE_MASK; // (vaddr / PAGE_SIZE) * PAGE_SIZE;
	aligned_len = PAGE_ALIGN(size + (vaddr & ~PAGE_MASK));
	aligned_end = aligned_start + aligned_len; //((vaddr + size + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE;
	n_req_pages = aligned_len / PAGE_SIZE; //(aligned_end - aligned_start) / PAGE_SIZE;
	n_locked_pages = 0;

	if (lock == false) {
		printk("sys_rk_mem_reserve_eviction_lock: we do not support unlock yet\n");
		return -1;
	}	
	
	// Simple Admission Test
	if (mem->mem_total_size < n_req_pages) {
		printk("sys_rk_mem_reserve_eviction_lock: requested block(start:%lu, size:%lu) is larger \
			than the memory reservation(pages: %d)\n", vaddr, (unsigned long)size, mem->mem_total_size);
		return -1;
	}
	/*
	if (mem->mem_free_size < n_req_pages) {
		printk("sys_rk_mem_reserve_eviction_lock: requested block(start:%lu, size:%lu) is larger \
			than free_list of memory reservation(free pages: %d)\n", vaddr, size, mem->mem_free_size);
		return -1;
	}*/

	// Make enough free space
	for (i = 0; i < 3; i++) {
		if (mem->mem_free_size >= n_req_pages) break;
		evict_reserved_pages(mem, n_req_pages);
	}
	if (mem->mem_free_size < n_req_pages) {
		printk("sys_rk_mem_reserve_eviction_lock: cannot make enough free pages(req:%d, free:%d)\n", n_req_pages, mem->mem_free_size);
		return -1;
	}

	// Make requested pages present
	//if (make_task_pages_present_range(p, vaddr, size) < 0) {
	if (make_task_pages_present_range(p, aligned_start, aligned_len) < 0) {
		printk("sys_rk_mem_reserve_eviction_lock: cannot make present requested pages\n");
		return -1;
	}

	// Attach loaded & non-reserved pages
	attach_pages_to_mem_reserve(mem, p, true);

	// Set PG_mem_elock flag
	mm = p->active_mm;
	down_read(&mm->mmap_sem);
	mmap = mm->mmap;
	//mem_dbg("aligned start : %lx - end : %lx\n", aligned_start, aligned_end);
	while (mmap) {
		unsigned long n, end;
		if (mmap->vm_flags & (VM_IO | VM_PFNMAP)) goto next_vma;
		if (aligned_end < mmap->vm_start || aligned_start > mmap->vm_end) {
			//mem_dbg("vm start : %lx - end :%lx : PASS\n", mmap->vm_start, mmap->vm_end);
			goto next_vma;
		}
		if (aligned_start > mmap->vm_start) n = aligned_start;
		else n = mmap->vm_start;
		if (aligned_end < mmap->vm_end) end = aligned_end;
		else end = mmap->vm_end;

		//mem_dbg("vm start : %lx - end :%lx -> %lx - %lx\n", mmap->vm_start, mmap->vm_end, n, end);
		for (; n < end; n += PAGE_SIZE) {
			pgd = pgd_offset(mmap->vm_mm, n);
			if (pgd_none(*pgd) || !pgd_present(*pgd)) continue;
			pud = pud_offset(pgd, n);
			if (pud_none(*pud) || !pud_present(*pud)) continue;
			pmd = pmd_offset(pud, n);
			if (pmd_none(*pmd) || !pmd_present(*pmd)) continue;
			pte = pte_offset_map(pmd, n);
			if (pte_none(*pte) || !pte_present(*pte)) goto unmap;

			page = pte_page(*pte);

			if (!PageMemReserve(page)) {
				mem_dbg("eviction lock: vaddr %lx - page:%lx is not reserved\n", 
					n, (unsigned long)page);
				goto unmap;
			}
			// check page ownership
			raw_spin_lock(&mem_reserve_lock);
			entry = get_task_page_ownership(mem, page->rsv);
			if (entry == NULL) {
				mem_dbg("eviction lock: vaddr %lx - page:%lx is not owned by pid %d\n", 
					n, (unsigned long)page, p->pid);
				goto unlock;
			}

			// Now, page is reserved and owned by the task
			if (PageEvictionLock(page)) {
				// Page is already locked (locked by someone else)
				mem_dbg("eviction lock: vaddr %lx - page %lx already locked & owned by %d\n", 
					n, (unsigned long)page, p->pid);
				n_locked_pages++;
				goto unlock;
			}
			// Set eviction lock
			lock_page(page);
			SetPageEvictionLock(page);
			unlock_page(page);
			
#ifndef RSV_NO_SHARED_PAGE_CONSERVATION
			// Shared Page Conservation
			// Check if the page is shared with other reserves
			if (!list_empty(&entry->shared)) {
				struct list_head *head, *shared_list;
				LIST_HEAD(page_list);
				struct page *tmp_page;

				// Remove conserved pages for this page from each reserves
				head = shared_list = &entry->shared;
				do {
					if (entry != page->rsv) { // has conserved page
						mem_reserve_t shr = entry->mem;

						VM_BUG_ON(shr->mem_conserved_size <= 0);
						shr->mem_conserved_size--;
						tmp_page = list_first_entry(&shr->mem_conserved_list, struct page, lru);
						list_move(&tmp_page->lru, &page_list);
					}
					shared_list = entry->shared.next;
					entry = list_entry(shared_list, struct mem_reserve_page, shared);
				} while (shared_list != head);
				raw_spin_unlock(&mem_reserve_lock);				
				// Deallocate free pages
				while (!list_empty(&page_list)) {
					tmp_page = list_first_entry(&page_list, struct page, lru);
					list_del(&tmp_page->lru);
					ClearPageMemReserve(tmp_page);
					tmp_page->rsv = NULL;
					free_page_to_pagebins(tmp_page);
				}
				mem_dbg("eviction lock: LOCK - vaddr %lx - page %lx (shared)\n", 
					n, (unsigned long)page);
			} else 
#endif
			{
				raw_spin_unlock(&mem_reserve_lock);
				mem_dbg("eviction lock: LOCK - vaddr %lx - page %lx\n", 
					n, (unsigned long)page);
			}
			n_locked_pages++;
			goto unmap;
unlock:
			raw_spin_unlock(&mem_reserve_lock);
unmap:
			pte_unmap(pte);
		}
next_vma:
		mmap = mmap->vm_next;
	}
	up_read(&mm->mmap_sem);

	printk("eviction_lock: %d pages locked\n", n_locked_pages);
	return 0;
}
// End of RK Shared Page Eviction Lock
//////////////////////////////////////////////////////////////////////////////


int sys_rk_mem_reserve_show_color_info(int color_idx)
{
	// hyos: for coloring test
	if (color_idx == -1) {
		int i, j, total = 0, min_bin = INT_MAX, max_bin = INT_MIN;
		for (i = 0; i < MEM_RSV_COLORS; i++) {
			//printk(" - cache %d\n", i);
			for (j = 0; j < MEM_RSV_BANK_COLORS; j++) {
				//printk("       - bank %d : %d pages\n", j, memrsv_pagebins_counter[i][j]);
				total += memrsv_pagebins_counter[i][j];
				if (memrsv_pagebins_counter[i][j] > max_bin) max_bin = memrsv_pagebins_counter[i][j];
				if (memrsv_pagebins_counter[i][j] < min_bin) min_bin = memrsv_pagebins_counter[i][j];
			}
		}
		printk(" - total: %d pages (size: %ld MB, maxbin: %d, minbin: %d)\n", 
			total, total * PAGE_SIZE / (1024 * 1024), max_bin, min_bin);
	}
	else if (color_idx >= 0 && color_idx < MEM_RSV_COLORS) {
		int j;
		for (j = 0; j < MEM_RSV_BANK_COLORS; j++) {
			printk(" - cache %d, bank %d : %d pages\n", color_idx, j, memrsv_pagebins_counter[color_idx][j]);
		}
	}
	else {
		return RK_ERROR;
	}
	return RK_SUCCESS;
}

// for procfs
int mem_reserve_read_proc(rk_reserve_t rsv, char *buf)
{
	int i;
	char *p = buf;
	struct mem_reserve mem;
	mem_reserve_attr_t attr;

	rk_sem_down();
	
	if (rsv == NULL || rsv->reserve == NULL) {
		rk_sem_up();
		return 0;
	}
	mem = *(mem_reserve_t)rsv->reserve;
	attr = &mem.mem_res_attr;

	rk_sem_up();

	p += sprintf(p, "mem_size     : %llu\n", attr->mem_size);
	p += sprintf(p, "rsv_mode     : %d\n", attr->reserve_mode);

	p += sprintf(p, "nr_colors    : %d {", attr->nr_colors);
	for (i = 0; i < attr->nr_colors; i++) {
		p += sprintf(p, " %d", attr->colors[i]);
	}
	p += sprintf(p, " }\n");
	p += sprintf(p, "total pages  : %d\n", mem.mem_total_size);
	p += sprintf(p, "free pages   : %d\n", mem.mem_free_size);
	p += sprintf(p, "used pages   : %d\n", mem.mem_used_size);
	p += sprintf(p, "aux. pages   : %d\n", mem.mem_aux_size);
	p += sprintf(p, "peak usage   : %d\n", mem.mem_peak_size);

	return (p - buf);
}

// for rk_trace
int mem_reserve_get_nr_colors(void)
{
	return MEM_RSV_COLORS;
}
int mem_reserve_get_color_idx(struct page* page)
{
	return MEM_RSV_COLORIDX(page);
}
int mem_reserve_get_nr_bank_colors(void)
{
	return MEM_RSV_BANK_COLORS;
}
int mem_reserve_get_bank_color_idx(struct page* page)
{
	return MEM_RSV_BANK_COLORIDX(page);
}



//////////////////////////////////////////////////////////////////////////////
// 
// RK vColoring
//
//////////////////////////////////////////////////////////////////////////////

#ifdef RK_VIRT_SUPPORT

#ifdef CONFIG_X86
#include <linux/kvm_host.h>
// From arch/x86/kvm/kvm_cache_regs.h
static inline unsigned long kvm_read_cr3(struct kvm_vcpu *vcpu)
{        
	if (!test_bit(VCPU_EXREG_CR3, (ulong *)&vcpu->arch.regs_avail))
		kvm_x86_ops->decache_cr3(vcpu);
	return vcpu->arch.cr3;
}        

static inline unsigned long gva_to_gpa(struct kvm_vcpu *vcpu, unsigned long gva)
{
	return vcpu->arch.walk_mmu->gva_to_gpa(vcpu, gva, 0, NULL);
}

// 
// rk_change_page_color
// : Change the color of a page that has been already attached 
//
// - color_idx: Index of attr->colors[] array
//
static int rk_change_page_color(mem_reserve_t mem, struct page *old_page, mem_reserve_attr_t attr, int color_idx)
{
	struct page *new_page;
	struct mem_reserve_page *old_entry, *new_entry;
	LIST_HEAD(entry_list);
	int ret;

	raw_spin_lock(&mem_reserve_lock);
	MEM_LOCK(&mem->mem_list_lock);
	
	// Check if page is part of memrsv
	old_entry = get_task_page_ownership(mem, old_page->rsv);
	if (!PageMemReserve(old_page) || !old_entry) {
		MEM_UNLOCK(&mem->mem_list_lock);
		raw_spin_unlock(&mem_reserve_lock);
		return RK_ERROR;
	}

	// Get target page entry corresponding to the color_idx of aux_res_attr
	new_entry = __page_in_mem_free_list(mem, attr->colors[color_idx], 
		    attr->bank_colors[mem->next_bank_color_to_tasks]);
	if (++(mem->next_bank_color_to_tasks) >= attr->nr_bank_colors)
		mem->next_bank_color_to_tasks = 0;
	if (!new_entry) {
		MEM_UNLOCK(&mem->mem_list_lock);
		raw_spin_unlock(&mem_reserve_lock);
		return RK_ERROR;
	}
	new_page = new_entry->page;
	if (!new_page) {
		MEM_UNLOCK(&mem->mem_list_lock);
		raw_spin_unlock(&mem_reserve_lock);
		return RK_ERROR;
	}
	del_page_from_mem_free_list(new_entry, mem); // Detach new_entry from mem_free_list	

	// Now we migrate old_page to new_page
	list_add(&new_entry->list, &entry_list); // Add it to a temporary list

	if (!isolate_lru_page(old_page)) {
		LIST_HEAD(page_list);

		list_add(&old_page->lru, &page_list);
		MEM_UNLOCK(&mem->mem_list_lock);
		raw_spin_unlock(&mem_reserve_lock);

		// No need to worry about race here
		// - No other tasks can attach the same page while we release spinlock
		// - The callers of this function is protected by mm->mmap_sem
		if ((ret = rk_migrate_page(old_page, new_page)) == 0) {
			// If rk_migrate_page() succeeds, it calls free_hot_cold_page(old_page) 
			// to free 'old_page' and 'old_entry' 

			// Move 'new_entry' to mem_used_list
			raw_spin_lock(&mem_reserve_lock);
			MEM_LOCK(&mem->mem_list_lock);
			add_page_to_mem_used_list(new_entry, mem);
		}
		else {
			// If rk_migrate_page() fails, it calls rk_free_pages(new_page) 
			// that in turn calls move_to_mem_free_list(new_entry). 
			// As 'new_entry' was not in mem_used_list, we need to fix mem_used_size here.
			raw_spin_lock(&mem_reserve_lock);
			MEM_LOCK(&mem->mem_list_lock);
			mem->mem_used_size++;
			mem->mem_inactive_size++; // cuz entry->active_used == 0
		}
		mem_dbg("rk_change_page_color: from:%lx, to:%lx, err:%d, free:%d, used:%d\n", 
			(unsigned long)old_page, (unsigned long)new_page, ret, mem->mem_free_size, mem->mem_used_size);
		//printk("rk_change_page_color: p:%lx, f:%lx\n", (unsigned long)page, page->flags);
	}
	MEM_UNLOCK(&mem->mem_list_lock);
	raw_spin_unlock(&mem_reserve_lock);

	return RK_SUCCESS;
}

// 
// rk_mem_reserve_assign_guest_task_colors
// : Assign or reclaim aux colors to a guest task
//
// - key: For security purpose
// - color_bits: Indices of auxrsv colors to be assigned. 
//       If zero, all colors previously assigned will be reclaimed.
//
int rk_mem_reserve_assign_guest_task_colors(struct kvm_vcpu *vcpu, unsigned long key, unsigned long color_bits)
{
	unsigned long i, j, k, l;
	struct mm_struct *mm;
	struct vm_area_struct *mmap;
	mem_reserve_t mem;
	unsigned long gpgd_base, gpud_base, gpmd_base, gpte_base;
	unsigned long color_idx;

	pgd_t *gpgd, *hpgd;
	pud_t *gpud, *hpud;
	pmd_t *gpmd, *hpmd;
	pte_t *gpte, *hpte;

	gpgd_base = kvm_read_cr3(vcpu);
	printk("assign_guest_task_colors: key %lu, color_bits %lx, guest PGD %lx\n", key, color_bits, gpgd_base);

	// TODO: Check key validity 
	if (key == 1) {
		// Prints out guest task ptes for debug purpose
		return rk_mem_reserve_traverse_guest_page_table(vcpu);
	}
	
	mm = current->active_mm;
	if (mm) mmap = mm->mmap;
	else {
		printk("assign_guest_task_colors: WARNING - mmap is NULL (pid %d)\n", current->pid);
		return RK_ERROR;
	}
	if (!current->rk_resource_set || !current->rk_resource_set->mem_reserve) {
		printk("assign_guest_task_colors: Error - No memory reservation\n");
		return RK_ERROR;
	}
	mem = current->rk_resource_set->mem_reserve->reserve;
	
	// Check color indices
	color_idx = 0;
	if (color_bits) {
		unsigned long color_mask;
		if (mem->aux_res_attr.mem_size == 0) {
			printk("assign_guest_task_colors: Error - No auxrsv\n");
			return RK_ERROR;
		}
		color_mask = (1ULL << mem->aux_res_attr.nr_colors) - 1;
		color_bits &= color_mask;
		if (!color_bits) {
			printk("assign_guest_task_colors: Error - Invalid color_bits\n");
			return RK_ERROR;
		}
		while ((color_bits & (1 << color_idx)) == 0) color_idx++;
	}

	down_read(&mm->mmap_sem);
	for (i = 0; i < PTRS_PER_PGD; ++i) {
		gpgd = ((pgd_t*)gfn_to_hva(vcpu->kvm, gpa_to_gfn(gpgd_base))) + i;
		if (pgd_none(*gpgd) || !pgd_present(*gpgd)) continue;
		if (!(pgd_val(*gpgd) & _PAGE_USER)) continue; // x86

		for (j = 0; j < PTRS_PER_PUD; ++j) {
			gpud_base = (unsigned long)pgd_val(*gpgd) & PTE_PFN_MASK;
			gpud = ((pud_t*)gfn_to_hva(vcpu->kvm, gpa_to_gfn(gpud_base))) + j;
			if (pud_none(*gpud) || !pud_present(*gpud)) continue;
			if (!(pud_val(*gpud) & _PAGE_USER)) continue; // x86

			for (k = 0; k < PTRS_PER_PMD; ++k) {
				gpmd_base = (unsigned long)pud_val(*gpud) & PTE_PFN_MASK;
				gpmd = ((pmd_t*)gfn_to_hva(vcpu->kvm, gpa_to_gfn(gpmd_base))) + k;
				if (pmd_none(*gpmd) || !pmd_present(*gpmd)) continue;
				if (!(pmd_val(*gpmd) & _PAGE_USER)) continue;

				for (l = 0; l < PTRS_PER_PTE; ++l) {
					struct page *page;
					unsigned long gpfn, gvfn, gpa, hva;
					gpte_base = (unsigned long)pmd_val(*gpmd) & PTE_PFN_MASK;
					gpte = ((pte_t*)gfn_to_hva(vcpu->kvm, gpa_to_gfn(gpte_base))) + l;
					if (!gpte || !pte_present(*gpte)) continue;
					if (!(pte_val(*gpte) & _PAGE_USER)) continue; // x86
					if ((pte_flags(*gpte) & _PAGE_GLOBAL)) continue; // x86

					gpfn = pte_pfn(*gpte);
					gvfn = ((i << PGDIR_SHIFT) | (j << PUD_SHIFT) | (k << PMD_SHIFT) | (l << PAGE_SHIFT)) >> PAGE_SHIFT;

					// gpte: guest's page table entry
					// Find the host's page corresponding to the guest pte
					page = NULL;
					hva = 0;

					gpa = pte_val(*gpte) & PTE_PFN_MASK;
					hva = gfn_to_hva(vcpu->kvm, gpa_to_gfn(gpa));

					hpgd = pgd_offset(mmap->vm_mm, hva);
					if (pgd_none(*hpgd) || !pgd_present(*hpgd)) goto find_next_page;
					hpud = pud_offset(hpgd, hva);
					if (pud_none(*hpud) || !pud_present(*hpud)) goto find_next_page;
					hpmd = pmd_offset(hpud, hva);
					if (pmd_none(*hpmd) || !pmd_present(*hpmd)) goto find_next_page;
					hpte = pte_offset_map(hpmd, hva);
					if (pte_none(*hpte) || !pte_present(*hpte)) goto unmap;
					
					page = pte_page(*hpte);

					// Do page migration
					if (color_bits) {
						// Assign colors: use pages in auxrsv
						if (rk_change_page_color(mem, page, &mem->aux_res_attr, color_idx) == RK_SUCCESS) {
							do {
								color_idx++;
								if (color_idx >= mem->aux_res_attr.nr_colors) color_idx = 0;
							} while ((color_bits & (1 << color_idx)) == 0);
						}
					}
					else {
						// Reclaim colors: use pages in memrsv
						if (rk_change_page_color(mem, page, &mem->mem_res_attr, color_idx) == RK_SUCCESS) {
							color_idx++;
							if (color_idx >= mem->mem_res_attr.nr_colors) color_idx = 0; 
						}
					}
unmap:
					pte_unmap(hpte);
find_next_page:
					; // do nothing
				}
			}
		}
	}
	up_read(&mm->mmap_sem);
	return RK_SUCCESS;
}

int rk_mem_reserve_traverse_guest_page_table(struct kvm_vcpu *vcpu)
{
	unsigned long i, j, k, l;
	struct mm_struct *mm;
	struct vm_area_struct *mmap;
	mem_reserve_t mem;
	unsigned long gpgd_base, gpud_base, gpmd_base, gpte_base;

	pgd_t *gpgd, *hpgd;
	pud_t *gpud, *hpud;
	pmd_t *gpmd, *hpmd;
	pte_t *gpte, *hpte;

	gpgd_base = kvm_read_cr3(vcpu);
	printk("guest_page_table: Guest PGD %lx\n", gpgd_base);

	mm = current->active_mm;
	if (mm) mmap = mm->mmap;
	else {
		printk("guest_page_table: WARNING - mmap is NULL (pid %d)\n", current->pid);
		return RK_ERROR;
	}
	if (!current->rk_resource_set || !current->rk_resource_set->mem_reserve) {
		printk("guest_page_table: Error. No memory reservation\n");
		return RK_ERROR;
	}
	mem = current->rk_resource_set->mem_reserve->reserve;

	down_read(&mm->mmap_sem);
	for (i = 0; i < PTRS_PER_PGD; ++i) {
		gpgd = ((pgd_t*)gfn_to_hva(vcpu->kvm, gpa_to_gfn(gpgd_base))) + i;
		if (pgd_none(*gpgd) || !pgd_present(*gpgd)) continue;
		if (!(pgd_val(*gpgd) & _PAGE_USER)) continue; // x86

		for (j = 0; j < PTRS_PER_PUD; ++j) {
			gpud_base = (unsigned long)pgd_val(*gpgd) & PTE_PFN_MASK;
			gpud = ((pud_t*)gfn_to_hva(vcpu->kvm, gpa_to_gfn(gpud_base))) + j;
			if (pud_none(*gpud) || !pud_present(*gpud)) continue;
			if (!(pud_val(*gpud) & _PAGE_USER)) continue; // x86

			for (k = 0; k < PTRS_PER_PMD; ++k) {
				gpmd_base = (unsigned long)pud_val(*gpud) & PTE_PFN_MASK;
				gpmd = ((pmd_t*)gfn_to_hva(vcpu->kvm, gpa_to_gfn(gpmd_base))) + k;
				if (pmd_none(*gpmd) || !pmd_present(*gpmd)) continue;
				if (!(pmd_val(*gpmd) & _PAGE_USER)) continue;

				for (l = 0; l < PTRS_PER_PTE; ++l) {
					struct page *page;
					unsigned long gpfn, gvfn, gpa, hva;
					gpte_base = (unsigned long)pmd_val(*gpmd) & PTE_PFN_MASK;
					gpte = ((pte_t*)gfn_to_hva(vcpu->kvm, gpa_to_gfn(gpte_base))) + l;
					if (!gpte || !pte_present(*gpte)) continue;
					if (!(pte_val(*gpte) & _PAGE_USER)) continue; // x86
					if ((pte_flags(*gpte) & _PAGE_GLOBAL)) continue; // x86

					gpfn = pte_pfn(*gpte);
					gvfn = ((i << PGDIR_SHIFT) | (j << PUD_SHIFT) | (k << PMD_SHIFT) | (l << PAGE_SHIFT)) >> PAGE_SHIFT;

					// gpte: guest's page table entry
					// Find the host's page corresponding to the guest pte
					page = NULL;
					hva = 0;
					//if (!mmap) goto print_output;

					gpa = pte_val(*gpte) & PTE_PFN_MASK;
					hva = gfn_to_hva(vcpu->kvm, gpa_to_gfn(gpa));

					hpgd = pgd_offset(mmap->vm_mm, hva);
					if (pgd_none(*hpgd) || !pgd_present(*hpgd)) goto find_next_page;
					hpud = pud_offset(hpgd, hva);
					if (pud_none(*hpud) || !pud_present(*hpud)) goto find_next_page;
					hpmd = pmd_offset(hpud, hva);
					if (pmd_none(*hpmd) || !pmd_present(*hpmd)) goto find_next_page;
					hpte = pte_offset_map(hpmd, hva);
					if (pte_none(*hpte) || !pte_present(*hpte)) goto unmap;
					
					page = pte_page(*hpte);
					printk("gvfn %lx, gpfn %lx (gpte flag: %lx) hva %lx, hpage %lx) -> COLOR %d\n", gvfn, gpfn, pte_flags(*gpte) & PTE_FLAGS_MASK, hva, (unsigned long)page, MEM_RSV_COLORIDX(page));

unmap:
					pte_unmap(hpte);
find_next_page:
					; // do nothing
				}
			}
		}
	}
	up_read(&mm->mmap_sem);

	return RK_SUCCESS;
}
#else

int rk_mem_reserve_assign_guest_task_colors(struct kvm_vcpu *vcpu, unsigned long key, unsigned long color_bits) { return RK_ERROR; }
int rk_mem_reserve_traverse_guest_page_table(struct kvm_vcpu *vcpu) { return RK_ERROR; }

#endif /* CONFIG_X86 */

#endif /* RK_VIRT_SUPPORT */ 

#endif /* RK_MEM */


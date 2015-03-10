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
#define MEM_RSV_TOTAL_PAGES	(MEM_RSV_TOTAL_SIZE / PAGE_SIZE)

// Swapping parameters
#define MEM_LOW_WATERMARK	0
#define PF_LOOK_BACK_WINDOW	1000
#define MEM_RSV_EVICT_SIZE	128

LIST_HEAD(mem_reserves_head);
int mem_max_capacity; // in pages
int mem_reserve_usage; 
raw_spinlock_t mem_reserve_lock;

struct list_head memrsv_pagebins[MEM_RSV_COLORS][MEM_RSV_BANK_COLORS];
int memrsv_pagebins_counter[MEM_RSV_COLORS][MEM_RSV_BANK_COLORS];

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


void mem_reserves_init(void)
{
	int i, j;

	INIT_LIST_HEAD(&mem_reserves_head);
	mem_max_capacity = MEM_RSV_TOTAL_PAGES; 
	mem_reserve_usage = 0;
	raw_spin_lock_init(&mem_reserve_lock);

	// Preallocating pages from global memory management
	for (i = 0; i < MEM_RSV_COLORS; i++) {
		for (j = 0; j < MEM_RSV_BANK_COLORS; j++) {
			INIT_LIST_HEAD(&memrsv_pagebins[i][j]);
			memrsv_pagebins_counter[i][j] = 0;		
		}
	}
	for (i = 0; i < mem_max_capacity; i++) {
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
	for (i = 0; i < MEM_RSV_COLORS; i++) {
		for (j = 0; j < MEM_RSV_BANK_COLORS; j++) {
			list_for_each_entry_safe(page, tmp, &memrsv_pagebins[i][j], lru) {
				ClearPageMemReserve(page);
				ClearPageEvictionLock(page);
				page->rsv = NULL;

				__free_page(page);
			}
		}
	}
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
	unsigned long flags;
	raw_spin_lock_irqsave(&mem_reserve_lock, flags);
	__free_page_to_pagebins(page);
	raw_spin_unlock_irqrestore(&mem_reserve_lock, flags);
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
	unsigned long flags;
	raw_spin_lock_irqsave(&mem_reserve_lock, flags);
	for (i = 0; i < attr->nr_colors; i++) {
		for (j = 0; j < attr->nr_bank_colors; j++) {
			total += memrsv_pagebins_counter[attr->colors[i]][attr->bank_colors[j]];
		}
		if (total > nr_pages) {
			raw_spin_unlock_irqrestore(&mem_reserve_lock, flags);
			return TRUE;
		}
	}
	raw_spin_unlock_irqrestore(&mem_reserve_lock, flags);
	return FALSE;
}

struct page* alloc_page_from_pagebins(mem_reserve_t mem)
{
	mem_reserve_attr_t attr;
	struct page *page = NULL;
	int i;
	unsigned long flags;

	if (!mem) return NULL;
	attr = &mem->mem_res_attr;

	raw_spin_lock_irqsave(&mem_reserve_lock, flags);
	for (i = 0; i < attr->nr_colors * 2; i++) {
		page = __get_page_from_pagebins(attr->colors[attr->next_color], attr->bank_colors[attr->next_bank_color]);
		if (++(attr->next_color) >= attr->nr_colors) {
			attr->next_color = 0;
			if (++(attr->next_bank_color) >= attr->nr_bank_colors) attr->next_bank_color = 0;
		}
		if (page) break;
	}
	raw_spin_unlock_irqrestore(&mem_reserve_lock, flags);
	if (!page) {
		page = alloc_page(GFP_HIGHUSER_MOVABLE);
		printk(KERN_ALERT "*** No color ***\n");
	}
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

static inline int page_category(struct mem_reserve_page *entry)
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
static inline void move_to_mem_used_list(struct mem_reserve_page *entry, mem_reserve_t mem) 
{
	list_move_tail(&entry->list, &mem->mem_active_list);
	mem->mem_free_size--;
	mem->mem_used_size++;
	mem->mem_active_size++;
	if (mem->mem_used_size > mem->mem_peak_size) mem->mem_peak_size = mem->mem_used_size;

	entry->active_used = 1;
	entry->access_count = 1;
}
static inline void move_to_mem_free_list(struct mem_reserve_page *entry, mem_reserve_t mem) 
{
	list_move_tail(&entry->list, &mem->mem_free_list);
	mem->mem_free_size++;
	mem->mem_used_size--;

	if (entry->active_used) mem->mem_active_size--;
	else mem->mem_inactive_size--;

	entry->active_used = 0;
	entry->executable = false;
	entry->access_count = 0;
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
void rk_remove_page_rmap(struct page *page, mem_reserve_t mem)
{
#ifndef RSV_NO_SHARED_PAGE_CONSERVATION
	struct mem_reserve_page *entry;
	struct page *tmp_page;
	unsigned long flags;

	entry = get_task_page_ownership(mem, page->rsv);
	if (!mem || !entry) return;

	if (PageEvictionLock(page)) return; 

	raw_spin_lock_irqsave(&mem_reserve_lock, flags);
	if (entry->access_count > 0) entry->access_count--;

	// Page is not shared with other reserves
	// (Unmapped private page will be freed by rk_free_pages)
	if (list_empty(&entry->shared)) goto unlock;

	// Page is shared with other reserves.
	// If access_count > 0, then we need to retain page entry info.
	if (entry->access_count > 0) goto unlock;

	// Page is allocated from current mem reserve
	if (page->rsv == entry) {
		mem_reserve_t shr;

		mem_dbg("remove_rmap: shared page(owner) entry:%lx - page:%lx\n", (unsigned long)entry, (unsigned long)page);

		// Setup page link to another mem_reserve_page entry
		page->rsv = list_entry(entry->shared.next, struct mem_reserve_page, shared);
		shr = ((struct mem_reserve_page*)page->rsv)->mem;
		VM_BUG_ON(shr == NULL);

		// Remove mem_reserve_page entry from shared list
		list_del(&entry->shared);
		INIT_LIST_HEAD(&entry->shared);

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
		list_del(&entry->shared);
		INIT_LIST_HEAD(&entry->shared);

		// Need to move one conserved page to mem_free_list
		MEM_LOCK(&mem->mem_list_lock);

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
	raw_spin_unlock_irqrestore(&mem_reserve_lock, flags);
#endif
}

// Should be called with mem_reserve_lock held
static struct mem_reserve_page* rk_attach_single_page(mem_reserve_t mem, struct page *page, bool shared_reserved_page)
{
	struct page *tmp;
	struct mem_reserve_page *entry;
	unsigned long flags;

	// Detach one free mem_reserve_entry from mem_free_list
	// and attach mem_reserve_entry to mem_used_list
	
	raw_spin_lock_irqsave(&mem_reserve_lock, flags);
	MEM_LOCK(&mem->mem_list_lock);
	entry = list_first_entry(&mem->mem_free_list, struct mem_reserve_page, list);
	tmp = entry->page;

	if (shared_reserved_page == false) {
		int ret = -1;
		// Migration for private page
		//if (page_mapcount(page) <= 1 && !isolate_lru_page(page)) {
		if (!isolate_lru_page(page)) {
			LIST_HEAD(page_list);
			LIST_HEAD(entry_list);

			list_add(&page->lru, &page_list);
			SetPageMemReserve(tmp);
			// Remove entry from mem_free_list before unlocking spinlock
			list_move(&entry->list, &entry_list); 
			mem->mem_free_size--;
			raw_spin_unlock_irqrestore(&mem_reserve_lock, flags);

			if ((ret = rk_migrate_page(page, tmp)) == 0) {
				// If rk_migrate_page() succeeds, it will call free_hot_cold_page(page).
				tmp = NULL;

				// This 'entry' will be moved to mem_used_list. 
				// Here we temporarily put it into mem_free_list due to consistency.
				raw_spin_lock_irqsave(&mem_reserve_lock, flags);
				list_move(&entry->list, &mem->mem_free_list);
				mem->mem_free_size++;
			}
			else {
				// If rk_migrate_page() fails, it will call rk_free_pages(tmp) that calls 
				// move_to_mem_free_list(entry). 
				// But 'entry' is currently not in mem_used_list, we need to fix size here.
				raw_spin_lock_irqsave(&mem_reserve_lock, flags);
				mem->mem_used_size++;
				mem->mem_inactive_size++;
				//printk("rk_attach_single_page: rk_migrate_page failed. p:%lx, f:%lx\n", (unsigned long)page, page->flags);
			}
			mem_dbg("rk_migrate_page : from:%lx, to:%lx, err:%d, free:%d, used:%d\n", 
				(unsigned long)page, (unsigned long)entry->page, ret, mem->mem_free_size, mem->mem_used_size);
			//printk("rk_attach_single_page: p:%lx, f:%lx\n", (unsigned long)page, page->flags);
		}
		else {
			//printk("rk_attach_single_page: cannot isolate from lru. p:%lx, f:%lx\n", (unsigned long)page, page->flags);
		}
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
		if (!PageEvictionLock(page)) {
			// Move the free page of mem_reserve_entry to conserved list
			list_add_tail(&tmp->lru, &mem->mem_conserved_list);
			mem->mem_conserved_size++;
			tmp = NULL;
		}
#endif
	}
	// Move entry to mem_used_list
	move_to_mem_used_list(entry, mem);
	MEM_UNLOCK(&mem->mem_list_lock);
	raw_spin_unlock_irqrestore(&mem_reserve_lock, flags);

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
void rk_add_page_rmap(struct page *page, bool is_anon)
{
	struct mem_reserve_page *entry;
	mem_reserve_t mem = current->rk_resource_set->mem_reserve->reserve;
	bool shared_reserved_page = false;
	unsigned long flags;

	mem_dbg("add_rmap: %s, %lx(%x), mc:%d\n", is_anon ? "anon" : "file", (unsigned long)page, (unsigned int)page->flags, page_mapcount(page));

	if (PageReserved(page)) return;

	raw_spin_lock_irqsave(&mem_reserve_lock, flags);
	if (PageMemReserve(page)) {
		// check page ownership
		entry = get_task_page_ownership(mem, page->rsv);
		if (entry) {
			mem_dbg("add_rmap: page %lx already owned by %d. ac:%d\n", 
				(unsigned long)page, current->pid, entry->access_count + 1);
			// increase access count
			entry->access_count++;
			raw_spin_unlock_irqrestore(&mem_reserve_lock, flags);
			return;
		}
		// need to add ownership to this shared page
		shared_reserved_page = true;
	}
	raw_spin_unlock_irqrestore(&mem_reserve_lock, flags);
	
	//printk("add_rmap: f:%d\n", mem->mem_free_size);
	if (mem->mem_free_size <= 0) {
		// This will not be happened because we confirm enough freed pages in handle_mm_fault()
		printk("WARNING: not enough RK pages - add_rmap (pid: %d)\n", current->pid);
		//dump_stack();
		return;
	}

	rk_attach_single_page(mem, page, shared_reserved_page);
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
	unsigned long flags;
	
	if (mem->mem_used_size == 0) {
		printk("evict_reserved_pages: no pages to evict!!\n");
		return -1;
	}

	// Refill inactive list
	raw_spin_lock_irqsave(&mem_reserve_lock, flags);
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

			nr_referenced = page_referenced(page, false, NULL, &vm_flags);
			nr_referenced += TestClearPageReferenced(page) != 0;
			//printk("act - page:%lx, f:%lx, ref:%d\n", (unsigned long)page, (unsigned long)page->flags, nr_referenced);
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
	raw_spin_unlock_irqrestore(&mem_reserve_lock, flags);

	// Scan inactive list
	raw_spin_lock_irqsave(&mem_reserve_lock, flags);
	MEM_LOCK(&mem->mem_list_lock);
	n_size = mem->mem_inactive_size;
	n_list_now = 0;
	for (i = 0; i < n_size; i++) {
		entry = list_first_entry(&mem->mem_inactive_list,
			struct mem_reserve_page, list);
		page = entry->page;

		if (!page) goto move_to_active;
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
		nr_referenced = page_referenced(page, false, NULL, &vm_flags);
		nr_referenced += TestClearPageReferenced(page) != 0;
		//printk("inact - page:%lx, f:%lx, ref:%d\n", (unsigned long)page, (unsigned long)page->flags, nr_referenced);
		if (nr_referenced > 0) goto move_to_active;

#ifndef RSV_NO_SHARED_PAGE_CONSERVATION
		// Shared Page Conservation
		// - Check if this page is shared
		if (!list_empty(&entry->shared)) {
			if (page->mapping == NULL) goto move_to_active;
			if (!trylock_page(page)) goto move_to_active;

			MEM_UNLOCK(&mem->mem_list_lock);
			raw_spin_unlock_irqrestore(&mem_reserve_lock, flags);
			if (SWAP_SUCCESS == try_to_unmap(page, TTU_UNMAP | TTU_RK_UNMAP | TTU_IGNORE_ACCESS)) {
				mem_dbg("evict page: UNMAP!! (page:%lx)\n", (unsigned long)page);
				n_evicted++;
			}
			else {
				mem_dbg("evict page: UNMAP failed!! (page:%lx)\n", (unsigned long)page);
			}
			unlock_page(page);

			raw_spin_lock_irqsave(&mem_reserve_lock, flags);
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
			raw_spin_unlock_irqrestore(&mem_reserve_lock, flags);

			ret = rk_page_list_out(last_zone, &page_list, n_list_now);
			//printk("req: %d, evicted:%d\n", n_list_now, ret);
			n_evicted += ret;
			if (ret != n_list_now) {
				mem_dbg("evict_page: UNFREED %d pages\n", n_list_now - ret);
			}
			n_list_now = 0;
			last_zone = NULL;

			raw_spin_lock_irqsave(&mem_reserve_lock, flags);
			MEM_LOCK(&mem->mem_list_lock);
		}
		last_zone = zone;
		mem_dbg("evict page:%lx, f:%x, c:%d, mc:%d, rsv:%d, %s\n", 
			(unsigned long)page, 
			(unsigned int)page->flags, 
			page_count(page), 
			page_mapcount(page), 
			PageMemReserve(page), 
			page->mapping == NULL ? "cache" 
				: (((unsigned long)page->mapping & 0x1) ? "mem" 
				: "file"));

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
	raw_spin_unlock_irqrestore(&mem_reserve_lock, flags);

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
	unsigned long flags;
	
	mm = p->active_mm;
	down_read(&mm->mmap_sem);
	mmap = mm->mmap;

	while(mmap) {
		unsigned long n = mmap->vm_start;
		if (mmap->vm_flags & (VM_IO | VM_PFNMAP)) goto next_vma;
			
		//bool executable = (mmap->vm_flags & VM_EXEC) != 0;
		while(n < mmap->vm_end) {
			bool shared_reserved_page = false;

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
#ifdef RSV_NO_PAGE_CACHE
			if (PageMappedToDisk(page) || !PageAnon(page)) goto unmap;
#endif
#ifdef RSV_NO_SHARED_MEM
			if (page_mapcount(page) > 1) goto unmap;
#endif
			n_pages++;

			raw_spin_lock_irqsave(&mem_reserve_lock, flags);
			if (PageMemReserve(page)) {
				// check page ownership
				entry = get_task_page_ownership(mem, page->rsv);
				if (entry) {
					if (!second_try) {
						mem_dbg("attach: page %lx already owned by %d. ac:%d\n", 
							(unsigned long)page, p->pid, entry->access_count + 1);
						// increase access count
						entry->access_count++;
						// account reserved page as attached
						n_attached_pages++;
					}
					raw_spin_unlock_irqrestore(&mem_reserve_lock, flags);
					goto unmap;
				}
				// need to add ownership to this shared page
				shared_reserved_page = true;
			}
			raw_spin_unlock_irqrestore(&mem_reserve_lock, flags);

			// check if free pages are not enough
			if ((!second_try && mem->mem_free_size < MEM_RSV_EVICT_SIZE / 2)
				|| (second_try && mem->mem_free_size <= 0)) {
				pte_unmap(pte);
				break;
			}	
			
			entry = rk_attach_single_page(mem, page, shared_reserved_page);

			n_attached_pages++;
			page = entry->page;
			mem_dbg("attach page:%lx, f:%x, c:%d, mc:%d, rsv:%d, %s -> entry %d\n", 
				(unsigned long)page, 
				(unsigned int)page->flags, 
				page_count(page), 
				page_mapcount(page), 
				PageMemReserve(page), 
				page->mapping == NULL ? "cache" 
					: (((unsigned long)page->mapping & 0x1) 
					? "mem" : "file"),
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

// Swap-out all unreserved pages of task p. Called by mem_reserve_attach_process()
int evict_unreserved_pages(struct task_struct *p)
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
					page->mapping == NULL ? "cache" 
						: (((unsigned long)page->mapping & 0x1) ? "mem" 
						: "file"));
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
				page->mapping == NULL ? "cache" 
					: (((unsigned long)page->mapping & 0x1) ? "mem" 
					: "file"));

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

	mem_dbg("evict_unreserved_pages: pageout:%d\n", n_evicted);
	return n_evicted;	
}

////////////////////////////////////////////////////////////////////////

// Copy of mm/mlock.c::stack_guard_page()
/*
static inline int stack_guard_page(struct vm_area_struct *vma, unsigned long addr)
{
	return (vma->vm_flags & VM_GROWSDOWN) &&
		(vma->vm_start == addr) &&
		!vma_stack_continue(vma->vm_prev, addr);
}
*/

// Copy of include/linux/hugetlb.h::is_vm_hugetlb_page()
/*
static inline int is_vm_hugetlb_page(struct vm_area_struct *vma)
{
	return vma->vm_flags & VM_HUGETLB;
}
*/

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
		|| is_vm_hugetlb_page(vma) || vma == get_gate_vma(current->mm))
		goto out;

	pgoff = vma->vm_pgoff + ((start - vma->vm_start) >> PAGE_SHIFT);
	*prev = vma_merge(mm, *prev, start, end, vma->vm_flags, vma->anon_vma,
		vma->vm_file, pgoff, vma->vm_policy);
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
////////////////////////////////////////////////////////////////////////

// Performs admission test for a task
int do_task_admission_test(mem_reserve_t mem, struct task_struct *p)
{
	return mem->mem_reserve_size;
}

void mem_reserve_attach_process(mem_reserve_t mem, struct task_struct *p)
{
	// Note: the task to be attached should have been suspended by the caller
	int n_remaining;

	if (mem == NULL || p == NULL) return;

	//printk("mem_reserve_attach_process: pid %d\n", p->pid);
	//printk("======== BEFORE ATTACHING (AFTER MAKING PRESENT) =========\n");
	//sys_rk_mem_reserve_show_task_vminfo(p->pid);

	n_remaining = attach_pages_to_mem_reserve(mem, p, false);
	//printk("======== AFTER ATTACHING  =========\n");
	//sys_rk_mem_reserve_show_task_vminfo(p->pid);
	if (n_remaining && mem->mem_res_attr.reserve_mode == RSV_HARD) {
		evict_unreserved_pages(p);
		attach_pages_to_mem_reserve(mem, p, true);
	}
	//printk("======== AFTER EVICTING UNRESERVED PAGES =========\n");
	//sys_rk_mem_reserve_show_task_vminfo(p->pid);
}

void mem_reserve_detach_process_in_kernel_mode(mem_reserve_t mem, struct task_struct *p, bool called_by_exit)
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
	int mig_used_size, mig_active_size, mig_inactive_size;
	unsigned long flags;

	cpu_tick_data_t t1, t2;
	rk_rdtsc(&t1);

	if (mem == NULL || p == NULL) return;

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
	
			if (called_by_exit) {
				//if (page_mapcount(page) <= 1) goto unmap;
			}

			raw_spin_lock_irqsave(&mem_reserve_lock, flags);
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
				raw_spin_unlock_irqrestore(&mem_reserve_lock, flags);
				goto unmap;
			}
			// Move this entry from used_list to to page_list
			list_move_tail(&entry->list, &page_list);
			mem->mem_used_size--;
			if (entry->active_used) mem->mem_active_size--;
			else mem->mem_inactive_size--;

			mem_dbg("detach: entry:%lx, page:%lx\n", (unsigned long)entry, (unsigned long)page);
			// Page is not shared with other reserves
			if (list_empty(&entry->shared)) {
				/*
				if (page_mapcount(page) <= 1) {
					// Do nothing here. migrate this page later
				}
				else {
					page->rsv = NULL;
					clear_reserve_hot_page(page);
					entry->page = NULL;
				}*/
				MEM_UNLOCK(&mem->mem_list_lock);
				raw_spin_unlock_irqrestore(&mem_reserve_lock, flags);
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
				raw_spin_unlock_irqrestore(&mem_reserve_lock, flags);
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

	mig_used_size = mig_active_size = mig_inactive_size = 0;
	list_for_each_entry_safe(entry, safe, &page_list, list) {
		// realloc page for mem_free_list
		struct page *newpage = NULL;

		page = entry->page;
		if (page == NULL) {
			newpage = alloc_page_from_pagebins(mem);
			SetPageMemReserve(newpage); // no need to page_lock()
			entry->page = newpage;
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
					// If rk_migrate_page() succeeds, it will call rk_free_pages(page) 
					// which calls move_to_mem_free_list().
					// But since this page was not in the used list, we should recover the counters here.
					mig_used_size++;
					if (is_active_used) mig_active_size++;
					else mig_inactive_size++;

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
				clear_reserve_hot_page(page);
				// reallocate newpage (rk_migrate_page frees newpage when it fails)
				// - here, we use alloc_page_from_pagebins, because newpage will be kept in mem_reserve.
				newpage = alloc_page_from_pagebins(mem); 
				SetPageMemReserve(newpage); // no need to page_lock()
				entry->page = newpage;

				// fill the pagebin-pool
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

	raw_spin_lock_irqsave(&mem_reserve_lock, flags);
	MEM_LOCK(&mem->mem_list_lock);

	list_splice_tail(&page_list, &mem->mem_free_list);
	mem->mem_free_size += n_free;
	mem->mem_used_size += mig_used_size;
	mem->mem_active_size += mig_active_size;
	mem->mem_inactive_size += mig_inactive_size;

	MEM_UNLOCK(&mem->mem_list_lock);
	raw_spin_unlock_irqrestore(&mem_reserve_lock, flags);
	
	rk_rdtsc(&t2);
	printk("detach_task: free_list:%d, used_list:%d, rsv_size:%d (maj_flt:%lu, min_flt:%lu), time:%lumsec\n", 
		mem->mem_free_size, mem->mem_used_size, mem->mem_reserve_size, p->maj_flt, p->min_flt, (unsigned long)(t2 - t1) / 1000000);
}

void mem_reserve_detach_process(mem_reserve_t mem, struct task_struct *p)
{
	mem_reserve_detach_process_in_kernel_mode(mem, p, false);
}

struct page* rk_alloc_pages(gfp_t gfp_mask, unsigned int order, bool* ret)
{
	struct mem_reserve_page* entry;
	mem_reserve_t mem;
	unsigned long flags;

	if (!(((gfp_mask & GFP_HIGHUSER_MOVABLE) == GFP_HIGHUSER_MOVABLE)
		&& current->rk_resource_set && current->rk_resource_set->mem_reserve)) {
		*ret = false; // let the kernel allocate a page
		return NULL;
	}
	if (order > 0) {
		printk("alloc_pages_hook: does not support > 1 (gfp:%x)\n", gfp_mask);
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

	raw_spin_lock_irqsave(&mem_reserve_lock, flags);
	MEM_LOCK(&mem->mem_list_lock);
	entry = list_first_entry(&mem->mem_free_list, struct mem_reserve_page, list);
 	move_to_mem_used_list(entry, mem); 
	// Reset access_count to 0 because it will be increase by rmap
	entry->access_count = 0; 
	MEM_UNLOCK(&mem->mem_list_lock);
	raw_spin_unlock_irqrestore(&mem_reserve_lock, flags);

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
	unsigned long flags;

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
	// FIXME: EvictionLock flag has been cleared before calling this function. 
	// Need to add an input parameter to check if eviction lock is used or not.
	if (PageEvictionLock(page)) { 
		need_realloc = true;
		ClearPageEvictionLock(page);
		mem_dbg("rk_free_pages: EVICTION LOCKED - page %lx\n", (unsigned long)page);
	}
#ifdef RSV_NO_SHARED_PAGE_CONSERVATION 
	need_realloc = true;
#endif
	SetPageMemReserve(page);
	atomic_set(&page->_count, 1);

	raw_spin_lock_irqsave(&mem_reserve_lock, flags);
	head = shared_list = &entry->shared;
	do {
		mem = entry->mem;
		MEM_LOCK(&mem->mem_list_lock);
		move_to_mem_free_list(entry, mem);
		if (entry != page->rsv) {
			if (need_realloc == false) {
				// The page does not belong to entry->mem.
				// This entry has only a link to the page.
				VM_BUG_ON(mem->mem_conserved_size <= 0);

				mem->mem_conserved_size--;
				entry->page = list_first_entry(&mem->mem_conserved_list, struct page, lru);
				entry->page->rsv = entry;
				list_del(&entry->page->lru);
				mem_dbg("    - recover page: e:%lx, p:%lx\n", (unsigned long)entry, (unsigned long)entry->page);
			} 
			else {
				list_move_tail(&entry->list, &entry_list);
				mem->mem_free_size--;
			}
		}
		MEM_UNLOCK(&mem->mem_list_lock);

		shared_list = entry->shared.next;
		INIT_LIST_HEAD(&entry->shared);
		entry = list_entry(shared_list, struct mem_reserve_page, shared);
	} while (shared_list != head);
	raw_spin_unlock_irqrestore(&mem_reserve_lock, flags);

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
			entry->page = alloc_page_from_pagebins(mem);
			mem_dbg("rk_free_pages: alloc for EVICTION LOCK entry:%lx - page:%lx\n", (unsigned long)entry, (unsigned long)entry->page);
			if (entry->page) {
				entry->page->rsv = entry;
				SetPageMemReserve(entry->page);
			}
			else {
				mem_dbg(" -> NULL\n");
			}
			
			// move to free list (member variables are already initialized)
			raw_spin_lock_irqsave(&mem_reserve_lock, flags);
			list_move_tail(&entry->list, &mem->mem_free_list);
			mem->mem_free_size++;
			raw_spin_unlock_irqrestore(&mem_reserve_lock, flags);
		}
		// restore temporarily disabled mem reserve
		if (cur_mem) current->rk_resource_set->mem_reserve = cur_mem->rsv;
	}
	//atomic_inc(&page->_count);
	return 0;
}

asmlinkage int sys_rk_mem_reserve_create(int rd, mem_reserve_attr_t usr_mem_attr)
{
	rk_reserve_t  		rsv;
	rk_resource_set_t	rset;
	mem_reserve_t 		mem;
	mem_reserve_attr_data_t mem_attr_data;
	int req_pages;
	int i, j;

	rk_sem_down();
	rset = resource_set_descriptor[rd];

	/* Input Checks */
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
	if (mem_attr_data.mem_size == 0) {// || mem_attr_data.mem_size > mem_max_capacity) {
		printk("sys_rk_mem_reserve_create: Invalid memory reservation size\n");
		goto unlock_error;
	}
	if (mem_attr_data.reserve_mode != RSV_HARD && mem_attr_data.reserve_mode != RSV_FIRM) {
		printk("sys_rk_mem_reserve_create: Mem reserve mode should be RSV_HARD or RSV_FIRM\n");
		goto unlock_error;
	}
	req_pages = (mem_attr_data.mem_size + PAGE_SIZE - 1) / PAGE_SIZE;
	if (req_pages < MEM_RSV_EVICT_SIZE * 2) {
		printk("sys_rk_mem_reserve_create: Requested size is too small. (Min = %lu)\n", MEM_RSV_EVICT_SIZE * 2 * PAGE_SIZE);
		goto unlock_error;
	}
	if (mem_max_capacity - mem_reserve_usage < req_pages) {
		printk("sys_rk_mem_reserve_create: admission test for mem reserve failed (max pages:%d, current usage:%d, req:%d)\n",
			mem_max_capacity, mem_reserve_usage, req_pages);
		goto unlock_error;
	}
	if (mem_attr_data.nr_colors <= 0 || mem_attr_data.nr_colors > MEM_RSV_COLORS) {
		printk("sys_rk_mem_reserve_create: invalid nr_colors(%d), range: 1-%d\n",
			mem_attr_data.nr_colors, MEM_RSV_COLORS);
		goto unlock_error;
	}
	for (i = 0; i < mem_attr_data.nr_colors; i++) {
		if (mem_attr_data.colors[i] < MEM_RSV_COLORS) continue;

		printk("sys_rk_mem_reserve_create: invalid color value(idx:%d, value:%d)\n",
			i, mem_attr_data.colors[i]);
		goto unlock_error;
	}
	if (mem_attr_data.nr_bank_colors <= 0 || mem_attr_data.nr_bank_colors > MEM_RSV_BANK_COLORS) {
		mem_attr_data.nr_bank_colors = MEM_RSV_BANK_COLORS;
		for (i = 0; i < MEM_RSV_BANK_COLORS; i++) mem_attr_data.bank_colors[i] = i;
	}
	for (i = 0; i < mem_attr_data.nr_bank_colors; i++) {
		if (mem_attr_data.bank_colors[i] < MEM_RSV_BANK_COLORS) continue;

		printk("sys_rk_mem_reserve_create: invalid bank color value(idx:%d, value:%d)\n",
			i, mem_attr_data.bank_colors[i]);
		goto unlock_error;
	}
	if (!is_nr_pages_in_pagebins(&mem_attr_data, req_pages)) {
		printk("sys_rk_mem_reserve_create: not enough pages in pagebins\n");
		goto unlock_error;
	}
	mem_attr_data.next_color = 0;
	mem_attr_data.next_bank_color = 0;

    	/* create mem reserve object */
	mem = kmalloc(sizeof(struct mem_reserve), GFP_ATOMIC);
        memset(mem, 0, sizeof(struct mem_reserve));

	mem->mem_res_attr = mem_attr_data;
	mem->mem_reserve_size = req_pages; 
	mem->mem_effective_size = mem->mem_reserve_size;
	mem->reserved_pages = vmalloc(sizeof(struct mem_reserve_page) * mem->mem_reserve_size);
	if (!mem->reserved_pages) {
		printk("sys_rk_mem_reserve_create: Failed to create mem reserve pool\n");
		kfree(mem);
		goto unlock_error;
	}

	INIT_LIST_HEAD(&mem->mem_link);
	INIT_LIST_HEAD(&mem->mem_free_list);
	//INIT_LIST_HEAD(&mem->mem_used_list);
	INIT_LIST_HEAD(&mem->mem_active_list);
	INIT_LIST_HEAD(&mem->mem_inactive_list);
	INIT_LIST_HEAD(&mem->mem_conserved_list);

	raw_spin_lock_init(&mem->mem_list_lock);
	
	printk("COLOR(NR:%d) : ", mem->mem_res_attr.nr_colors);
	for (i = 0; i < mem->mem_res_attr.nr_colors; i++) {
		printk("%d ", mem->mem_res_attr.colors[i]);
	}
	printk("\n");

	printk("BANK COLOR(NR:%d) : ", mem->mem_res_attr.nr_bank_colors);
	for (i = 0; i < mem->mem_res_attr.nr_bank_colors; i++) {
		printk("%d ", mem->mem_res_attr.bank_colors[i]);
	}
	printk("\n");

	// allocate a page frame and add it to free list
	for (i = 0; i < mem->mem_reserve_size; i++) {
		mem->reserved_pages[i].page = alloc_page_from_pagebins(mem);
		if (!mem->reserved_pages[i].page) {
			// FAIL: dealloc and return
			printk("sys_rk_mem_reserve_create: Failed to allocate page for mem reserve pool\n");

			for (j = 0; j < i; j++) {
				ClearPageMemReserve(mem->reserved_pages[i].page);
				free_page_to_pagebins(mem->reserved_pages[i].page);
			}
			vfree(mem->reserved_pages);
			kfree(mem);
			goto unlock_error;
		}
		SetPageMemReserve(mem->reserved_pages[i].page); // no need to page_lock()
		// PageMemReserve : test
		// ClearPageMemReserve : clear
		mem->mem_free_size++;
		list_add_tail(&mem->reserved_pages[i].list, &mem->mem_free_list);
		mem->reserved_pages[i].mem = mem;
		mem->reserved_pages[i].active_used = 0;
		mem->reserved_pages[i].executable = false;
		mem->reserved_pages[i].access_count = 0;
		mem->reserved_pages[i].page->rsv = &mem->reserved_pages[i];
		INIT_LIST_HEAD(&mem->reserved_pages[i].shared);
	}

    	/* create generic reserve object */
    	rsv = rk_reserve_create(rset, RSV_MEM);
    	rsv->reserve = mem;
    	rsv->operations = &mem_reserve_ops;
    	mem->rsv = rsv;

	rset->mem_reserve = rsv;
	list_add_tail(&mem->mem_link, &mem_reserves_head);

	mem_reserve_usage += mem->mem_reserve_size;

	rk_procfs_reserve_create(rsv, 0);
	rk_sem_up();

	printk("sys_rk_mem_reserve_create: %dpages (max:%d, current reserves usage:%d)\n", 
		mem->mem_reserve_size, mem_max_capacity, mem_reserve_usage);
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
	int i;

	if (mem == NULL) {
		printk("rk_mem_reserve_delete: Deleting a NULL reserve\n");
		return;
	}
	printk("rk_mem_reserve_delete: (now) free_list:%d, used_list:%d, rsv_size:%d \n", 
		mem->mem_free_size, mem->mem_used_size, mem->mem_reserve_size);

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
				page->mapping == NULL ? "cache" 
					: (((unsigned long)page->mapping & 0x1) 
					? "mem" : "file"),
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
	
	list_for_each_entry(entry, &mem->mem_free_list, list) {
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

	vfree(mem->reserved_pages);

	mem_reserve_usage -= mem->mem_reserve_size;
	printk("rk_mem_reserve_delete: %dpages, peak usage:%d (max:%d, current reserves usage:%d)\n", 
		mem->mem_reserve_size, mem->mem_peak_size, mem_max_capacity, mem_reserve_usage);
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
	unsigned long flags;

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
	if (mem->mem_reserve_size < n_req_pages) {
		printk("sys_rk_mem_reserve_eviction_lock: requested block(start:%lu, size:%lu) is larger \
			than the memory reservation(pages: %d)\n", vaddr, (unsigned long)size, mem->mem_reserve_size);
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

	// Attach loaded & unreserved pages
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
			raw_spin_lock_irqsave(&mem_reserve_lock, flags);
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
				raw_spin_unlock_irqrestore(&mem_reserve_lock, flags);
				
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
				raw_spin_unlock_irqrestore(&mem_reserve_lock, flags);
				mem_dbg("eviction lock: LOCK - vaddr %lx - page %lx\n", 
					n, (unsigned long)page);
			}
			n_locked_pages++;
			goto unmap;
unlock:
			raw_spin_unlock_irqrestore(&mem_reserve_lock, flags);
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
	mem_reserve_t mem;
	mem_reserve_attr_data_t attr;

	rk_sem_down();
	
	if (rsv == NULL || rsv->reserve == NULL) {
		rk_sem_up();
		return 0;
	}
	mem = rsv->reserve;
	attr = mem->mem_res_attr;

	rk_sem_up();

	p += sprintf(p, "mem_size     : %llu\n", attr.mem_size);
	p += sprintf(p, "rsv_mode     : %d\n", attr.reserve_mode);

	p += sprintf(p, "nr_colors    : %d {", attr.nr_colors);
	for (i = 0; i < attr.nr_colors; i++) {
		p += sprintf(p, " %d", attr.colors[i]);
	}
	p += sprintf(p, " }\n");

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

#endif /* RK_MEM */

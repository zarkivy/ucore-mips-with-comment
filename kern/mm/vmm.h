// 定义了虚拟内存管理的相关数据结构与宏
#ifndef __KERN_MM_VMM_H__
#define __KERN_MM_VMM_H__

#include <defs.h>
#include <list.h>
#include <memlayout.h>
#include <atomic.h>
#include <sync.h>
#include <proc.h>
#include <sem.h>

// pre define
struct mm_struct;

// the virtual continuous memory area(vma)
// 管理虚拟内存区域的数据结构
// 一个 vma_struct 维护一段虚拟内存的信息：其开始与结束地址、rwx 权限
struct vma_struct {
    struct mm_struct *vm_mm; // the set of vma using the same PDT 指向管理自己的 mm_struct
    uintptr_t vm_start;      //	start addr of vma	
    uintptr_t vm_end;        // end addr of vma
    uint32_t vm_flags;       // flags of vma
    list_entry_t list_link;  // linear list link which sorted by start addr of vma
};

#define le2vma(le, member)                  \
    to_struct((le), struct vma_struct, member)

// vma 的 rwx 权限
#define VM_READ                 0x00000001
#define VM_WRITE                0x00000002
#define VM_EXEC                 0x00000004
#define VM_STACK                0x00000008


// the control struct for a set of vma using the same PDT
// 一个进程的 4GB 虚拟内存空间使用一个 mm_struct 管理
// mm_struct 管理该进程的所有 vma_struct
struct mm_struct {
    list_entry_t mmap_list;        // linear list link which sorted by start addr of vma
    struct vma_struct *mmap_cache; // current accessed vma, used for speed purpose
    pde_t *pgdir;                  // the PDT of these vma 该进程的页表地址
    int map_count;                 // the count of these vma
	void *sm_priv;				   // the private data for swap manager
	atomic_t mm_count;
	semaphore_t mm_sem;
	int locked_by;

};

struct vma_struct *find_vma(struct mm_struct *mm, uintptr_t addr);
struct vma_struct *vma_create(uintptr_t vm_start, uintptr_t vm_end, uint32_t vm_flags);
void insert_vma_struct(struct mm_struct *mm, struct vma_struct *vma);

struct mm_struct *mm_create(void);
void mm_destroy(struct mm_struct *mm);

void vmm_init(void);

int do_pgfault(struct mm_struct *mm, uint32_t error_code, uintptr_t addr);

extern volatile unsigned int pgfault_num;
extern struct mm_struct *check_mm_struct;

bool user_mem_check(struct mm_struct *mm, uintptr_t start, size_t len, bool write);
bool copy_from_user(struct mm_struct *mm, void *dst, const void *src, size_t len, bool writable);
bool copy_to_user(struct mm_struct *mm, void *dst, const void *src, size_t len);

int mm_unmap(struct mm_struct *mm, uintptr_t addr, size_t len);
int dup_mmap(struct mm_struct *to, struct mm_struct *from);
void exit_mmap(struct mm_struct *mm);
uintptr_t get_unmapped_area(struct mm_struct *mm, size_t len);
int mm_brk(struct mm_struct *mm, uintptr_t addr, size_t len);

static inline int
mm_count(struct mm_struct *mm) {
    return atomic_read(&(mm->mm_count));
}

static inline void
set_mm_count(struct mm_struct *mm, int val) {
    atomic_set(&(mm->mm_count), val);
}

static inline int
mm_count_inc(struct mm_struct *mm) {
    return atomic_add_return(&(mm->mm_count), 1);
}

static inline int
mm_count_dec(struct mm_struct *mm) {
    return atomic_sub_return(&(mm->mm_count), 1);
}

static inline void
lock_mm(struct mm_struct *mm) {
    if (mm != NULL) {
        down(&(mm->mm_sem));
        if (current != NULL) {
            mm->locked_by = current->pid;
        }
    }
}

static inline void
unlock_mm(struct mm_struct *mm) {
    if (mm != NULL) {
        up(&(mm->mm_sem));
        mm->locked_by = 0;
    }
}

#endif /* !__KERN_MM_VMM_H__ */


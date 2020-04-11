#include <proc.h>
#include <kmalloc.h>
#include <string.h>
#include <sync.h>
#include <pmm.h>
#include <error.h>
#include <sched.h>
#include <elf.h>
#include <vmm.h>
#include <trap.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <fs.h>
#include <vfs.h>
#include <sysfile.h>

/* ------------- process/thread mechanism design&implementation -------------
(an simplified Linux process/thread mechanism )
introduction:
  ucore implements a simple process/thread mechanism. process contains the independent memory sapce, at least one threads
for execution, the kernel data(for management), processor state (for context switch), files(in lab6), etc. ucore needs to
manage all these details efficiently. In ucore, a thread is just a special kind of process(share process's memory).
------------------------------
process state       :     meaning               -- reason
    PROC_UNINIT     :   uninitialized           -- alloc_proc
    PROC_SLEEPING   :   sleeping                -- try_free_pages, do_wait, do_sleep
    PROC_RUNNABLE   :   runnable(maybe running) -- proc_init, wakeup_proc, 
    PROC_ZOMBIE     :   almost dead             -- do_exit

-----------------------------
process state changing:
                                            
  alloc_proc                                 RUNNING
      +                                   +--<----<--+
      +                                   + proc_run +
      V                                   +-->---->--+ 
PROC_UNINIT -- proc_init/wakeup_proc --> PROC_RUNNABLE -- try_free_pages/do_wait/do_sleep --> PROC_SLEEPING --
                                           A      +                                                           +
                                           |      +--- do_exit --> PROC_ZOMBIE                                +
                                           +                                                                  + 
                                           -----------------------wakeup_proc----------------------------------
-----------------------------
process relations
parent:           proc->parent  (proc is children)
children:         proc->cptr    (proc is parent)
older sibling:    proc->optr    (proc is younger sibling)
younger sibling:  proc->yptr    (proc is older sibling)
-----------------------------
related syscall for process:
SYS_exit        : process exit,                           -->do_exit
SYS_fork        : create child process, dup mm            -->do_fork-->wakeup_proc
SYS_wait        : wait process                            -->do_wait
SYS_exec        : after fork, process execute a program   -->load a program and refresh the mm
SYS_clone       : create child thread                     -->do_fork-->wakeup_proc
SYS_yield       : process flag itself need resecheduling, -- proc->need_sched=1, then scheduler will rescheule this process
SYS_sleep       : process sleep                           -->do_sleep 
SYS_kill        : kill process                            -->do_kill-->proc->flags |= PF_EXITING
                                                                 -->wakeup_proc-->do_wait-->do_exit   
SYS_getpid      : get the process's pid

*/

// the process set's list
list_entry_t proc_list;

#define HASH_SHIFT          10
#define HASH_LIST_SIZE      (1 << HASH_SHIFT)
#define pid_hashfn(x)       (hash32(x, HASH_SHIFT))

// has list for process set based on pid
static list_entry_t hash_list[HASH_LIST_SIZE];

// 三个特殊的指针，一个指向空闲进程，一个指向最开始的init进程，最后一个指向当前进程（永远指向当前进程，就相当于进程的this指针）
// idle proc
struct proc_struct *idleproc = NULL;
// init proc
struct proc_struct *initproc = NULL;
// current proc
struct proc_struct *current = NULL;

static int nr_process = 0;

// ↓ 函数声明，函数体是汇编，写在entry.S里（且entry.S只有这一个函数）
void kernel_thread_entry(void);
// 同上，本体是汇编的函数声明，在kern/trap/exception.S里
// 大致内容是把栈顶寄存器向栈底移，然后哗啦哗啦load到各个寄存器
// 是为了把当前栈的内容读进trapframe，这种做法好像在哪也见过
// 另外，x86版在trapEntry.S里——也有可能是这份代码太老了
void forkrets(struct trapframe *tf);
// 同上，函数体在switch.S。大意是保存from进程上下文的寄存器，恢复to的寄存器
void switch_to(struct context *from, struct context *to);

// 创建进程结构体，并初始化（大部分赋值位NULL/0，列表则用专门的初始化函数初始化，最特殊的是cr3（MIPS也有cr3？）和pid）
// alloc_proc - alloc a proc_struct and init all fields of proc_struct
static struct proc_struct *
alloc_proc(void) {
    struct proc_struct *proc = kmalloc(sizeof(struct proc_struct));
    if (proc != NULL) {
      //LAB4:EXERCISE1 2009010989
      proc->state = PROC_UNINIT;
      proc->pid = -1;
      proc->runs = 0;
      proc->kstack = 0;
      proc->need_resched = 0;
      proc->parent = NULL;
      proc->mm = NULL;
      proc->tf = NULL;
      proc->flags = 0;
      proc->need_resched = 0;
      proc->cr3 = boot_cr3;
      memset(&(proc->context), 0, sizeof(struct context));
      memset(proc->name, 0, PROC_NAME_LEN);
      proc->exit_code = 0;
      proc->wait_state = 0;
      list_init(&(proc->run_link));
      list_init(&(proc->list_link));
      proc->time_slice = 0;
      proc->cptr = proc->yptr = proc->optr = NULL;
      proc->fs_struct = NULL;  //初始化fs中的进程控制结构
    }
    return proc;
}

// set_proc_name - set the name of proc
char *
set_proc_name(struct proc_struct *proc, const char *name) {
  memset(proc->name, 0, sizeof(proc->name));
    return memcpy(proc->name, name, PROC_NAME_LEN);
}

// get_proc_name - get the name of proc
char *
get_proc_name(struct proc_struct *proc) {
    static char name[PROC_NAME_LEN + 1];
    memset(name, 0, sizeof(name));
    return memcpy(name, proc->name, PROC_NAME_LEN);
}

// set_links - set the relation links of process
static void
set_links(struct proc_struct *proc) {
    list_add(&proc_list, &(proc->list_link));
    proc->yptr = NULL;
    if ((proc->optr = proc->parent->cptr) != NULL) {
        proc->optr->yptr = proc;
    }
    proc->parent->cptr = proc;
    nr_process ++;
}

// remove_links - clean the relation links of process
static void
remove_links(struct proc_struct *proc) {
    list_del(&(proc->list_link));
    if (proc->optr != NULL) {
        proc->optr->yptr = proc->yptr;
    }
    if (proc->yptr != NULL) {
        proc->yptr->optr = proc->optr;
    }
    else {
       proc->parent->cptr = proc->optr;
    }
    nr_process --;
}

// 给进程分配唯一pid，我记得lab几有问题问过算法……
// get_pid - alloc a unique pid for process
static int
get_pid(void) {
    static_assert(MAX_PID > MAX_PROCESS);
    struct proc_struct *proc;
    list_entry_t *list = &proc_list, *le;
    static int next_safe = MAX_PID, last_pid = MAX_PID;
    if (++ last_pid >= MAX_PID) {
        last_pid = 1;
        goto inside;
    }
    if (last_pid >= next_safe) {
    inside:
        next_safe = MAX_PID;
    repeat:
        le = list;
        while ((le = list_next(le)) != list) {
            proc = le2proc(le, list_link);
            if (proc->pid == last_pid) {
                if (++ last_pid >= next_safe) {
                    if (last_pid >= MAX_PID) {
                        last_pid = 1;
                    }
                    next_safe = MAX_PID;
                    goto repeat;
                }
            }
            else if (proc->pid > last_pid && next_safe > proc->pid) {
                next_safe = proc->pid;
            }
        }
    }
    return last_pid;
}

// 给某个进程cpu，让它真正跑起来
// 关中断，切换页目录表（cr3），调用switch_to切换寄存器内容，最后开中断
// proc_run - make process "proc" running on cpu
// NOTE: before call switch_to, should load  base addr of "proc"'s new PDT
void
proc_run(struct proc_struct *proc) {
    if (proc != current) {
        bool intr_flag;
        struct proc_struct *prev = current, *next = proc;
        local_intr_save(intr_flag);
        {
          //panic("unimpl");
            current = proc;
            //load_sp(next->kstack + KSTACKSIZE);
            lcr3(next->cr3);
            tlb_invalidate_all();
            switch_to(&(prev->context), &(next->context));
        }
        local_intr_restore(intr_flag);
    }
}

// forkret -- the first kernel entry point of a new thread/process
// NOTE: the addr of forkret is setted in copy_thread function
//       after switch_to, the current proc will execute here.
static void
forkret(void) {
    forkrets(current->tf);
}

// ↓ 这两个函数是专门用于操作 hash_list 的插入、删除函数
// hash_proc - add proc into proc hash_list
static void
hash_proc(struct proc_struct *proc) {
    list_add(hash_list + pid_hashfn(proc->pid), &(proc->hash_link));
}

// unhash_proc - delete proc from proc hash_list
static void
unhash_proc(struct proc_struct *proc) {
    list_del(&(proc->hash_link));
}

// 遍历hash_list，找到对应pid的进程
// find_proc - find proc frome proc hash_list according to pid
struct proc_struct *
find_proc(int pid) {
    if (0 < pid && pid < MAX_PID) {
        list_entry_t *list = hash_list + pid_hashfn(pid), *le = list;
        while ((le = list_next(le)) != list) {
            struct proc_struct *proc = le2proc(le, hash_link);
            if (proc->pid == pid) {
                return proc;
            }
        }
    }
    return NULL;
}

// 使用fn函数创建进程。首先申请一个trapframe结构体，初始化后用do_fork创建进程
// do_fork会接着调用copy_thread。最后本函数申请的tf变成了proc->tf
// kernel_thread - create a kernel thread using "fn" function
// NOTE: the contents of temp trapframe tf will be copied to 
//       proc->tf in do_fork-->copy_thread function
int
kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags) {
    struct trapframe tf;
    memset(&tf, 0, sizeof(struct trapframe));
    tf.tf_regs.reg_r[MIPS_REG_A0] = (uint32_t)arg;
    tf.tf_regs.reg_r[MIPS_REG_A1] = (uint32_t)fn;
    tf.tf_regs.reg_r[MIPS_REG_V0] = 0;
    //TODO
    tf.tf_status = read_c0_status();
    tf.tf_status &= ~ST0_KSU;
    tf.tf_status |= ST0_IE;
    tf.tf_status |= ST0_EXL;
    tf.tf_regs.reg_r[MIPS_REG_GP] = __read_reg($28);
    tf.tf_epc = (uint32_t)kernel_thread_entry;
    return do_fork(clone_flags | CLONE_VM, 0, &tf);
}

// 用alloc_pages给进程申请一页，当做kernel stack（proc->kstack）
// setup_kstack - alloc pages with size KSTACKPAGE as process kernel stack
static int
setup_kstack(struct proc_struct *proc) {
    struct Page *page = alloc_pages(KSTACKPAGE);
    if (page != NULL) {
        proc->kstack = (uintptr_t)page2kva(page);
        return 0;
    }
    return -E_NO_MEM;
}

// 与↑呼应，释放proc->kstack
// put_kstack - free the memory space of process kernel stack
static void
put_kstack(struct proc_struct *proc) {
    free_pages(kva2page((void *)(proc->kstack)), KSTACKPAGE);
}

// ↓ 这俩一个申请一个释放pgdir
// setup_pgdir - alloc one page as PDT
static int
setup_pgdir(struct mm_struct *mm) {
    struct Page *page;
    if ((page = alloc_page()) == NULL) {
        return -E_NO_MEM;
    }
    pde_t *pgdir = page2kva(page);
    memcpy(pgdir, boot_pgdir, PGSIZE);
    //panic("unimpl");
    //pgdir[PDX(VPT)] = PADDR(pgdir) | PTE_P | PTE_W;
    mm->pgdir = pgdir;
    return 0;
}

// put_pgdir - free the memory space of PDT
static void
put_pgdir(struct mm_struct *mm) {
    free_page(kva2page(mm->pgdir));
}

// 复制或共享当前进程与传入进程的内存管理器，在do_fork里有用
// copy_mm - process "proc" duplicate OR share process "current"'s mm according clone_flags
//         - if clone_flags & CLONE_VM, then "share" ; else "duplicate"
static int
copy_mm(uint32_t clone_flags, struct proc_struct *proc) {
    struct mm_struct *mm, *oldmm = current->mm;

    /* current is a kernel thread */
    if (oldmm == NULL) {
        return 0;
    }
    if (clone_flags & CLONE_VM) {
        mm = oldmm;
        goto good_mm;
    }

    int ret = -E_NO_MEM;
    if ((mm = mm_create()) == NULL) {
        goto bad_mm;
    }
    if (setup_pgdir(mm) != 0) {
        goto bad_pgdir_cleanup_mm;
    }

    lock_mm(oldmm);
    {
        ret = dup_mmap(mm, oldmm);
    }
    unlock_mm(oldmm);

    if (ret != 0) {
        goto bad_dup_cleanup_mmap;
    }

good_mm:
    mm_count_inc(mm);
    proc->mm = mm;
    proc->cr3 = PADDR(mm->pgdir);
    return 0;
bad_dup_cleanup_mmap:
    exit_mmap(mm);
    put_pgdir(mm);
bad_pgdir_cleanup_mm:
    mm_destroy(mm);
bad_mm:
    return ret;
}

// 用于do_fork，建立新进程的trapframe和上下文，此处的参数tf是do_fork的参数直接传进来的
// copy_thread - setup the trapframe on the  process's kernel stack top and
//             - setup the kernel entry point and stack of process
static void
copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf) {
    proc->tf = (struct trapframe *)(proc->kstack + KSTACKSIZE) - 1;
    *(proc->tf) = *tf;
    proc->tf->tf_regs.reg_r[MIPS_REG_V0] = 0;
    if(esp == 0) //a kernel thread
      esp = (uintptr_t)proc->tf - 32;
    proc->tf->tf_regs.reg_r[MIPS_REG_SP] = esp;
    proc->context.sf_ra = (uintptr_t)forkret;
    proc->context.sf_sp = (uintptr_t)(proc->tf) - 32;
}

//copy_fs&put_fs function used by do_fork in LAB8
static int
copy_fs(uint32_t clone_flags, struct proc_struct *proc) {
    struct fs_struct *fs_struct, *old_fs_struct = current->fs_struct;
    assert(old_fs_struct != NULL);

    if (clone_flags & CLONE_FS) {
        fs_struct = old_fs_struct;
        goto good_fs_struct;
    }

    int ret = -E_NO_MEM;
    if ((fs_struct = fs_create()) == NULL) {
        goto bad_fs_struct;
    }

    if ((ret = dup_fs(fs_struct, old_fs_struct)) != 0) {
        goto bad_dup_cleanup_fs;
    }

good_fs_struct:
    fs_count_inc(fs_struct);
    proc->fs_struct = fs_struct;
    return 0;

bad_dup_cleanup_fs:
    fs_destroy(fs_struct);
bad_fs_struct:
    return ret;
}

static void
put_fs(struct proc_struct *proc) {
    struct fs_struct *fs_struct = proc->fs_struct;
    if (fs_struct != NULL) {
        if (fs_count_dec(fs_struct) == 0) {
            fs_destroy(fs_struct);
        }
    }
}

// 真正创建新进程的函数，记得linux也是叫这个名字
// 使用了前面的很多函数，先用alloc_proc新建proc结构体，然后设置当前进程为新建进程的父亲
// 依次用setup_kstack、copy_mm、copy_thread设置刚建出来的proc结构体
// 把设置好的结构体插入hash_list和proc_list以备管理
// 用wakup_proc改变新建进程状态为RUNNABLE
// 最后的返回值是新建进程的pid
// 写在报告里的：
// 创建一个子进程，先看有没有到进程上限，然后申请`proc_struct`结构体，为子进程建立堆栈、复制内存，把子进程加入追踪进程的表里，最后用 wakeup_proc 把子进程变成 RUNNABLE
// do_fork - parent process for a new child process
//    1. call alloc_proc to allocate a proc_struct
//    2. call setup_kstack to allocate a kernel stack for child process
//    3. call copy_mm to dup OR share mm according clone_flag
//    4. call copy_thread to setup tf & context in proc_struct
//    5. insert proc_struct into hash_list && proc_list
//    6. call wakup_proc to make the new child process RUNNABLE 
//    7. set ret vaule using child proc's pid
int
do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf) {
    int ret = -E_NO_FREE_PROC;
    struct proc_struct *proc;
    if (nr_process >= MAX_PROCESS) {
        goto fork_out;
    }
    ret = -E_NO_MEM;
    //LAB4:EXERCISE2 2009010989
    if ((proc = alloc_proc()) == NULL) {
        goto fork_out;
    }

    proc->parent = current;

    if(setup_kstack(proc)){
        goto bad_fork_cleanup_proc;
    }
    //LAB8:EXERCISE2 2009010989 HINT:how to copy the fs in parent's proc_struct?
    if (copy_fs(clone_flags, proc) != 0) {
        goto bad_fork_cleanup_kstack;
    }
    if (copy_mm(clone_flags, proc)){
        goto bad_fork_cleanup_fs;
    }

    copy_thread(proc, (uint32_t)stack, tf);

    proc->pid = get_pid();
    hash_proc(proc);


    //list_add(&proc_list, &(proc->list_link));
    set_links(proc);

    wakeup_proc(proc);

    ret = proc->pid;

fork_out:
    return ret;

bad_fork_cleanup_fs:
    put_fs(proc);
bad_fork_cleanup_kstack:
    put_kstack(proc);
bad_fork_cleanup_proc:
    kfree(proc);
    goto fork_out;
}

// 用exit_mmap、 put_pgdir、 mm_destro清空大部分进程的内存空间
// 把进程状态改为PROC_ZOMBIE，然后唤醒父进程
// 最后调用调度器切换到其他进程
// do_exit - called by sys_exit
//   1. call exit_mmap & put_pgdir & mm_destroy to free the almost all memory space of process
//   2. set process' state as PROC_ZOMBIE, then call wakeup_proc(parent) to ask parent reclaim itself.
//   3. call scheduler to switch to other process
int
do_exit(int error_code) {
    if (current == idleproc) {
        panic("idleproc exit.\n");
    }
    if (current == initproc) {
        panic("initproc exit.\n");
    }
	
    struct mm_struct *mm = current->mm;
    if (mm != NULL) {
        lcr3(boot_cr3);
        if (mm_count_dec(mm) == 0) {
            exit_mmap(mm);
            put_pgdir(mm);
            mm_destroy(mm);
        }
        current->mm = NULL;
    }
    put_fs(current); //in LAB8
    current->state = PROC_ZOMBIE;
    current->exit_code = error_code;

	
    bool intr_flag;
    struct proc_struct *proc;
    local_intr_save(intr_flag);
    {
        proc = current->parent;
        if (proc->wait_state == WT_CHILD) {
            wakeup_proc(proc);
        }
        while (current->cptr != NULL) {
            proc = current->cptr;
            current->cptr = proc->optr;
	
            proc->yptr = NULL;
            if ((proc->optr = initproc->cptr) != NULL) {
                initproc->cptr->yptr = proc;
            }
            proc->parent = initproc;
            initproc->cptr = proc;
            if (proc->state == PROC_ZOMBIE) {
                if (initproc->wait_state == WT_CHILD) {
                    wakeup_proc(initproc);
                }
            }
        }
    }
    local_intr_restore(intr_flag);
	
    schedule();
    panic("do_exit will not return!! %d.\n", current->pid);
}

//load_icode_read is used by load_icode in LAB8
static int
load_icode_read(int fd, void *buf, size_t len, off_t offset) {
    int ret;
    if ((ret = sysfile_seek(fd, offset, LSEEK_SET)) != 0) {
        return ret;
    }
    if ((ret = sysfile_read(fd, buf, len)) != len) {
        return (ret < 0) ? ret : -1;
    }
    return 0;
}

// load_icode -  called by sys_exec-->do_execve
// 1. create a new mm for current process
// 2. create a new PDT, and mm->pgdir= kernel virtual addr of PDT
// 3. copy TEXT/DATA/BSS parts in binary to memory space of process
// 4. call mm_map to setup user stack, and put parameters into user stack
// 5. setup trapframe for user environment	
/*
    将文件加载到内存中执行
        - 建立内存管理器
        - 建立页目录
        - 将文件逐个段加载到内存中，这里要注意设置虚拟地址与物理地址之间的映射
        - 建立相应的虚拟内存映射表
        - 建立并初始化用户堆栈
        - 处理用户栈中传入的参数
        - 最后很关键的一步是设置用户进程的中断帧
*/
static int
load_icode(int fd, int argc, char **kargv) {
    // 建立内存管理器
    if (current->mm != NULL) { // 要求当前内存管理器为空
        panic("load_icode: current->mm must be empty.\n");
    }
    //panic("unimpl");
    int ret = -E_NO_MEM;  // E_NO_MEM代表因为存储设备产生的请求错误
    struct mm_struct *mm; // 建立内存管理器
    if ((mm = mm_create()) == NULL) {
        goto bad_mm;
    }
    // 建立页目录
    if (setup_pgdir(mm) != 0) {
        goto bad_pgdir_cleanup_mm;
    }

    //assert(((uint32_t)binary & 0x3) == 0);
    // 从文件加载程序到内存
    struct __elfhdr ___elfhdr__;
    struct elfhdr32 __elf, *elf = &__elf;
    // 读取elf文件头
    if ((ret = load_icode_read(fd, &___elfhdr__, sizeof(struct __elfhdr), 0)) != 0) {
        goto bad_elf_cleanup_pgdir;
    }

    _load_elfhdr((unsigned char*)&___elfhdr__, &__elf);

    if (elf->e_magic != ELF_MAGIC) {
        ret = -E_INVAL_ELF;
        goto bad_elf_cleanup_pgdir;
    }

    struct proghdr _ph, *ph = &_ph;
    uint32_t vm_flags, phnum;
    uint32_t perm = 0;
    struct Page *page;
    //e_phnum代表程序段入口地址数目，即多少各段
    for (phnum = 0; phnum < elf->e_phnum; phnum ++) { //循环读取程序的每个段的头部  
      off_t phoff = elf->e_phoff + sizeof(struct proghdr) * phnum;
      if ((ret = load_icode_read(fd, ph, sizeof(struct proghdr), phoff)) != 0) {
        goto bad_cleanup_mmap;
      }
      if (ph->p_type != ELF_PT_LOAD) {
        continue ;
      }
      if (ph->p_filesz > ph->p_memsz) {
        ret = -E_INVAL_ELF;
        goto bad_cleanup_mmap;
      }
      // 建立虚拟地址与物理地址之间的映射
      vm_flags = 0;
      //ptep_set_u_read(&perm);
      perm |= PTE_U;
      if (ph->p_flags & ELF_PF_X) vm_flags |= VM_EXEC;
      if (ph->p_flags & ELF_PF_W) vm_flags |= VM_WRITE;
      if (ph->p_flags & ELF_PF_R) vm_flags |= VM_READ;
      if (vm_flags & VM_WRITE) perm |= PTE_W; 

      if ((ret = mm_map(mm, ph->p_va, ph->p_memsz, vm_flags, NULL)) != 0) {
        goto bad_cleanup_mmap;
      }

      off_t offset = ph->p_offset;
      size_t off, size;
      uintptr_t start = ph->p_va, end, la = ROUNDDOWN_2N(start, PGSHIFT);

      // 复制数据段和代码段
      end = ph->p_va + ph->p_filesz; // 计算数据段和代码段终止地址
      while (start < end) {
        if ((page = pgdir_alloc_page(mm->pgdir, la, perm)) == NULL) {
          ret = -E_NO_MEM;
          goto bad_cleanup_mmap;
        }
        off = start - la, size = PGSIZE - off, la += PGSIZE;
        if (end < la) {
          size -= la - end;
        }
        // 每次读取size大小的块，直至全部读完
        if ((ret = load_icode_read(fd, page2kva(page) + off, size, offset)) != 0) {
          goto bad_cleanup_mmap;
        }
        start += size, offset += size;
      }
      //建立BSS段
      end = ph->p_va + ph->p_memsz; //同样计算终止地址

      if (start < la) {
        if (start >= end) {
          continue ;
        }
        off = start + PGSIZE - la, size = PGSIZE - off;
        if (end < la) {
          size -= la - end;
        }
        memset(page2kva(page) + off, 0, size);
        start += size;
        assert((end < la && start == end) || (end >= la && start == la));
      }

      while (start < end) {
        if ((page = pgdir_alloc_page(mm->pgdir, la, perm)) == NULL) {
          ret = -E_NO_MEM;
          goto bad_cleanup_mmap;
        }
        off = start - la, size = PGSIZE - off, la += PGSIZE;
        if (end < la) {
          size -= la - end;
        }
        //每次操作size大小的块
        memset(page2kva(page) + off, 0, size);
        start += size;
      }
    }
    // 关闭文件，加载程序结束
    sysfile_close(fd);

    //mm->brk_start = mm->brk = ROUNDUP(mm->brk_start, PGSIZE);

    // 建立相应的虚拟内存映射表
    vm_flags = VM_READ | VM_WRITE | VM_STACK;
    if ((ret = mm_map(mm, USTACKTOP - USTACKSIZE, USTACKSIZE, vm_flags, NULL)) != 0) {
      goto bad_cleanup_mmap;
    }
    // 设置用户栈
    mm_count_inc(mm);
    current->mm = mm;
    current->cr3 = PADDR(mm->pgdir);
    lcr3(PADDR(mm->pgdir));

    //LAB5:EXERCISE1 2009010989
    // should set cs,ds,es,ss,esp,eip,eflags
#if 0
    tf->tf_cs = USER_CS;
    tf->tf_ds = tf->tf_es = USER_DS;
    tf->tf_ss = USER_DS;
    tf->tf_esp = USTACKTOP;
    tf->tf_eip = elf->e_entry;
    tf->tf_eflags = FL_IF;
#endif
    // 处理用户栈中传入的参数，其中argc对应参数个数，uargv[]对应参数的具体内容的地址
	uintptr_t stacktop = USTACKTOP - argc * PGSIZE;
    char **uargv = (char **)(stacktop - argc * sizeof(char *));
    int i;
    for (i = 0; i < argc; i ++) { // 将所有参数取出来放置uargv
        uargv[i] = strcpy((char *)(stacktop + i * PGSIZE), kargv[i]);
    }
    //stacktop = (uintptr_t)uargv - sizeof(int);
    //*(int *)stacktop = argc;

    // 设置进程的中断帧
    struct trapframe *tf = current->tf;
    // 初始化tf，设置中断帧
    memset(tf, 0, sizeof(struct trapframe));

    tf->tf_epc = elf->e_entry;
    tf->tf_regs.reg_r[MIPS_REG_SP] = USTACKTOP;
    uint32_t status = read_c0_status();
    status &= ~ST0_KSU;
    status |= KSU_USER;
    status |= ST0_EXL;
    tf->tf_status = status;
    tf->tf_regs.reg_r[MIPS_REG_A0] = argc;
    tf->tf_regs.reg_r[MIPS_REG_A1] = (uint32_t)uargv;

    //kprintf("## %08x\n", tf->tf_status);
    ret = 0;
    // 错误处理部分
out:
    return ret;
bad_cleanup_mmap:
    exit_mmap(mm);
bad_elf_cleanup_pgdir:
    put_pgdir(mm);
bad_pgdir_cleanup_mm:
    mm_destroy(mm);
bad_mm:
    goto out;
}

// this function isn't very correct in LAB8
static void
put_kargv(int argc, char **kargv) {
    while (argc > 0) {
        kfree(kargv[-- argc]);
    }
}

static int
copy_kargv(struct mm_struct *mm, int argc, char **kargv, const char **argv) {
    int i, ret = -E_INVAL;
    if (!user_mem_check(mm, (uintptr_t)argv, sizeof(const char *) * argc, 0)) {
        return ret;
    }
    for (i = 0; i < argc; i ++) {
        char *buffer;
        if ((buffer = kmalloc(EXEC_MAX_ARG_LEN + 1)) == NULL) {
            goto failed_nomem;
        }
        if (!copy_string(mm, buffer, argv[i], EXEC_MAX_ARG_LEN + 1)) {
            kfree(buffer);
            goto failed_cleanup;
        }
        kargv[i] = buffer;
    }
    return 0;

failed_nomem:
    ret = -E_NO_MEM;
failed_cleanup:
    put_kargv(i, kargv);
    return ret;
}

// 用 load_icode 把 ELF 文件加载到内存里，load_icode 修改了 eip 指针，接下来就会执行 ELF 了。
// do_execve - call exit_mmap(mm)&pug_pgdir(mm) to reclaim memory space of current process
//           - call load_icode to setup new memory space accroding binary prog.
int
do_execve(const char *name, int argc, const char **argv) {
    static_assert(EXEC_MAX_ARG_LEN >= FS_MAX_FPATH_LEN);
    struct mm_struct *mm = current->mm;
    if (!(argc >= 1 && argc <= EXEC_MAX_ARG_NUM)) {
        return -E_INVAL;
    }

    char local_name[PROC_NAME_LEN + 1];
    memset(local_name, 0, sizeof(local_name));
	
    char *kargv[EXEC_MAX_ARG_NUM];
    const char *path;
	
    int ret = -E_INVAL;
	
    lock_mm(mm);
    if (name == NULL) {
        snprintf(local_name, sizeof(local_name), "<null> %d", current->pid);
    }
    else {
        if (!copy_string(mm, local_name, name, sizeof(local_name))) {
            unlock_mm(mm);
            return ret;
        }
    }
    if ((ret = copy_kargv(mm, argc, kargv, argv)) != 0) {
        unlock_mm(mm);
        return ret;
    }
    path = argv[0];
    unlock_mm(mm);
    fs_closeall(current->fs_struct);

    /* sysfile_open will check the first argument path, thus we have to use a user-space pointer, and argv[0] may be incorrect */	
    int fd;
    if ((ret = fd = sysfile_open(path, O_RDONLY)) < 0) {
        goto execve_exit;
    }
    if (mm != NULL) {
        lcr3(boot_cr3);
        if (mm_count_dec(mm) == 0) {
            exit_mmap(mm);
            put_pgdir(mm);
            mm_destroy(mm);
        }
        current->mm = NULL;
    }
    ret= -E_NO_MEM;;
    if ((ret = load_icode(fd, argc, kargv)) != 0) {
        goto execve_exit;
    }
    put_kargv(argc, kargv);
    set_proc_name(current, local_name);
    return 0;

execve_exit:
    put_kargv(argc, kargv);
    do_exit(ret);
    panic("already exit: %e.\n", ret);
}

// do_yield - ask the scheduler to reschedule
int
do_yield(void) {
  current->need_resched = 1;
  return 0;
}

// 根据找僵尸态的进程，若传入 pid 为 0，for 循环随便找一个，不为 0 用 find_pid 直接查看。
// 找到后从 hash_list 和 proc_list 里删除这个进程，释放空间。
// 找不到就让当前进程睡眠，调用进程调度程序，之后再找。
// do_wait - wait one OR any children with PROC_ZOMBIE state, and free memory space of kernel stack
//         - proc struct of this child.
// NOTE: only after do_wait function, all resources of the child proces are free.
int
do_wait(int pid, int *code_store) {
  assert(current);
  struct mm_struct *mm = current->mm;
  if (code_store != NULL) {
    if (!user_mem_check(mm, (uintptr_t)code_store, sizeof(int), 1)) {
      return -E_INVAL;
    }
  }

  struct proc_struct *proc;
  bool intr_flag, haskid;
repeat:
  haskid = 0;
  if (pid != 0) {
    proc = find_proc(pid);
        if (proc != NULL && proc->parent == current) {
            haskid = 1;
            if (proc->state == PROC_ZOMBIE) {
                goto found;
            }
        }
    }
    else {
        proc = current->cptr;
        for (; proc != NULL; proc = proc->optr) {
            haskid = 1;
            if (proc->state == PROC_ZOMBIE) {
                goto found;
            }
        }
    }
    if (haskid) {
        current->state = PROC_SLEEPING;
        current->wait_state = WT_CHILD;
        schedule();
        if (current->flags & PF_EXITING) {
            do_exit(-E_KILLED);
        }
        goto repeat;
    }
    return -E_BAD_PROC;

found:
    if (proc == idleproc || proc == initproc) {
        panic("wait idleproc or initproc.\n");
    }
    if (code_store != NULL) {
        *code_store = proc->exit_code;
    }
    local_intr_save(intr_flag);
    {
        unhash_proc(proc);
        remove_links(proc);
    }
    local_intr_restore(intr_flag);
    put_kstack(proc);
    kfree(proc);
    return 0;
}

// 用find_proc找对应pid的进程，给进程的flag设置为PF_EXITING
// 但如果进程的wait_state里有WT_INTERRUPTED，会唤醒那个进程
// do_kill - kill process with pid by set this process's flags with PF_EXITING
int
do_kill(int pid) {
    struct proc_struct *proc;
    if ((proc = find_proc(pid)) != NULL) {
        if (!(proc->flags & PF_EXITING)) {
            proc->flags |= PF_EXITING;
            if (proc->wait_state & WT_INTERRUPTED) {
                wakeup_proc(proc);
            }
            return 0;
        }
        return -E_KILLED;
    }
    return -E_INVAL;
}

// 系统调用SYS_exec
// kernel_execve - do SYS_exec syscall to exec a user program called by user_main kernel_thread
static int
kernel_execve(const char *name, const char **argv) {
    int argc = 0, ret;
    while (argv[argc] != NULL) {
        argc ++;
    }
    //panic("unimpl");
    asm volatile(
      "la $v0, %1;\n" /* syscall no. */
      "move $a0, %2;\n"
      "move $a1, %3;\n"
      "move $a2, %4;\n"
      "move $a3, %5;\n"
      "syscall;\n"
      "nop;\n"
      "move %0, $v0;\n"
      : "=r"(ret)
      : "i"(SYSCALL_BASE+SYS_exec), "r"(name), "r"(argc), "r"(argv), "r"(argc) 
      : "a0", "a1", "a2", "a3", "v0"
    );
    return ret;
}

// 是下面user_main的真正实现，写成宏应该是为了可变参数
#define __KERNEL_EXECVE(name, path, ...) ({                         \
const char *argv[] = {path, ##__VA_ARGS__, NULL};       \
					 kprintf("kernel_execve: pid = %d, name = \"%s\".\n",    \
							 current->pid, name);                            \
					 kernel_execve(name, argv);                              \
})

#define KERNEL_EXECVE(x, ...)                   __KERNEL_EXECVE(#x, #x, ##__VA_ARGS__)

#define KERNEL_EXECVE2(x, ...)                  KERNEL_EXECVE(x, ##__VA_ARGS__)

#define __KERNEL_EXECVE3(x, s, ...)             KERNEL_EXECVE(x, #s, ##__VA_ARGS__)

#define KERNEL_EXECVE3(x, s, ...)               __KERNEL_EXECVE3(x, s, ##__VA_ARGS__)

// 给下面的init_main创建进程的函数
// user_main - kernel thread used to exec a user program
static int
user_main(void *arg) {
    KERNEL_EXECVE(sh);
    panic("user_main execve failed.\n");
}

// 其实是检查你写的程序对不对的——通过新建进程，检查相关属性
// init_main - the second kernel thread used to create user_main kernel threads
static int
init_main(void *arg) {
	int ret;
    if ((ret = vfs_set_bootfs("disk0:")) != 0) {
        panic("set boot fs failed: %e.\n", ret);
    }
	
    size_t nr_free_pages_store = nr_free_pages();
    size_t slab_allocated_store = kallocated();

    int pid = kernel_thread(user_main, NULL, 0);
    if (pid <= 0) {
        panic("create user_main failed.\n");
    }

    while (do_wait(0, NULL) == 0) {
        schedule();
    }

    fs_cleanup();
    kprintf("all user-mode processes have quit.\n");
    assert(initproc->cptr == NULL && initproc->yptr == NULL && initproc->optr == NULL);
    assert(nr_process == 2);
    assert(list_next(&proc_list) == &(initproc->list_link));
    assert(list_prev(&proc_list) == &(initproc->list_link));
    assert(nr_free_pages_store == nr_free_pages());
    assert(slab_allocated_store == kallocated());
    kprintf("init check memory pass.\n");
    return 0;
}

// 初始化proc_list、hash_list。创建第一个内核线程idleproc，然后在创建其子进程initproc
// proc_init - set up the first kernel thread idleproc "idle" by itself and 
//           - create the second kernel thread init_main
void
proc_init(void) {
    int i;

    list_init(&proc_list);
    for (i = 0; i < HASH_LIST_SIZE; i ++) {
        list_init(hash_list + i);
    }

    if ((idleproc = alloc_proc()) == NULL) {
        panic("cannot alloc idleproc.\n");
    }

    idleproc->pid = 0;
    idleproc->state = PROC_RUNNABLE;
    idleproc->kstack = (uintptr_t)bootstack;
    idleproc->need_resched = 1;

    if ((idleproc->fs_struct = fs_create()) == NULL) {
      panic("create fs_struct (idleproc) failed.\n");
    }
    fs_count_inc(idleproc->fs_struct);

    set_proc_name(idleproc, "idle");
    nr_process ++;

    current = idleproc;

    int pid = kernel_thread(init_main, NULL, 0);
    if (pid <= 0) {
        panic("create init_main failed.\n");
    }

    initproc = find_proc(pid);
    set_proc_name(initproc, "init");

    assert(idleproc != NULL && idleproc->pid == 0);
    assert(initproc != NULL && initproc->pid == 1);
}

// 不停轮询看当前进程需不需要调度，需要就调用调度器（貌似只有idleproc会调用此函数？）
// cpu_idle - at the end of kern_init, the first kernel thread idleproc will do below works
void
cpu_idle(void) {
    while (1) {
        if (current->need_resched) {
            schedule();
        }
    }
}

// 创建timer，切换进程状态为PROC_SLEEPING，调用调度器
// 时间到了，这个进程又要跑了，先删除timer
// do_sleep - set current process state to sleep and add timer with "time"
//          - then call scheduler. if process run again, delete timer first.
int
do_sleep(unsigned int time) {
    if (time == 0) {
        return 0;
    }
    bool intr_flag;
    local_intr_save(intr_flag);
    timer_t __timer, *timer = timer_init(&__timer, current, time);
    current->state = PROC_SLEEPING;
    current->wait_state = WT_TIMER;
    add_timer(timer);
    local_intr_restore(intr_flag);

    schedule();

    del_timer(timer);
    return 0;
}

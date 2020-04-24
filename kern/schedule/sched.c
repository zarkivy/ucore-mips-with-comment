// 定义了调度器的接口实现，具体算法实现为 default_sched 中的轮盘调度

#include <list.h>
#include <sync.h>
#include <proc.h>
#include <sched.h>
#include <stdio.h>
#include <assert.h>
#include <default_sched.h>

<<<<<<< Updated upstream
// 增加了定时器（timer）机制，用于进程/线程的do_sleep功能——实验手册
=======
// 计时器链表
>>>>>>> Stashed changes
static list_entry_t timer_list;

// 总调度器指针，指向了 default_sched.c 中的 default_sched_class
static struct sched_class *sched_class;

// 运行进程队列
static struct run_queue *rq;

<<<<<<< Updated upstream
// ↓这三个函数都只是包装一下sched_class->，可能是因为箭头不好打吧
=======
// 进程进入调度器
>>>>>>> Stashed changes
static inline void
sched_class_enqueue(struct proc_struct *proc) {
    if (proc != idleproc) {
        sched_class->enqueue(rq, proc);
    }
}

// 进程离开调度器
static inline void
sched_class_dequeue(struct proc_struct *proc) {
    sched_class->dequeue(rq, proc);
}

// 取调度器中的下一个进程
static inline struct proc_struct *
sched_class_pick_next(void) {
    return sched_class->pick_next(rq);
}

<<<<<<< Updated upstream
// 每一个 tick 调用`sched_class_proc_tick`，
// 如果是 idleproc，就让它`need_resched = 1;`（直接让它待调度）
// 如果不是就调用`sched_class->proc_tick(rq, proc);`
// proc_tick在这里是RR_proc_tick，轮盘调度，看时间片
=======
// 调度器中的进程时钟值+1
>>>>>>> Stashed changes
static void
sched_class_proc_tick(struct proc_struct *proc) {
    // 不为空闲进程则 tick 一次
    if (proc != idleproc) {
        sched_class->proc_tick(rq, proc);
    }
    // 为空闲进程则标记 need reschedule
    else {
        proc->need_resched = 1;
    }
}

static struct run_queue __rq;

<<<<<<< Updated upstream
// 初始化，初始化timer列表、sched_class（管理器）、runqueue
=======

// 调度器初始化函数
>>>>>>> Stashed changes
void
sched_init(void) {
    list_init(&timer_list);

    sched_class = &default_sched_class;

    rq = &__rq; // 这是干嘛
    rq->max_time_slice = 20;
    sched_class->init(rq);

    kprintf("sched class: %s\n", sched_class->name);
}

<<<<<<< Updated upstream
// wakeup_proc函数其实完成了把一个就绪进程放入到就绪进程队列中的工作，为此还调用了一个调度类接口函数sched_class_enqueue，
// 这使得wakeup_proc的实现与具体调度算法无关    —— 摘自实验手册
=======

// 唤醒进程
>>>>>>> Stashed changes
void
wakeup_proc(struct proc_struct *proc) {
    assert(proc->state != PROC_ZOMBIE);
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        if (proc->state != PROC_RUNNABLE) {
            proc->state = PROC_RUNNABLE;
            proc->wait_state = 0;
            if (proc != current) {
                sched_class_enqueue(proc);
            }
        }
        else {
            warn("wakeup runnable process.\n");
        }
    }
    local_intr_restore(intr_flag);
}

// 真正的调度部分，由cpu_idle在need_resched == 1时调用，所以一开始先把它置0
// 如果是RUNNABLE直接加入调度队列（插入run_queue）
// 拿出run_queue第一个进程next，不是NULL的话就从中剔除，
// 是NULL就赋值idleproc（就是说其实run_queue是空的，那这个进程命名为idleproc）
// （可是idleproc应该是在proc_init里就被赋值了，这里再赋值是为什么？）
// 给next增加runs次数，如果next不是当前进程，就让它跑起来（给它CPU）
void
schedule(void) {
    bool intr_flag;
    struct proc_struct *next;
    local_intr_save(intr_flag);
    {
        current->need_resched = 0;
        if (current->state == PROC_RUNNABLE) {
            sched_class_enqueue(current);
        }
        if ((next = sched_class_pick_next()) != NULL) {
            sched_class_dequeue(next);
        }
        if (next == NULL) {
            next = idleproc;
        }
        next->runs ++;
        if (next != current) {
            //kprintf("########################\n");
            //kprintf("c %d TO %d\n", current->pid, next->pid);
            //print_trapframe(next->tf);
            //kprintf("@@@@@@@@@@@@@@@@@@@@@@@@\n");
            proc_run(next);
        }
    }
    local_intr_restore(intr_flag);
}

<<<<<<< Updated upstream
// 添加timer，如果当前timer的剩余时间小于下一个timer，还会把下一个timer减去当前timer这么多的时间
// 可是为什么要这样做呢？
// 向系统添加某个初始化过的timer_t，该定时器在 指定时间后被激活，并将对应的进程唤醒至runnable（如果当前进程处在等待状态）——手册
=======

// 向链表中添加计时器
>>>>>>> Stashed changes
void
add_timer(timer_t *timer) {
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        assert(timer->expires > 0 && timer->proc != NULL);
        assert(list_empty(&(timer->timer_link)));
        list_entry_t *le = list_next(&timer_list);
        while (le != &timer_list) {
            timer_t *next = le2timer(le, timer_link);
            if (timer->expires < next->expires) {
                next->expires -= timer->expires;
                break;
            }
            timer->expires -= next->expires;
            le = list_next(le);
        }
        list_add_before(le, &(timer->timer_link));
    }
    local_intr_restore(intr_flag);
}

<<<<<<< Updated upstream
// 删除timer，还会把dangqiantimer的剩余时间给下一个timer
// 可是为什么要这么做呢？
// 向系统删除（或者说取消）某一个定时器。该定时器在取消后不会被系统激活并唤醒进程——手册
=======
// 从链表中删除计时器
>>>>>>> Stashed changes
void
del_timer(timer_t *timer) {
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        if (!list_empty(&(timer->timer_link))) {
            if (timer->expires != 0) {
                list_entry_t *le = list_next(&(timer->timer_link));
                if (le != &timer_list) {
                    timer_t *next = le2timer(le, timer_link);
                    next->expires += timer->expires;
                }
            }
            list_del_init(&(timer->timer_link));
        }
    }
    local_intr_restore(intr_flag);
}

// run_timer_list函数在每次timer中断处理过程中被调用，从而可用来调用调度算法所需的timer时间事件感知操作，
// 调整相关进程的进程调度相关的属性值。通过调用调度类接口函数sched_class_proc_tick使得此操作与具体调度算法无关——实验手册
// 简而言之是timer到期了就wakeup_proc，然后不管到没到期都调用sched_class_proc_tick
// 更新当前系统时间点，遍历当前所有处在系统管理内的定时器，找出所有应该激活的计数器，
// 并激活它们。该过程在且只在每次定时器中断时被调用。在ucore 中，其还会调用调度器事件处理程序——实验手册
void
run_timer_list(void) {
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        list_entry_t *le = list_next(&timer_list);
        if (le != &timer_list) {
            timer_t *timer = le2timer(le, timer_link);
            assert(timer->expires != 0);
            timer->expires --;
            while (timer->expires == 0) {
                le = list_next(le);
                struct proc_struct *proc = timer->proc;
                if (proc->wait_state != 0) {
                    assert(proc->wait_state & WT_INTERRUPTED);
                }
                else {
                    warn("process %d's wait_state == 0.\n", proc->pid);
                }
                wakeup_proc(proc);
                del_timer(timer);
                if (le == &timer_list) {
                    break;
                }
                timer = le2timer(le, timer_link);
            }
        }
        sched_class_proc_tick(current);
    }
    local_intr_restore(intr_flag);
}

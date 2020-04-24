// 定义了调度器、计时器与进程队列的数据结构与相关内联函数

#ifndef __KERN_SCHEDULE_SCHED_H__
#define __KERN_SCHEDULE_SCHED_H__

#include <defs.h>
#include <list.h>
//#include <skew_heap.h>

struct proc_struct;

// timer结构体，有让它可以被加入list的le，还有时间、指向进程的指针
typedef struct {
    unsigned int expires;
    struct proc_struct *proc;
    list_entry_t timer_link;
} timer_t;

#define le2timer(le, member)            \
to_struct((le), timer_t, member)

// 对某定时器 进行初始化，让它在 expires 时间片之后唤醒 proc 进程——手册
static inline timer_t *
timer_init(timer_t *timer, struct proc_struct *proc, int expires) {
    timer->expires = expires;
    timer->proc = proc;
    list_init(&(timer->timer_link));
    return timer;
}

struct run_queue;

// 为了适应多种调度算法总结而来的接口
// The introduction of scheduling classes is borrrowed from Linux, and makes the 
// core scheduler quite extensible. These classes (the scheduler modules) encapsulate 
// the scheduling policies. 
struct sched_class {
    // the name of sched_class
    const char *name;
    // Init the run queue
    void (*init)(struct run_queue *rq);
    // put the proc into runqueue, and this function must be called with rq_lock
    void (*enqueue)(struct run_queue *rq, struct proc_struct *proc);
    // get the proc out runqueue, and this function must be called with rq_lock
    void (*dequeue)(struct run_queue *rq, struct proc_struct *proc);
    // choose the next runnable task
    struct proc_struct *(*pick_next)(struct run_queue *rq);
    // dealer of the time-tick
    void (*proc_tick)(struct run_queue *rq, struct proc_struct *proc);
	/* for SMP support in the future
	 *  load_balance
	 *	 void (*load_balance)(struct rq* rq);
	 *  get some proc from this rq, used in load_balance,
	 *  return value is the num of gotten proc
	 *  int (*get_proc)(struct rq* rq, struct proc* procs_moved[]);
	 */
};

// 名字叫队列，其实只是一个列表，两个列表的属性集合在一起而已
struct run_queue {
    list_entry_t run_list;
    unsigned int proc_num;
    int max_time_slice;
};

void sched_init(void);
void wakeup_proc(struct proc_struct *proc);
void schedule(void);
void add_timer(timer_t *timer);
void del_timer(timer_t *timer);
void run_timer_list(void);

#endif /* !__KERN_SCHEDULE_SCHED_H__ */

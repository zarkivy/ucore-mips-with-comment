#include <defs.h>
#include <list.h>
#include <proc.h>
#include <assert.h>
#include <default_sched.h>

// 初始化run_queue的run_list
static void
RR_init(struct run_queue *rq) {
    list_init(&(rq->run_list));
    rq->proc_num = 0;
}

// 把某进程加入run_queue末尾（本质是插入run_list）
static void
RR_enqueue(struct run_queue *rq, struct proc_struct *proc) {
    assert(list_empty(&(proc->run_link)));
    list_add_before(&(rq->run_list), &(proc->run_link));
    // 在RR_proc_tick里时间片==0直接重新调度了，再加回来时时间片可能还是==0，这里给它初始化成最大时间片
    // 另外时间片比最大时间片还大的情况我不知道是在哪会出现……
    if (proc->time_slice == 0 || proc->time_slice > rq->max_time_slice) {
        proc->time_slice = rq->max_time_slice;
    }
    proc->rq = rq;
    rq->proc_num ++;
}

// 从run_queue剔除某个进程（因为run_queue本质是run_list的包装，所以↑和↓两个函数都不能忘了操作proc_num）
static void
RR_dequeue(struct run_queue *rq, struct proc_struct *proc) {
    assert(!list_empty(&(proc->run_link)) && proc->rq == rq);
    list_del_init(&(proc->run_link));
    rq->proc_num --;
}

// 从run_queue里弹出第一个进程（不过并不是插入或删除，所以不用管proc_num）
// 在run_queue为空时返回NULL
// （我寻思不是有list_empty函数么？非要直接判断取出来的le是不是list的头，还是我理解错了）
static struct proc_struct *
RR_pick_next(struct run_queue *rq) {
    list_entry_t *le = list_next(&(rq->run_list));
    if (le != &(rq->run_list)) {
        return le2proc(le, run_link);
    }
    return NULL;
}

// 简单易懂的轮盘调度，每个tick非空闲进程都会执行这个函数
// 时间片还有就自减，没有就标记为待调度
static void
RR_proc_tick(struct run_queue *rq, struct proc_struct *proc) {
    if (proc->time_slice > 0) {
        proc->time_slice --;
    }
    if (proc->time_slice == 0) {
        proc->need_resched = 1;
    }
}

// 初始化sched_class，类似内存控制的manager，用函数指针实现多态
// 实验中还有别的调度算法，但这个代码里直接用的轮盘算法
struct sched_class default_sched_class = {
    .name = "RR_scheduler",
    .init = RR_init,
    .enqueue = RR_enqueue,
    .dequeue = RR_dequeue,
    .pick_next = RR_pick_next,
    .proc_tick = RR_proc_tick,
};


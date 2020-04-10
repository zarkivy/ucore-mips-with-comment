#include <defs.h>
#include <assert.h>
#include <thumips.h>
#include <picirq.h>
#include <asm/mipsregs.h>

static bool did_init = 0;

//可编程中断控制器 ，针对中断向量表进行操作，之后通过case进行判断

//开中断
void
pic_enable(unsigned int irq) {
    assert(irq < 8);
    uint32_t sr = read_c0_status(); 
    sr |= 1<<(irq + STATUSB_IP0);
    write_c0_status(sr);
}

//关中断
void pic_disable(unsigned int irq){
    assert(irq < 8);
    uint32_t sr = read_c0_status(); 
    sr &= ~(1<<(irq + STATUSB_IP0));
    write_c0_status(sr);
}

//中断初始化
void
pic_init(void) {
    uint32_t sr = read_c0_status(); 
    /* disable all */
    sr &= ~ST0_IM;
    write_c0_status(sr);
    did_init = 1;
}


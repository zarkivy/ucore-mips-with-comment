#include <thumips.h>
#include <trap.h>
#include <stdio.h>
#include <picirq.h>
#include <sched.h>
#include <asm/mipsregs.h>

volatile size_t ticks;

#define TIMER0_INTERVAL  1000000 

static void reload_timer()
{
  uint32_t counter = read_c0_count();
  counter += TIMER0_INTERVAL;
  write_c0_compare(counter);
}

//时钟中断处理函数
int clock_int_handler(void * data)
{
  ticks++;
//  if( (ticks & 0x1F) == 0)
//    cons_putc('A');
  run_timer_list();  
  reload_timer(); 
  return 0;
}

//初始化Clock计数器，开启TIMER0_IRQ的中断
void
clock_init(void) {
  reload_timer(); 
  pic_enable(TIMER0_IRQ);
  kprintf("++setup timer interrupts\n");
}


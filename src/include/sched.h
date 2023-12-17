#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <debug.h>
#include <util.h>
#include <kmalloc.h>
#include <mmu.h>
#include <list.h>
#include <process.h>
#include <rbtree.h>

extern RBTree* sched_runnable_procs;



#define NO_HART -1

void sched_init();
void sched_invoke(uint32_t hart);
void sched_add(Process *p);
void sched_remove(Process* p);
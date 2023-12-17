#include <sched.h>
#include <list.h>
#include <process.h>
#include <rbtree.h>
#include <lock.h>
#include <page.h>
#include <config.h>
#include <util.h>
#include <csr.h>
#include <sbi.h>

static RBTree* g_runnable_procs = NULL;

static List* g_processes;
static uint32_t curr_proc_idx;
static ListElem* curr_list_elem = NULL;

Process* idle_process = NULL;
static List* g_waiting_procs = NULL;

Mutex tree_lock;

Process* global_proc;

uint64_t trap_stack_vaddr;

static bool comparator(uint64_t left, uint64_t right){
    Process* left_p = (Process*)left;
    Process* right_p = (Process*)right;

    return left_p->stats.vruntime > right_p->stats.vruntime;
}

void sched_init(){
    
    // g_processes = list_new();
    trap_stack_vaddr = (uint64_t)kzalloc(4096);
    g_processes = list_new();
    curr_proc_idx = 0;

    //initialize idle process
    idle_process = process_new(PM_SUPERVISOR);
    idle_process->frame.sepc = (uint64_t)idle_proc;
    idle_process->frame.xregs[10] = &idle_process->frame.xregs[XREG_SP];
    int num_instrs = 4096;
    void *image_page;
    image_page = page_zalloc();
    image_page = memcpy(image_page, ALIGN_DOWN_POT((uint64_t)idle_proc, PAGE_SIZE_4K), num_instrs);
    list_add_ptr(idle_process->rcb.image_pages, image_page);
    mmu_map(idle_process->ptable, ALIGN_DOWN_POT((uint64_t)idle_proc, PAGE_SIZE_4K), image_page, MMU_LEVEL_4K, PB_WRITE | PB_READ | PB_EXECUTE);

    curr_list_elem = NULL;
}

void sched_add(Process *p) {
    //debugf("ADD PROC\n");
    list_add_ptr(g_processes, (uint64_t)p);
    global_proc = p;
    // debugf("sched_add: list size %d pid %d\n", list_size(g_processes), p->pid);
}

//Need to add Mutex logic here
Process* get_proc_on_hart(uint32_t hart){

    Process* curr = NULL;
    ListElem *curr_elem;

    list_for_each(g_processes, curr_elem) {
        curr = (Process*)list_elem_value_ptr(curr_elem);
        if(curr->hart == hart){
            return curr;
        }
    }

    return NULL;
}
//This seems fine for now...
static void jettison_proc_from_hart(Process* p){
    //First update the vruntime of p

    p->stats.vruntime += sbi_get_time() - p->ran_at;
    p->hart = NO_HART;

    //If the process is still runnable, then we insert it back into the rb tree
    if(p->state == PS_RUNNING){
        rb_insert(g_runnable_procs, (int)p->stats.vruntime, p);
    }
    //Cleanup dead process
    else if(p->state == PS_DEAD){
        list_remove(g_processes, p);
        process_free(p);
    }

}

//I am assuming this is a function we implement. 
static bool sleep_until(Process* p, unsigned long time){
    mutex_spinlock(&tree_lock);

    if(p->hart != NO_HART){
        return false;
    }

    p->sleep_until = (unsigned long)sbi_get_time() + time;
    p->state = PS_SLEEPING;

    mutex_unlock(&tree_lock);
    return true;
}

Process* schedule_proc(){

    //First we grab the next proccess!

    Process* next_proc = NULL;
    bool return_val = rb_min_val(g_runnable_procs, &next_proc);

    if(return_val == false){
        return NULL;
    }

    uint64_t key = next_proc->stats.vruntime;
    
    //Delete current proc that we got from the rb tree. 
    //Insertion happens once we deschedule a process if the process is still runnable!
    rb_delete(g_runnable_procs, key);

    return next_proc;
}

void awaken_sleeping_procs(){

    Process* curr = NULL;
    ListElem *curr_elem;

    uint64_t curr_time = sbi_get_time();
    list_for_each_ascending(g_processes, curr_elem) {
        curr = (Process*)list_elem_value_ptr(curr_elem);
        if(curr->state == PS_SLEEPING && curr->sleep_until <= curr_time){
            curr->state = PS_RUNNING;
            // rb_insert(g_runnable_procs, curr->stats.vruntime, curr);
        }
    }
}

static void round_robin(uint32_t hart){
    Process* p = NULL;
    
    mutex_spinlock(&tree_lock);

    //wake up procs that can be
    awaken_sleeping_procs();

    //initial list elem
    if(curr_list_elem == NULL){
        curr_list_elem = list_elem_start_descending(g_processes);
    }

    // save state of process
    p = get_proc_on_hart(hart);
    if(p == NULL && idle_process->hart == hart){
        p = idle_process;
    }
    
    // save state of proc
    if(p != NULL){
        uint64_t* sepc;
        CSR_READ(sepc, "sepc");
        p->frame.sepc = sepc;
        p->hart = -1U;
    }

    //get next process that can run. NULL if none
    Process* temp_p;
    ListElem* prev_list_elem = curr_list_elem;
    curr_list_elem = list_elem_next(curr_list_elem);
    if(list_size(g_processes) != 0){
        while(curr_list_elem != prev_list_elem){
            if(!list_elem_valid(g_processes, curr_list_elem)){
                curr_list_elem = list_elem_start_ascending(g_processes);
            }
            else{
                curr_list_elem = list_elem_next(curr_list_elem);

            }

            temp_p = (Process*) list_elem_value_ptr(curr_list_elem);
            if(temp_p->state == PS_RUNNING){
                p = temp_p;
                break;
            }
        }
    }

    if(p == NULL){
        p = idle_process;
    }
    p->hart = hart;

    // debugf("round_robin: scheduling process pid %d\n", p->pid);

    uint64_t trap_stack_paddr = (uint64_t)mmu_translate(kernel_mmu_table, trap_stack_vaddr);
    mmu_map(p->ptable, trap_stack_vaddr, trap_stack_paddr, MMU_LEVEL_4K, PB_READ | PB_WRITE | PB_EXECUTE);
    p->frame.trap_stack = trap_stack_vaddr;
    sbi_add_timer(hart, CONTEXT_SWITCH_TIMER * p->quantum);
    p->stats.vruntime += 1;
    mutex_unlock(&tree_lock);
    if(process_run(p, hart)) debugf("process successfully returned!\n");
    bool test_sched = true;
}

static void cfs(hart){

    //First we need to pull the current process off the specified hart!
    
    //First we go through the global process list and see if any sleeping procs can be awaken and put on the rb tree!
    awaken_sleeping_procs();
    
    Process* curr_proc = get_proc_on_hart(hart);

    //Now that we have the current proccess on the hart, we have to remove it from the hart!

    if(curr_proc != NULL){
        
        jettison_proc_from_hart(curr_proc);

    }

    Process* next_proc = schedule_proc();
    if(next_proc != NULL){

        sbi_add_timer(hart, CONTEXT_SWITCH_TIMER * next_proc->quantum);
        next_proc->ran_at = sbi_get_time();
        process_run(next_proc, hart);

    }else{
        //Figure this out as well, Not really sure how this works. 
        process_run(idle_proc, hart);
    }

}

// void add_proc_to_rb_tree(Process* p){

//     if(p->state == PS_RUNNING){
//         rb_insert(g_runnable_procs, p->stats.vruntime, (uint64_t)p);
//     }

// }


//Add a proc to the global list, AND, if the proc can run, put it in the RB tree.
static bool sched_add_proc(Process* new_proc){

    if (new_proc == NULL || g_processes == NULL){
        return false;
    }
    
    //We want to add the proccess to the global list
    
    new_proc->stats.vruntime = 0;
    list_add_ptr(g_processes, (uint64_t)new_proc);

    mutex_spinlock(&tree_lock);
    //And if the proccess is runnable, we want to add it to the RB tree with a key of zero
    if(new_proc->state == PS_RUNNING){
        rb_insert(g_runnable_procs, new_proc->stats.vruntime, (uint64_t)new_proc);
    }
    mutex_unlock(&tree_lock);
    return true;
}

void sched_cfs_init(){
    g_processes = list_new();
    g_runnable_procs = rb_new();
    //I have to figure this out. 
    idle_process = process_new(0);

}


//This is where we remove the proccess from the global process list
//This also should remove it from the rb tree assuming
// void sched_remove_proc(Process* p, uint64_t tree_key){
    
//     list_remove(g_processes, (uint64_t)p);

//     proccess_free(p);

// }

void sched_remove(Process* p){
    mutex_spinlock(&tree_lock);
    list_remove(g_processes, (uint64_t)p);
    mutex_unlock(&tree_lock);
}


void sched_remove_proc_rb(uint64_t v_runtime){

    mutex_spinlock(&tree_lock);

    rb_delete(g_runnable_procs, v_runtime);

    mutex_unlock(&tree_lock);
}

void sched_invoke(uint32_t hart){
    round_robin(hart);
    // cfs(hart);
}
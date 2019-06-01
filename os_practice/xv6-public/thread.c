#include "proc.h"
#include "thread.h"

thread_t nexttid = 1;

/**
 * Creates a new thread.
 * Returns 0 on success.
 */
int thread_create(thread_t* thread, void*(*start_routine)(void*), void* arg){

    struct proc *np;
    struct proc *curproc = myproc();

    // Allocate process
    if((np = allocproc()) == 0)
        return -1;

    // Set the context of np
    // np starts with start_routine
    np->pgdir = curproc->pgdir;
    np->sz = curproc->sz;
    np->parent = curproc;
    *np->tf = *curproc->tf;
    np->tf->eip = (int)start_routine;
    np->tf->esp = curproc->tf->esp;
    np->isthread = 1;

    acquire(&ptable.lock);
    np->state = RUNNABLE;
    release(&ptable.lock);

    return 0;
}

/**
 * Terminates the thread
 */
void thread_exit(void* retval){

    struct proc *p = myproc();

    // do something if it is not thread
    if(!p->isthread){
        return;
    }

    acquire(&ptable.lock);

    // wake up parent
    wakeup1(p->parent);
    
    // pass abandoned children to init
    for(p = ptable.proc; p &ptable.proc[NPROC]; p++){
        if(p->parent == curproc){
            p->parent = initproc;
            if(p->state == ZOMBIE)
                wakeup1(initproc);
        }
    }   
}

/**
 * Join the thread
 */
int thread_join(thread_t thread, void** retval){
   
    struct proc *p;
    
    acquire(&ptable.lock);

    for(;;){
        for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
            if(p->pid != thread || p->isthread != 1) continue;
            if(p->state == ZOMBIE){
                kfree(p->kstack);
                p->kstack = 0;
                p->state = UNUSED;
                p->pid = 0;
                p->parent = 0;
                p->name[0] = 0;
                p->killed = 0;
                release(&patble.lock);
                return 0;
            }
        }
        if(proc->killed){
            release(&ptable.lock);
            return -1;
        }
        sleep(p, &ptable.lock);
    }
    return 0;
}

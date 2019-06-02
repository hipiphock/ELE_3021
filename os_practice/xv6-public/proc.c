#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#define NULL 0
#define PASSWORD 2016025423

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  // initialized for FCFS scheduler
  p->createtime = ticks;
  p->tickcounts = 0;
  // initialized for MLFQ scheduler
  p->level = 0;
  p->priority = 0;

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  // initialized for threada implementation
  // this differenciate process from thread
  p->tid = 0;
  p->original = 0;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return oldsz on success, -1 on failure.
int
growproc(int n)
{
  uint sz, oldsz;
  struct proc *curproc = myproc();
  struct proc *p;   // added for thread implementation

  acquire(&ptable.lock);

  p = (curproc->original) ? curproc->original : curproc;

  sz = p->sz;
  oldsz = sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0){
      release(&ptable.lock);
      return -1;
    }
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0){
      release(&ptable.lock);
      return -1;
    }
  }
  p->sz = sz;
  release(&ptable.lock);
  switchuvm(curproc);
  return oldsz;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state form proc.
  // Memory space is managed by original process
  if(curproc->tid != 0)
      //thread use original's sz
      np->pgdir = copyuvm(curproc->pgdir, curproc->original->sz);
  else
      np->pgdir = copyuvm(curproc->pgdir, curproc->sz);
  // if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
  if(np->pgdir == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd, threadnum;

  if(curproc == initproc)
    panic("init exiting");

  // First, check for threads.
  // You must kill all of the threads to exit.
  if(curproc->tid == 0){
    acquire(&ptable.lock);
    for(;;){
      threadnum = 0;
      for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
        if(p->original == curproc){
          // case: thread p is already zombie
          if(p->state == ZOMBIE){
            // clean up
            kfree(p->kstack);
            p->kstack = 0;
            p->original->emptyvm.data[p->original->emptyvm.size++] = p->vmba;
            p->pid = 0;
            p->parent = 0;
            p->original = 0;
            p->name[0] = 0;
            p->killed = 0;
            p->state = UNUSED;
            deallocuvm(p->pgdir, p->sz, p->vmba);
          }
          // case: thread p is not zombie
          else{
            // kill the thread
            threadnum++;
            p->killed = 1;
            wakeup1(p);
          }
        }
      }
      if(threadnum == 0){
        release(&ptable.lock);
        break;
      }
      // wait for thread to exit.
      sleep(curproc, &ptable.lock);
    }
  }

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // case: process
  if(curproc->tid == 0)
    // Parent might be sleeping in wait().
    wakeup1(curproc->parent);

  // case: thread
  else{
    // case: original process is alive
    if(curproc->original){
      curproc->original->killed = 1;
      wakeup1(curproc->original);
    }
  }

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);

#ifdef  FCFS_SCHED
    struct proc* minproc = NULL;
    // Find the process that is the oldest
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
        if(p->state == RUNNABLE){
            if(minproc == NULL)
                minproc = p;
            else{
                if(p->createtime < minproc->createtime){
                    minproc = p;
                }
            }
        }
    }
    // Switch to chosen process.
    if(minproc != NULL){
        p = minproc;
        p->tickcounts = 0;      // initialize tickcounts when scheduled - for FCFS scheduler
        c->proc = p;
        switchuvm(p);
        p->state = RUNNING;
        swtch(&(c->scheduler), p->context);
        switchkvm();
        c->proc = 0;  
    }

#elif   MLFQ_SCHED
    // first, search for level 0
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
       if(p->level == 0){
           if(p->state == RUNNABLE){
               p->level = 1;
               c->proc = p;
               switchuvm(p);
               p->state = RUNNING;

               swtch(&(c->scheduler), p->context);
               switchkvm();

               c->proc = 0;
           }
       }
    }
    // if there were runnable p with level 0,
    // this if case would be ignored.
    struct proc* maxproc = NULL;
    if(p == &ptable.proc[NPROC]){
        for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
            if(p->state == RUNNABLE){
                if(maxproc == NULL)
                    maxproc = p;
                else
                    if(maxproc->priority < p->priority)
                        maxproc = p;
                if(maxproc->priority == 10){
                    p = maxproc;
                    c->proc = p;
                    switchuvm(p);
                    p->state = RUNNING;

                    swtch(&(c->scheduler), p->context);
                    switchkvm();

                    c->proc = 0;
                    maxproc = NULL;
                    break;
                }
            }
        }
        if(maxproc != NULL){
            p = maxproc;
            c->proc = p;
            switchuvm(p);
            p->state = RUNNING;

            swtch(&(c->scheduler), p->context);
            switchkvm();
        
            c->proc = 0;
        }
    }

#else
    // Default case: using Round Robin
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
#endif
    release(&ptable.lock);

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

// function for FCFS scheduler
// increase the process's tickcounts
void ageprocess(){
    struct proc* p;
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
        if(p->state == RUNNING)
            p->tickcounts++;
    
    release(&ptable.lock);
}

// function for MLFQ scheduler
// boost processes
void boostprocess(){
    struct proc* p;
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
        p->level = 0;
        p->priority = 0;
    }
    release(&ptable.lock);
}

// function for MLFQ scheduler
int getlev(void){
    int level = myproc()->level;
    return level;
}

// function for MLFQ scheduler
void setpriority(int pid, int priority){
    
    // case: priority not between 0 and 10
    if(priority < 0 || priority > 10)
        return;

    // normal case
    struct proc* p;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
        if(p->pid == pid){
            p->priority = priority;
            return;
        }
}

// function for MLFQ scheduler
void monopolize(int password){
    if(password != PASSWORD){
        cprintf("Wrong Password!\n");
        myproc()->killed = 1;
    }
}


/**
 * the followings were added for thread implementation
 */

thread_t nexttid = 1;

/**
 * Creates a new thread.
 * Returns 0 on success.
 */
int thread_create(thread_t* thread, void*(*start_routine)(void*), void* arg){

    int i;
    uint sz, sp, vmba;
    pde_t* pgdir;
    struct proc *np;
    struct proc *curproc = myproc();
    struct proc *original = curproc->original ? curproc->original : curproc;

    // Allocate process
    if((np = allocproc()) == 0)
        return -1;
    nextpid--;

    // Set the thread's basic properties
    np->original = original;
    np->pid = original->pid;
    np->tid = nexttid++;

    acquire(&ptable.lock);
    pgdir = original->pgdir;
    
    // Use the empty memory of the process first.
    // If there is no memory, increase the virtual memory
    // and give the new memory at the top.
    if(original->emptyvm.size)
        vmba = original->emptyvm.data[--original->emptyvm.size];
    else{
        vmba = original->sz;
        original->sz += 2 * PGSIZE;
    }

    // Allocate two pages for current thread
    if((sz = allocuvm(pgdir, vmba, vmba + 2 * PGSIZE)) == 0){
        // case: allocation failed
        np->state = UNUSED;
        return -1;
    }
    release(&ptable.lock);

    // Set the state of np
    *np->tf = *original->tf;
    
    for(i = 0; i < NOFILE; i++)
        if(original->ofile[i])
            np->ofile[i] = filedup(original->ofile[i]);
    np->cwd = idup(original->cwd);

    safestrcpy(np->name, original->name, sizeof(original->name));

    sp = sz - 4;
    *((uint*)sp) = (uint)arg;   // arg
    sp -= 4;
    *((uint*)sp) = 0xffffffff;  // PC
    np->pgdir = pgdir;
    np->vmba = vmba;
    np->sz = sz;
    np->tf->eip = (uint)start_routine;  // entry point
    np->tf->esp = sp;   // stack pointer
    
    *thread = np->tid;  // fucking error

    // Make the new thread runnable
    acquire(&ptable.lock);
    np->state = RUNNABLE;
    release(&ptable.lock);

    return 0;
}

/**
 * Terminates the thread
 */
void thread_exit(void* retval){

    struct proc *curproc = myproc();
    int fd;

    for(fd = 0; fd < NOFILE; fd++){
        if(curproc->ofile[fd]){
            fileclose(curproc->ofile[fd]);
            curproc->ofile[fd] = 0;
        }
    }

    begin_op();
    iput(curproc->cwd);
    end_op();
    curproc->cwd = 0;

    acquire(&ptable.lock);

    curproc->tmp_retval = retval;

    // Original process might be sleeping in wait().
    wakeup1(curproc->original);
    
    // Jump into the scheduler, never to return.
    curproc->state = ZOMBIE;
    sched();
    panic("zombie exit");
}

/**
 * join the thread
 */
int thread_join(thread_t thread, void** retval){
    
    struct proc *p;
    struct proc *curproc = myproc();

    // Only original process can call thread_join
    if(curproc->tid != 0)
        return -1;
    
    acquire(&ptable.lock);
    for(;;){
        for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
            if(p->tid != thread) continue;

            if(p->original != curproc){
                release(&ptable.lock);
                return -1;
            }

            if(p->state == ZOMBIE){
                *retval = p->tmp_retval;
                kfree(p->kstack);
                p->kstack = 0;
                p->original->emptyvm.data[p->original->emptyvm.size++] = p->vmba;
                p->pid = 0;
                p->parent = 0;
                p->original = 0;
                p->name[0] = 0;
                p->killed = 0;
                p->state = UNUSED;
                deallocuvm(p->pgdir, p->sz, p->vmba);
                release(&ptable.lock);
                return 0;
            }
        }

        if(curproc->killed){
            release(&ptable.lock);
            return -1;
        }

        // Wait for thread to exit
        sleep(curproc, &ptable.lock);
    }
    return 0;
}

// Helper function for exec system call
// Make other processes and threads with given pid go to sleep
// except for the exception.
// The process with given pid will start a new program in exec.
void put_to_sleep(int pid, struct proc* exception){

    struct proc* p;

    acquire(&ptable.lock);

    if(myproc()->killed){
        release(&ptable.lock);
        return;
    }

    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
        if(p->pid == pid && p != exception){
            p->killed = 1;
            p->chan = 0;
            p->state = SLEEPING;
        }
    }
    release(&ptable.lock);
}

// Helper function for exec system call
// Wake up the previous processes and threads with given pid
// except for the exception.
void wake_up_again(int pid, struct proc* exception){
    
    int childexist = 0;
    struct proc* p;

    acquire(&ptable.lock);

    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
        if(p->pid == pid && p != exception){
            p->state = RUNNABLE;
            if(p->parent){
                p->parent = exception;
                childexist = 1;
            }
        }
    }
    release(&ptable.lock);
    if(childexist)
        wait();
}

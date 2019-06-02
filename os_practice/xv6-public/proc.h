// Per-CPU state
struct cpu {
  uchar apicid;                // Local APIC ID
  struct context *scheduler;   // swtch() here to enter scheduler
  struct taskstate ts;         // Used by x86 to find stack for interrupt
  struct segdesc gdt[NSEGS];   // x86 global descriptor table
  volatile uint started;       // Has the CPU started?
  int ncli;                    // Depth of pushcli nesting.
  int intena;                  // Were interrupts enabled before pushcli?
  struct proc *proc;           // The process running on this cpu or null
};

extern struct cpu cpus[NCPU];
extern int ncpu;

//PAGEBREAK: 17
// Saved registers for kernel context switches.
// Don't need to save all the segment registers (%cs, etc),
// because they are constant across kernel contexts.
// Don't need to save %eax, %ecx, %edx, because the
// x86 convention is that the caller has saved them.
// Contexts are stored at the bottom of the stack they
// describe; the stack pointer is the address of the context.
// The layout of the context matches the layout of the stack in swtch.S
// at the "Switch stacks" comment. Switch doesn't save eip explicitly,
// but it is on the stack and allocproc() manipulates it.
struct context {
  uint edi;
  uint esi;
  uint ebx;
  uint ebp;
  uint eip;
};

enum procstate { UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

// helper structure for thread's resoruce clean-up
struct emptyvm{
    uint data[NPROC];
    int size;
};


// Per-process state
struct proc {
  uint sz;                     // Size of process memory (bytes)
  pde_t* pgdir;                // Page table
  char *kstack;                // Bottom of kernel stack for this process
  enum procstate state;        // Process state
  int pid;                     // Process ID
  struct proc *parent;         // Parent process
  struct trapframe *tf;        // Trap frame for current syscall
  struct context *context;     // swtch() here to run process
  void *chan;                  // If non-zero, sleeping on chan
  int killed;                  // If non-zero, have been killed
  struct file *ofile[NOFILE];  // Open files
  struct inode *cwd;           // Current directory
  char name[16];               // Process name (debugging)
  // added for FCFS scheduler
  uint createtime;             
  uint tickcounts;             
  // added for MLFQ scheduler
  int level;                   
  int priority;
  // added for thead implementation
  int tid;                     // thread id. 0 if it is process
  struct proc* original;       // original process for this thread
  void* tmp_retval;            // temporary value for thread_exit
  uint vmba;                   // base address of virtual memory
  struct emptyvm emptyvm;      // empty memory space of original process
};

// Process memory is laid out contiguously, low addresses first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap


// added for scheduler implementation
void ageprocess();                          // added for FCFS scheduler
void boostprocess();                        // added for MLFQ scheduler
int getlev(void);                           // added for MLFQ scheduler
void setpriority(int pid, int priority);    // added for MLFQ scheduler
void monopolize(int password);              // added for MLFQ scheduler


// added for thread implementation
typedef int thread_t;

int thread_create(thread_t* thread, void*(*start_routine)(void*), void* arg);
void thread_exit(void* retval);
int thread_join(thread_t thread, void** retval);
void put_to_sleep(int pid, struct proc* exception);
void wake_up_again(int pid, struct proc* exception);

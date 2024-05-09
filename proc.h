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
/*
 * Added WAIT which will be a low priority sleep
 */
enum procstate { UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE, WAIT};

#define DEFAULT_KERNEL_PRIORITY = 2
#define DEFAULT_USER_PRIORITY = 1
#define SLOAD                  1     // process is in core
#define SSYS                   2     //scheduling process
#define SSWAP                  4    //process is being swapped
#define SLOCK                  8     //

#define SCHED                  0
#define KERNEL_PROC            1
#define USER_PROC              2

#define CHILD_SAME_PRI         0
#define CHILD_DIFF_PRI         1


// Per-process state
struct proc {
  uint sz;                     // Size of process memory (bytes)
  int p_sig;                  //The signal sent to this process
  char p_pri;                  // The priority of this process, for scheduling
  int p_time_quantum;         //The resident time for scheduling
  int p_time_taken;
  char p_nice;                  //nice value, lower nice value means more important and should be scheduled faster, higher nice value means opposite
  char p_flag;                  //Flag indicating the schedule status of this proc
  int space_flag;              //flag to mark a process as either kernel space or user space
  int child_pri;               //A binary flag that will just indicate whether any children on fork should retain the same scheduling priority.
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
};

// Process memory is laid out contiguously, low addresses first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap


/* Dustyn's extra function */

uint tally_allocated_memory_for_all_procs(void);
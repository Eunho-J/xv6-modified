// #include "priority_queue.h"
#define QUEUE_MLFQ			      0
#define QUEUE_STRIDE		      1
#define LEVEL_MLFQ_0          0
#define LEVEL_MLFQ_1          1
#define LEVEL_MLFQ_2          2
#define LEVEL_STRIDE          -1
#define LEVEL_MLFQ_AS_PROC    -2

struct q_node {
	int share;
	int level;
	uint turnCount;
	float distance;
	struct proc* proc;
	struct q_node* next;
};

struct q_header {
	int type;
	int level;
	struct q_node* next;
};


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

struct blanktsb {
  int cnt;
  uint blanklist[NPROC];
};



enum procstate { UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

// Per-process state
struct proc {
  uint sz;                     // Size of process memory (bytes) (Same for every LWPs in the same group)
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
  struct file *tempofile[NOFILE];
  struct inode *tempcwd;           // Current directory
  char name[16];               // Process name (debugging)

  thread_t tid;                // Thread ID
  uint tsb;                    // Stack base point of the thread
  struct blanktsb bl;          // Blank thread used stack base list(empty spaces for now)
  struct proc *master;
  int nthread;
  void *ret_val;               // Return value of the thread

  struct q_header lwps;        // lwp queue of the process

  struct q_node p_node;        // process node for scheduling queue
};

// Process memory is laid out contiguously, low addresses first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap

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
  struct thread *thread;       // The thread running on this cpu or null
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

struct thread {
  char *t_kstack;
  enum procstate t_state;
  thread_t tid;
  struct proc *master;
  struct trapframe *t_tf;
  struct context *t_context;
  void *t_chan;
  int t_killed;

  struct q_node t_node;
}


struct blanktsb {
  int cnt;
  uint blanklist[NPROC];
};


// Per-process state
struct proc {
  uint sz;                     // Size of process memory (bytes)
  pde_t* pgdir;                // Page table
  // char *kstack;                // Bottom of kernel stack for this process
  enum procstate state;        // Process state
  int pid;                     // Process ID
  struct proc *parent;         // Parent process
  // struct trapframe *tf;        // Trap frame for current syscall
  // struct context *context;     // swtch() here to run process
  void *chan;                  // If non-zero, sleeping on chan
  int killed;                  // If non-zero, have been killed
  struct file *ofile[NOFILE];  // Open files
  struct inode *cwd;           // Current directory
  char name[16];               // Process name (debugging)

  struct q_node p_node;        // process node for scheduling queue

  int nrunnablet;              // runnable threads number
  struct q_header q_thread;
};

// Process memory is laid out contiguously, low addresses first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap
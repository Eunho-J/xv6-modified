#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "priority_queue.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;
struct q_header* mlfq0 = 0;
struct q_header* mlfq1;
struct q_header* mlfq2;
struct q_header* stride;
struct q_node* mlfqSched;
int sysyield_called = 0;
int remaining_tickets = 80;

uint mlfq_tickCount = 0;


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

  // cprintf("!!!!!!!!!!!allocproc called!!!!!!!\n");

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

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

  // for MLFQ & Stride, init values are 0
  p->turnCount = 0;
  p->tickets = 0;
  p->tickCount = 0;
  p->p_node = 0;
  // create q_node of process & push to mlfq0, which is initial one.
  struct q_node* temp = queue_newNode(p, !IS_MLFQSCHED);

  p->p_node = temp;
  queue_push(&mlfq0, &temp);

  
  // cprintf("process added to mlfq0: pid %d\n", p->pid);
  // struct q_node* temp2 = mlfq0->next;
  // if(temp2 != 0)
  // { 
  //   int i = 0;
  //   cprintf("%d 'th pid %d\n", i++, temp2->p->pid);
  //   while (temp2->next != 0)
  //   {
  //     cprintf("%d 'th pid %d\n", i++, temp2->next->p->pid);
  //     temp2 = temp2->next;
  //   }
  // }

  //cprintf("state: %d\n", p->state);
  // Q. priority boost needed..? nope.

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  if(mlfq0 == 0){
    // init scheduling queues
    mlfq0 = queue_newHeader(QUEUE_MLFQ);
    mlfq1 = queue_newHeader(QUEUE_MLFQ);
    mlfq2 = queue_newHeader(QUEUE_MLFQ);
    stride = queue_newHeader(QUEUE_STRIDE);
  }

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

  //cprintf("set RUNABBLE!!!\n");
  p->state = RUNNABLE;  

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
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
    cprintf("fork failed\n");
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    cprintf("fork failed\n");
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
  int fd;

  if(curproc == initproc)
    panic("init exiting");

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

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

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

// boosts every priority in mlfq scheduler.
void
priority_boost(void)
{
  struct q_node* temp = queue_popall(&mlfq1);
  queue_pushall(&mlfq0, &temp);
  temp = queue_popall(&mlfq2);
  queue_pushall(&mlfq1, &temp);
  mlfq_tickCount = 0;

  // reset all turnCount in mlfq0 except runnig process
  temp = mlfq0->next;
  if( temp != 0 ){
    while(temp->next != 0) {
      if(temp->p->state != RUNNING)
        temp->p->turnCount = 0;
      temp = temp->next;
    }
  }
  // reset all turnCount in mlfq0 except runnig process
  temp = mlfq1->next;
  if( temp != 0 ){
    while(temp->next != 0) {
      if(temp->p->state != RUNNING)
        temp->p->turnCount = 5;
      temp = temp->next;
    }
  }
  return;
}

int
set_cpu_share(int share)
{
  return 1;
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
  cprintf("hello in scheduler 1\n");
  struct proc *p;
  // struct q_node* stride_selected;
  // struct q_node* mlfq_selected = 0;
  struct cpu *c = mycpu();
  c->proc = 0;

  cprintf("hello in scheduler 2\n");
  // push mlfqSched into stide queue
  // this is for considering mlfq as a process in Stride with 20 shares.
  // mlfqSched = queue_newNode(0, IS_MLFQSCHED);
  // queue_push(&stride, &mlfqSched);
  
  // loop never ends
  for(;;){
    
    // Enable interrupts on this processor.
    sti();
    
    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    // //cprintf("hello in scheduler 4\n");
  
    // stride_selected = queue_pop(&stride);
    // if(stride_selected->isMLFQ)
    // { // mlfq scheduling selected in stride.

    
    //   uint temp = mlfq_tickCount;
    //   while (mlfq_tickCount - temp < 4)                  //mlfq runs for 4 ticks only(stride slice is 4)
    //   { 
        
    //     if(mlfq_selected == 0)                          // process turn is over
    //     {
    //       if(!queue_isEmpty(&mlfq0)) {                  // case: mlfq0 has nodes
    //         mlfq_selected = queue_pop(&mlfq0);
    //       } else if(!queue_isEmpty(&mlfq1)) {           // case: mlfq0 has no nodes but mlfq1 has.
    //         mlfq_selected = queue_pop(&mlfq1);
    //       } 
    //       else if(!queue_isEmpty(&mlfq2)) {             // case: only mlfq2 has nodes.
    //         mlfq_selected = queue_pop(&mlfq2);
    //       } 
    //       else { // case: mlfq has no process
    //         break;
    //       }
    //     } 

    //     //cprintf("test 1\n");
    //     p = mlfq_selected->p;

    //     if (p->state != RUNNABLE){
    //       cprintf("process name: %s, pid: %d, state: %d\n", p->name, p->pid, p->state);
    //       mlfq_selected = 0;
    //       continue;
    //     }
        
    //     //cprintf("test runnable yes!!!!!\n");
    //     // cprintf("test 2\n");
    //     p->tickCount++;
    //     // cprintf("test 3\n");
    //     mlfq_tickCount++;
    //     // cprintf("test 4\n");

        
    //     c->proc = p;
    //     switchuvm(p);
    //     p->state = RUNNING;
    //     swtch(&(c->scheduler), p->context);
    //     switchkvm();

    //     if (p->state == RUNNABLE || p->state == SLEEPING)  //still have something to do
    //     {
    //       if (p->tickets == 0) //still in mlfq
    //       {
    //         if (p->turnCount < 5)               // queue 0
    //         {
    //           if(p->tickCount >= 1)             // turnover
    //           {
    //             queue_push(&mlfq0, &mlfq_selected);
    //             mlfq_selected = 0;
    //             p->tickCount = 0;
    //           }
    //         }
    //         else if (p->turnCount < 10)         // queue 1
    //         {
    //           if(p->tickCount >= 2)             // turnover
    //           {
    //             queue_push(&mlfq1, &mlfq_selected);
    //             mlfq_selected = 0;
    //             p->tickCount = 0;
    //           }
    //         }
    //         else                                // queue 2
    //         {
    //           if(p->tickCount >= 4)             // turnover
    //           {
    //             queue_push(&mlfq2, &mlfq_selected);
    //             mlfq_selected = 0;
    //             p->tickCount = 0;
    //           }
    //         }
    //       }
    //       else                  //moved to stride
    //       {
    //         queue_push(&stride, &mlfq_selected);
    //         mlfq_selected = 0;
    //         p->tickCount = 0;
    //       }
    //     }
    //     else
    //     {
    //       mlfq_selected = 0;
    //       p->tickCount = 0;
    //       p->turnCount = 0;
    //     }
        
    //     c->proc = 0;
        
    //     // release(&ptable.lock);
    //   } // while loop end

    //   cprintf("test while end\n");

    //   //mlfq has 20 shares in total 100 -> distance of 1 step is 5.
    //   mlfqSched->distance = mlfqSched->distance + 5; 
    //   queue_push(&stride, &mlfqSched);

    // }
    // else
    // { // other process selected in Stride scheduling
      





    //   p = stride_selected->p;
    //   p->turnCount++;
    //   for (int i = 0; i < 4; i++)         // loop for 4 ticks
    //   {

















    //   }
      
    // }
    // cprintf("for one loop ended\n");
    // release(&ptable.lock);

//------------------------RR scheduling-----------------------

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

      if (mlfq_tickCount == 100){
        priority_boost();
      }
      
      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);
//-------------------------------------------------------------
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
    
    cprintf("ptable not same pname: %s\n", p->name);
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;
  cprintf("pname: %s\n", p->name);

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

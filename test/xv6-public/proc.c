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

int nextpid = 1;
struct q_header* mlfq0;
struct q_header* mlfq1;
struct q_header* mlfq2;
struct q_header* stride;
struct q_node* mlfqInStride;

int sysyield_called = 0;
int remaining_tickets = 80;


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
  // cprintf("allocproc starting\n");

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;
  // p->tickets = 0;
  // p->tickCount = 0;
  if(p->p_node == 0){
    struct q_node* temp = queue_newNode(p->pid);
    p->p_node = temp;
  }
  p->p_node->turnCount = 0;
  p->p_node->tickCount = 0;
  p->p_node->tickets = 0;
  p->p_node->distance = 0;
  p->p_node->isRunnable = 0;
  p->p_node->pid = p->pid;
  p->p_node->next = 0;

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
  
  // cprintf("allocproc complete!\n");

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];
  // cprintf("starting setting: scheduling queue\n");

  if(mlfq0 == 0){
    mlfq0 = queue_newHeader(QUEUE_MLFQ);
    mlfq1 = queue_newHeader(QUEUE_MLFQ);
    mlfq2 = queue_newHeader(QUEUE_MLFQ);
    stride = queue_newHeader(QUEUE_STRIDE);
    mlfqInStride = queue_newNode(IS_MLFQ_IN_STRIDE);
    queue_push(stride, mlfqInStride);
    // cprintf("completed setting: scheduling queue\n");
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
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
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

int
set_cpu_share(int share){
  struct proc* curproc = myproc();
  if(remaining_tickets >= share){
    acquire(&ptable.lock);
    curproc->p_node->tickets = share;
    remaining_tickets = remaining_tickets - share;
    release(&ptable.lock);
    return curproc->p_node->tickets;
  }
  return 0;
}

// boosts every priority in mlfq scheduler.
// except running process.
void
priority_boost(void)
{
  struct q_node* temp = mlfq1->next;
  mlfq1->next = 0;
  queue_push(mlfq0, temp);
  temp = mlfq2->next;
  mlfq2->next = 0;
  queue_push(mlfq1, temp);

  temp = mlfq0->next;
  while (temp != 0)
  {
    temp->tickCount = 0;
    temp->turnCount = 0;
    temp = temp->next;
  }
  temp = mlfq1->next;
  while (temp != 0)
  {
    temp->tickCount = 0;
    temp->turnCount = 5;
    temp = temp->next;
  }
  
  return;
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
  struct q_node *q_stride = 0;
  struct q_node *q_mlfq = 0;
  struct cpu *c = mycpu();
  int tpt; //ticks per turn for mlfq
  int pid_q;
  // int found = 0;
  uint mlfq_ticks = 0; //for calc distance of mlfq scheduler
  // uint stride_tickCount = 0; //for calc runned process in stride scheduler
  uint xticks_boost = 0; //for check mlfq to boost priority
  c->proc = 0;

  if(mlfq0 == 0){
    mlfq0 = queue_newHeader(QUEUE_MLFQ);
    mlfq1 = queue_newHeader(QUEUE_MLFQ);
    mlfq2 = queue_newHeader(QUEUE_MLFQ);
    stride = queue_newHeader(QUEUE_STRIDE);
    mlfqInStride = queue_newNode(IS_MLFQ_IN_STRIDE);
    queue_push(stride, mlfqInStride);
  }

  mlfqInStride->tickets = 20;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();
    // Loop over process table looking for process to run.

    
    acquire(&ptable.lock);
    // if(q_stride != 0 && q_stride->pid > 0) cprintf("n0[%d] ", q_stride->pid);
    


    // TODO: ptable에서 RUNNABLE로 바뀐 프로세스들 큐에 다시 추가해주느 코드
    struct proc* temp_p;
    for(temp_p = ptable.proc; temp_p < &ptable.proc[NPROC]; temp_p++){
      if (temp_p->state == RUNNABLE && !temp_p->p_node->isRunnable){ //was not RUNNABLE, but now is RUNNABLE
        temp_p->p_node->isRunnable = 1;
        if(temp_p->p_node->tickets == 0) {         //mlfq
          if(temp_p->p_node->turnCount < 5){
            // cprintf("pushed back to m0\n");
            queue_push(mlfq0, temp_p->p_node);
          }else if(temp_p->p_node->turnCount < 10){
            // cprintf("pushed back to m1\n");
            queue_push(mlfq1, temp_p->p_node);
          }else {
            // cprintf("pushed back to m2\n");
            queue_push(mlfq2, temp_p->p_node);
          }
        }else{                                //stride
          // cprintf("pushed back to st\n");
          queue_push(stride, temp_p->p_node);
        }
      } 
      // else if (temp_p->state == RUNNABLE){
      //   //동시성 문제로 lost 된 노드 다시 push 해 주는 코드
      //   // RUNNABLE 중에 자신을 next로 가지고 있거나
      //   // header가 자신을 next로 가지고 있으면 queue에 있는 것.
      //   // 그게 아니면 queue에서 누락된 것.
      //   struct proc* finder;
      //   for(finder = ptable.proc; finder < &ptable.proc[NPROC]; finder++){
      //     if(finder->state == RUNNABLE){
      //       if(finder->p_node->next == temp_p->p_node) {
      //         found = 1;
      //         break;
      //       }
      //     } else continue;
      //   }
      //   if(!found) {
      //     if(mlfq0->next == temp_p->p_node) found = 1;
      //     else if(mlfq1->next == temp_p->p_node) found = 1;
      //     else if(mlfq2->next == temp_p->p_node) found = 1;
      //     else if(stride->next == temp_p->p_node) found = 1;
      //   }

      //   if(!found){
      //     temp_p->p_node->isRunnable = 0;
      //     break;
      //   }
      // }
    }

    // if(!found){
    //   cprintf("nf#### \n");
    //   found = 0;
    //   release(&ptable.lock);
    //   continue;
    // } else found = 0;




    temp_p = 0;  //prevent unexpected change with temp_p


    if(mlfq_ticks == 0 && !queue_isEmpty(stride)){
      q_stride = queue_pop(stride);
      // cprintf("1");
    } else {
      q_stride = mlfqInStride;
      // cprintf("2");
    }

    if (q_stride->pid == -1)                            // MLFQ selection
    { 
      if (!queue_isEmpty(mlfq0)) {
        q_mlfq = queue_pop(mlfq0);
        tpt = 1;
      } else if (!queue_isEmpty(mlfq1)) {
        q_mlfq = queue_pop(mlfq1);
        tpt = 2;
      } else if (!queue_isEmpty(mlfq2)) {
        q_mlfq = queue_pop(mlfq2);
        tpt = 4;
      } else {
        mlfqInStride->turnCount++;
        mlfqInStride->distance = (uint)(mlfqInStride->turnCount * (uint)(100 - remaining_tickets) / 20);
        queue_push(stride, mlfqInStride);
        mlfq_ticks = 0;
        // cprintf("..");
        // cprintf("mlfq is empty\n");
        release(&ptable.lock);
        continue;                   
      }
      
      pid_q = q_mlfq->pid;

    } 
    else                                                // STRIDE selection
    { 
      tpt = 4;
      // cprintf("stride calle!!!!!!!\n");
      pid_q = q_stride->pid;
    }

    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if (p->pid == pid_q)
        break;
    }


    for (uint i = 0; i < tpt ; i++)   // execute turn
    { 
      

      if(sysyield_called){
        sysyield_called = 0;
        break;
      }
      if(p->state != RUNNABLE)
        break;
      
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      c->proc = 0;
      // cprintf("proc run!\n");

      if(q_stride->pid == -1){
        mlfq_ticks++;
        xticks_boost++;
        if(xticks_boost >= 100){
          priority_boost();
          p->p_node->turnCount = 0;
          p->p_node->tickCount = 0;
          xticks_boost = 0;
        }
      } else {
        p->p_node->tickCount++;
      }
      
    }

    // end of a turn
    p->p_node->turnCount++;

    if(q_stride->pid == -1) {    // was mlfq
      
      if(mlfq_ticks >= 4){                // mlfq scheduler worked for 4+a ticks.
        // if(p->pid > 2)
        //   cprintf("[%d] mlfq end %d\n", p->pid, mlfq_ticks);
        mlfqInStride->turnCount++;
        mlfqInStride->distance = (uint)(mlfqInStride->turnCount * (uint)(100 - remaining_tickets) / 20);
        queue_push(stride, mlfqInStride);
        mlfq_ticks = 0;                   // means mlfq scheduler turn has over
      }else{
        // if(p->pid > 2)
        //   cprintf("[%d] mlfq not end %d\n",p->pid, mlfq_ticks);
      }
    } else {                     // was stride
      //do nothing
    }

    if(p->state == RUNNABLE){                // is RUNNABLE
      if(q_stride->pid == -1){                            // was mlfq
        if(q_mlfq->tickets == 0) {                               //is still mlfq
          if(p->p_node->turnCount < 5){
            queue_push(mlfq0, q_mlfq);
          } else if(p->p_node->turnCount < 10){
            queue_push(mlfq1, q_mlfq);
          } else{
            queue_push(mlfq2, q_mlfq);
          }
        } else {                                                //will be stride                                        
          queue_resetTickCount(stride);
          mlfqInStride->tickCount = 0;
          mlfqInStride->turnCount = 0;
          mlfqInStride->distance = 0;
          q_mlfq->tickCount = 0;
          q_mlfq->turnCount = 0;
          queue_push(stride, q_mlfq);
          // cprintf("ws[%d] ", q_mlfq->pid);
        }
      } else {                                            // was stride
        q_stride->distance = (uint)(q_stride->turnCount * (uint)(100 - remaining_tickets) / q_stride->tickets);
        queue_push(stride, q_stride);
        // cprintf("s[%d] ", q_stride->pid);
      }
    } else {                                // not RUNNABLE

      if(p->state == ZOMBIE || p->state == UNUSED){     //process is fin.
        p->p_node->tickCount = 0;
        p->p_node->turnCount = 0;
        p->p_node->distance = 0;
        
        if(q_stride->pid != -1 )                                    // if finished process was stride
          remaining_tickets = remaining_tickets + q_stride->tickets;
        p->p_node->tickets = 0;
        p->p_node->isRunnable = 0;
      } else {                                           //not finished, but not RUNNABLE
        // cprintf("nr\n");
				if(p->p_node->tickets != 0)
        	p->p_node->distance = (uint)(q_stride->turnCount * (uint)(100 - remaining_tickets) / q_stride->tickets);
 				p->p_node->isRunnable = 0;
        // later, if it is RUNNABLE, push to queue again.
      }
    }
    q_stride = 0;
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

void
yield2(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  // cprintf("sysyield called\n");
  sysyield_called=1;
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

int
getlev(void)
{
  uint turnCount;
  uint tickets;
  acquire(&ptable.lock);
  tickets = myproc()->p_node->tickets;
  turnCount = myproc()->p_node->turnCount;
  release(&ptable.lock);

  if(tickets != 0) return 3;

  if(turnCount < 5) return 0;
  else if (turnCount < 10) return 1;
  else if (turnCount) return 2;
  
  return -1;
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

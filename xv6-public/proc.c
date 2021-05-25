#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
  struct thread thread[NTHREAD];
} ptable;

static struct proc *initproc;

int nextpid = 1;
thread_t nexttid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

struct q_header mlfq_0;
struct q_header mlfq_1;
struct q_header mlfq_2;
struct q_header stride;
struct q_node mlfq_as_proc;

int shareleft = 80;


void
queue_init(void)
{
  shareleft = 80;

  mlfq_0.level = 0;
  mlfq_0.type = QUEUE_MLFQ;
  mlfq_0.next = 0;

  mlfq_1.level = 1;
  mlfq_1.type = QUEUE_MLFQ;
  mlfq_1.next = 0;

  mlfq_2.level = 2;
  mlfq_2.type = QUEUE_MLFQ;
  mlfq_2.next = 0;

  stride.level = -1;
  stride.type = QUEUE_STRIDE;
  stride.next = 0;

  mlfq_as_proc.level = LEVEL_MLFQ_AS_PROC;
  mlfq_as_proc.share = 20;
  mlfq_as_proc.turnCount = 0;
  mlfq_as_proc.distance = 0;
  mlfq_as_proc.proc = 0;
  mlfq_as_proc.next = 0;
  queue_push(&stride, &mlfq_as_proc);
}

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

struct thread*
mythread(void) {
  struct cpu *c;
  struct thread *t;
  pushcli();
  c = mycpu();
  t = c->proc->t_node->thread;
  popcli();
  return t;
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
  struct thread *t;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto pfound;

  release(&ptable.lock);
  return 0;

pfound:
  for(t = ptable.thread; t < &ptable.thread[NTHREAD]; t++)
    if(t->tstate == UNUSED)
      goto tfound;

  release(&ptable.lock);
  return 0;

tfound:
  p->state = EMBRYO;
  p->pid = nextpid++;

  t->tstate = EMBRYO;
  t->tid = nexttid++;
  t->t_node.thread = t;
  t->t_node.next = 0;

  p->t_node = &(t->t_node);
  p->nrt = 0;
  p->nt = 0;
  p->bl.cnt = 0;
  t->chan = 0;

  p->tl.next = 0;
  p->tl.level = 0;
  p->tl.type = QUEUE_MLFQ;

  t->master = p;

  release(&ptable.lock);

  // Allocate kernel stack.
  if((t->kstack = kalloc()) == 0){
    t->tstate = UNUSED;
    p->state = UNUSED;
    p->t_node = 0;
    t->master = 0;
    return 0;
  }
  sp = t->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *t->tf;
  t->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *t->context;
  t->context = (struct context*)sp;
  memset(t->context, 0, sizeof *t->context);
  t->context->eip = (uint)forkret;

  // Set up node for scheduling queue
  p->p_node.proc = p;
  p->p_node.next = 0;
  p->p_node.share = 0;
  p->p_node.turnCount = 0;
  p->p_node.distance = 0;
  p->p_node.level = 0;
  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  struct thread * t;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  t = p->t_node->thread; //first thread
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(t->tf, 0, sizeof(*t->tf));
  t->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  t->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  t->tf->es = t->tf->ds;
  t->tf->ss = t->tf->ds;
  t->tf->eflags = FL_IF;
  t->tf->esp = PGSIZE;
  t->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;
  t->tstate = RUNNABLE;
  p->nrt = 1;
  p->nt = 1;
  // Push node to mlfq_0 when state changed to RUNNABLE.
  queue_push(&mlfq_0, &(p->p_node));

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();
  // cprintf("growproc\n");

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
  struct thread *nt;
  struct proc *curproc = myproc();
  struct thread *curthread = mythread();
  struct q_node *temp;


  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }
  nt = np->t_node->thread;


  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    cprintf("fork failed\n");
    kfree(nt->kstack);
    nt->kstack = 0;
    np->state = UNUSED;
    nt->tstate = UNUSED;
    np->t_node = 0;
    nt->master = 0;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  np->tl.next = 0;
  *nt->tf = *curthread->tf;
  nt->tsb = curthread->tsb;

  // Clear %eax so that fork returns 0 in the child.
  nt->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  for(int i = 0; i < curproc->bl.cnt; i++){
    np->bl.blanklist[i] = curproc->bl.blanklist[i];
  }
  np->bl.cnt = curproc->bl.cnt;

  for(temp = curproc->tl.next; temp != 0; temp = temp->next){
    np->bl.blanklist[np->bl.cnt++] = temp->thread->tsb;
    deallocuvm(np->pgdir, temp->thread->tsb + 2*PGSIZE, temp->thread->tsb);
  }

  np->state = RUNNABLE;
  nt->tstate = RUNNABLE;
  np->nrt = 1;
  np->nt = 1;
  
  // Push to mlfq_0 when state changed to RUNNABLE
  queue_push(&mlfq_0, &(np->p_node));

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
  if (curproc->p_node.level == LEVEL_STRIDE)
  {
    shareleft = shareleft + curproc->p_node.share;
    curproc->p_node.share = 0;
  }
  
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
  struct thread *tempt;
  struct q_node *tempnode;
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){

        for(tempnode = p->t_node; tempnode != 0; tempnode = queue_pop(&p->tl)){
          tempt = tempnode->thread;
          kfree(tempt->kstack);
          tempt->master = 0;
          tempt->context = 0;
          tempt->chan = 0;
          tempt->tf = 0;
          tempt->tsb = 0;
          tempt->tstate = UNUSED;
          tempt->tid = 0;
          tempt->kstack = 0;
        }

        // Found one.
        pid = p->pid;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->bl.cnt = 0;
        p->sz = 0;
        p->t_node = 0;
        p->nrt = 0;
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

void
priority_boost(void)
{
  // bpriority boost of mlfq queues.
  struct q_node* q;

  q = queue_popall(&mlfq_1);
  queue_pushall(&mlfq_0, q);
  q = mlfq_0.next;
  while (q != 0)
  {
    q->level = LEVEL_MLFQ_0;
    q->turnCount = 0;
    q = q->next;
  }
  q = queue_popall(&mlfq_2);
  queue_pushall(&mlfq_1, q);
  q = mlfq_1.next;
  while (q != 0)
  {
    q->level = LEVEL_MLFQ_1;
    q->turnCount = 0;
    q = q->next;
  }
  
  struct proc *p;
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->state == RUNNING)
    {
      p->p_node.turnCount = 0;
      if (p->p_node.level > LEVEL_MLFQ_0)
      {
        p->p_node.level--;
      }
    }
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
  struct q_node *node;
  struct cpu *c = mycpu();
  uint mlfq_tickCount = 0;

  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);

    // prevent buffer overflow of distances
    if (stride.next->distance > 399 ) // means every node in stride has about 100 - shareleft distance
    {
      // reset all distance of stride processes to prevent buffer overflow.
      for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
      {
        if (p->p_node.level == LEVEL_STRIDE)
        {
          p->p_node.turnCount = 0;
          p->p_node.distance = 0;
        }
      }
      mlfq_as_proc.turnCount = 0;
      mlfq_as_proc.distance = 0;
      p = 0;
    }

    if ((node = queue_pop(&stride)) == 0)
    {
      panic("stride is empty!\n");
    }
    
    
    if(node->level == LEVEL_MLFQ_AS_PROC)
    {
      mlfq_as_proc.turnCount++;
      mlfq_as_proc.distance = mlfq_as_proc.turnCount * 400 / mlfq_as_proc.share;
      queue_push(&stride, &mlfq_as_proc);
      for (int i = 0; i < 4;)
      {
        if(queue_hasRunnable(&mlfq_0)) // mlfq_0 has runnable node
        {
          node = queue_pop(&mlfq_0);
          if (node->proc->state == SLEEPING)
          {
            queue_push(&mlfq_0,node);
            continue;
          }
          i++;
          mlfq_tickCount+=5;
        }
        else if (queue_hasRunnable(&mlfq_1)) // mlfq_1 has runnable node
        {
          node = queue_pop(&mlfq_1);
          if (node->proc->state == SLEEPING)
          {
            queue_push(&mlfq_1,node);
            continue;
          }
          i += 2;
          mlfq_tickCount += 10;
        }
        else if (queue_hasRunnable(&mlfq_2)) // mlfq_2 has runnable node
        {
          node = queue_pop(&mlfq_2);
          if (node->proc->state == SLEEPING)
          {
            queue_push(&mlfq_2,node);
            continue;
          }
          i += 4;
          mlfq_tickCount += 20;
        }
        else // mlfq has no runnable node
        {
          break;
        }
        

        node->turnCount++;
        if (node->turnCount >= 5 && node->level < 2)
        {
          node->turnCount = 0;
          node->level++;
        }
        
        p = node->proc;
        
        
        c->proc = p;
        switchuvm(p);
        p->state = RUNNING;

        swtch(&(c->scheduler), p->t_node->thread->context);
        switchkvm();

        if (p->state == RUNNABLE || p->state == SLEEPING)
        {
          if (p->p_node.level == LEVEL_MLFQ_0)
          {
            queue_push(&mlfq_0,&(p->p_node));
          }
          else if (p->p_node.level == LEVEL_MLFQ_1)
          {
            queue_push(&mlfq_1,&(p->p_node));
          }
          else if (p->p_node.level == LEVEL_MLFQ_2)
          {
            queue_push(&mlfq_2,&(p->p_node));
          }
          else if (p->p_node.level == LEVEL_STRIDE)
          {
            p->p_node.turnCount++; // since set_cpu_share() called, turnCount is reset to 0.
            p->p_node.distance = p->p_node.turnCount * 100 / p->p_node.share;
            queue_push(&stride,&(p->p_node));
          }
          else
          {
            panic("process level undefined!\n");
          }
        }
        
        c->proc = 0;

        if (mlfq_tickCount >= 200)
        {
          mlfq_tickCount = 0;
          priority_boost();
        }
        
      }
      
    }
    else if (node->level == LEVEL_STRIDE)
    {
      // stride node execution code 
      if (node->proc->state == SLEEPING)
      {
        node->turnCount++;
        node->distance = node->turnCount * 100 / node->share;
        queue_push(&stride, node);
        release(&ptable.lock);
        continue;
      }

      node->turnCount++;
      node->distance = node->turnCount * 100 / node->share;

      p = node->proc;
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;
      swtch(&(c->scheduler), p->t_node->thread->context);
      switchkvm();

      if (p->state == RUNNABLE || p->state == SLEEPING)
      {
        queue_push(&stride, &(p->p_node));
      }

      c->proc = 0;
    }
    else
    {
      panic("level of popped node from stride is not LEVEL_STRIDE!\n");
    }
    
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
  swtch(&mythread()->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

void
tsched(void)
{
  struct proc *curproc = myproc();
  struct thread *curthread = mythread();
  struct q_node *newt_node, *oldt_node;

  
  if(curproc->nrt < 0){
    panic("nrt less than 1");
  }
  else if (curproc->nrt == 1 && curthread->tstate == RUNNABLE) //if nrt is 1, does not need to switch thread
  {
    return;
  }
  
  oldt_node = curproc->t_node;
  newt_node = queue_pop(&curproc->tl);
  while(newt_node->thread->tstate != RUNNABLE)
  {
    queue_push(&curproc->tl, newt_node);
    newt_node = queue_pop(&curproc->tl);
  }

  if(oldt_node->thread->tstate != ZOMBIE)
    queue_push(&curproc->tl, oldt_node);
  curproc->t_node = newt_node;
  mycpu()->ts.esp0 = (uint)newt_node->thread->kstack + KSTACKSIZE;
  swtch(&oldt_node->thread->context, newt_node->thread->context);
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
tyield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  tsched();
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
  struct thread *t = mythread();
  
  if(p == 0)
    panic("sleep");
  
  if(t == 0)
    panic("sleep t");

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
  t->chan = chan;
  t->tstate = SLEEPING;
  p->nrt--;
  
  if(p->nrt > 0)
    goto threadsched;
  
  p->state = SLEEPING;
  sched();
  if(t->tstate == SLEEPING)
    goto threadsched;

  // Tidy up.
  t->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
  return;

threadsched:
  
  tsched();

  t->chan = 0;
  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
  return;
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct thread *t;
  for(t = ptable.thread; t < &ptable.thread[NTHREAD]; t++)
  {
    if(t->tstate == SLEEPING && t->chan == chan)
    {
      t->tstate = RUNNABLE;
      t->master->nrt++;
      if(t->master->state == SLEEPING)
        t->master->state = RUNNABLE;
    }
  }
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
      if(p->nrt == 0)
      {
        p->nrt++;
        p->t_node->thread->tstate = RUNNABLE;
      }
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
      getcallerpcs((uint*)p->t_node->thread->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

int
set_cpu_share(int share)
{
  acquire(&ptable.lock);
  if( shareleft < share || share <= 0) {
    release(&ptable.lock);
    return -1;
  }
  struct proc *curproc = myproc();
  struct proc *temp;
  
  curproc->p_node.level = -1;
  shareleft = shareleft - share;
  curproc->p_node.share = share;
  
  // reset distance of all stride processes
  for (temp = ptable.proc; temp < &ptable.proc[NPROC]; temp++)
  {
    if (temp->p_node.level == LEVEL_STRIDE)
    {
      temp->p_node.turnCount = 0;
      temp->p_node.distance = 0;
    }
  }
  mlfq_as_proc.turnCount = 0;
  mlfq_as_proc.distance = 0;
  
  release(&ptable.lock);
  return 0;
}

int 
queue_push(struct q_header* header, struct q_node* node)
{
	if(header->next == 0) {
		header->next = node;
	} 
  else if(header->type == QUEUE_STRIDE){
		struct q_node* temp = header->next;
		if(temp->distance > node->distance) {
			node->next = temp;
			header->next = node;
		}else {
			while( temp->next != 0 ) {
				if(temp->next->distance <= node->distance) {
					temp = temp->next;
				} else break;
			}
			node->next = temp->next;
			temp->next = node;
		}
	} 
  else if(header->type == QUEUE_MLFQ){
		struct q_node* temp = header->next;
		while( temp->next != 0 ) {
			temp = temp->next;		
		}
		temp->next = node;
		node->next = 0;
	}
	return 0;
}

struct q_node* 
queue_pop(struct q_header* header)
{
	struct q_node* temp = 0;
  temp = header->next;
  if(temp != 0){ 
    header->next = temp->next;
    temp->next = 0;
  }
	
	return temp;
}

struct q_node* 
queue_popall(struct q_header* header)
{
	struct q_node* temp = header->next;
	header->next = 0;
	return temp;
}

int 
queue_pushall(struct q_header* header, struct q_node* frontNode)
{
	struct q_node* temp = header->next;
	if(temp == 0){
		header->next = frontNode;
	} else {
		while(temp->next != 0){
			temp = temp->next;
		}
		temp->next = frontNode;
	}
	return 1;
}

int 
queue_hasRunnable(struct q_header* header)
{
  int hasRunnable = 0;
  struct q_node *temp = header->next;
  while (temp != 0)
  {
    if (temp->proc->state == RUNNABLE)
    {
      hasRunnable++;
      break;
    }
    temp = temp->next;
  }
  
  return hasRunnable;
}

int queue_findPid(struct q_header* header, int p)
{
	struct q_node* temp = header->next;
	while(temp != 0){
		if(temp->proc->pid == p){
			return 1;
		}
	}
	return 0;
}

int
thread_create(thread_t* thread, void* (*start_routine)(void *), void* arg)
{
  struct thread *t, *curthread;
  struct proc *curproc;
  char *sp;
  uint sz, nsp;
  pde_t *pgdir;

  acquire(&ptable.lock);
  curproc = myproc();
  curthread = mythread();

  for(t = ptable.thread; t < &ptable.thread[NTHREAD]; t++)
    if(t->tstate == UNUSED)
      goto tfound;

  cprintf("failed to create thread\n");
  release(&ptable.lock);
  return -1;

tfound:
  t->tstate = EMBRYO;
  t->tid = nexttid++;
  t->t_node.thread = t;
  t->t_node.next = 0;
  t->chan = 0;
  t->master = curproc;
  release(&ptable.lock);

  if((t->kstack = kalloc()) == 0){
    t->tstate = UNUSED;
    t->master = 0;
    t->tid = 0;
    return -1;
  }

  sp = t->kstack + KSTACKSIZE;

  sp -= sizeof *t->tf;
  t->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *t->context;
  t->context = (struct context*)sp;
  memset(t->context, 0, sizeof *t->context);
  t->context->eip = (uint)forkret;


  acquire(&ptable.lock);
  if(curproc->bl.cnt == 0)
  {
    sz = curproc->sz;
    curproc->sz += 2*PGSIZE;
  }
  else
  {
    curproc->bl.cnt--;
    sz = curproc->bl.blanklist[curproc->bl.cnt];
  }

  pgdir = curproc->pgdir;
  t->tsb = sz;
  if((sz = allocuvm(pgdir, sz, sz + 2*PGSIZE)) == 0)
    goto bad;

  release(&ptable.lock);

  nsp = sz - 4;
  *(uint*)nsp = (uint)arg;
  nsp -= 4;
  *(uint*)nsp = 0xffffffff;  // fake return PC - should not pop from stack

  *t->tf = *curthread->tf;
  t->tf->esp = nsp;
  t->tf->eip = (uint)start_routine;

  *thread = t->tid;

  acquire(&ptable.lock);
  curproc->nrt++;
  curproc->nt++;
  t->tstate = RUNNABLE;
  queue_push(&curproc->tl, &t->t_node);
  release(&ptable.lock);
  return 0;

bad:
  t->tstate = UNUSED;
  kfree(t->kstack);
  t->kstack = 0;
  t->master = 0;
  t->tid = 0;
  cprintf("bad!\n");
  release(&ptable.lock);
  return -1;
}


void 
thread_exit(void* retval)
{
  struct proc *curproc;
  struct thread *curthread;
  acquire(&ptable.lock);
  curproc = myproc();
  curthread = mythread();

  curproc->nt--;
  curproc->nrt--;
  if(curproc->nt < 1)  // this was last thread...
  {
    cprintf("last thread called thread_exit\n");
    release(&ptable.lock);
    exit();
  }

  curthread->ret_val = retval;
  curthread->tstate = ZOMBIE;

  wakeup1(curproc);

  while(curproc->nrt < 1)
  {
    curproc->state = RUNNABLE;
    sched();
  }
  tsched();
  panic("zombie thread exit");
}


int
thread_join(thread_t thread, void** retval)
{
  struct thread *t;
  struct proc *curproc = myproc();
  int havethreads = 0;
  
  acquire(&ptable.lock);

  for(;;){
    havethreads = 0;
    for(t = ptable.thread; t < &ptable.thread[NTHREAD]; t++){
      if(t->tid != thread)
        continue;
      havethreads = 1;
      if(t->tstate == ZOMBIE){
        *retval = t->ret_val;
        kfree(t->kstack);
        t->kstack = 0;
        t->tstate = UNUSED;
        t->master->bl.blanklist[t->master->bl.cnt] = t->tsb;
        t->master->bl.cnt++;
        deallocuvm(t->master->pgdir, t->tsb + 2*PGSIZE, t->tsb);
        t->master = 0;
        release(&ptable.lock);
        return 0;
      }
      
    }

    // No point waiting if we don't have any children.
    if(!havethreads || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }

  release(&ptable.lock);

  return 0;
}



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
  p->tid = nexttid++;

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

  // Set up node for scheduling queue
  p->p_node.proc = p;
  p->p_node.next = 0;
  p->p_node.share = 0;
  p->p_node.turnCount = 0;
  p->p_node.distance = 0;
  p->p_node.level = 0;
  p->master = p;
  p->nthread = 1;

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
  
  // Push to mlfq_0 when state changed to RUNNABLE
  queue_push(&mlfq_0, &(np->p_node));
  // cprintf("forked! %d\n",np->p_node.proc->pid);

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
    if (stride.next->distance > 199 ) // means every node in stride has about 200 - at least 2 loops
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
      mlfq_as_proc.distance = mlfq_as_proc.turnCount * 100 / mlfq_as_proc.share;
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
          mlfq_tickCount++;
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
          mlfq_tickCount += 2;
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
          mlfq_tickCount += 4;
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

        swtch(&(c->scheduler), p->context);
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

        if (mlfq_tickCount >= 100)
        {
          mlfq_tickCount = 0;
          priority_boost();
        }
        
      }
      
    }
    else if (node->level == LEVEL_STRIDE)
    {
      // TODO: stride node execution code here
      if (node->proc->state == SLEEPING)
      {
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
      swtch(&(c->scheduler), p->context);
      switchkvm();

      if (p->state == RUNNABLE || p->state == SLEEPING)
      {
        // if set_cpu_share(0) called, process goes to MLFQ_0.
        if (p->p_node.level == LEVEL_MLFQ_0)
        {
          queue_push(&mlfq_0,&(p->p_node));
        } // else, process remains in STRIDE.
        else if (p->p_node.level == LEVEL_STRIDE)
        {
          queue_push(&stride, &(p->p_node));
        }
        
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

int
set_cpu_share(int share)
{
  acquire(&ptable.lock);
  struct proc *curproc = myproc();
  struct proc *temp;
  if (share < 0)
  {
    release(&ptable.lock);
    return -1;
  }
  
  if(curproc->p_node.level >= LEVEL_MLFQ_0)
  {
    if( shareleft < share || share == 0) {
      release(&ptable.lock);
      return -1;
    }
    curproc->p_node.level = LEVEL_STRIDE;
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
  else if(curproc->p_node.level == LEVEL_STRIDE)
  {
    if (share == 0)
    {
      shareleft = shareleft + curproc->p_node.share;
      curproc->p_node.share = 0;
      curproc->p_node.level = LEVEL_MLFQ_0;
      curproc->p_node.turnCount = 0;
      curproc->p_node.distance = 0;
      release(&ptable.lock);
      return 0;
    }
    else if (shareleft + curproc->p_node.share >= share)
    {
      shareleft = shareleft + curproc->p_node.share - share;
      curproc->p_node.share = share;
      release(&ptable.lock);
      return 0;
    }
    else
    {
      release(&ptable.lock);
      return -1;
    }
  }
  else
  {
    panic("set_cpu_share: not matching level!");
  }
  
  return -1;
}

int 
queue_push(struct q_header* header, struct q_node* node)
{
	//printf("push called\n");
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
  struct proc *nt;
  struct proc *curmaster = myproc()->master;
  pde_t *pgdir;
  uint sz, sp;
  int i;

  // Allocate process.
  if((nt = allocproc()) == 0){
    cprintf("cannot alloc thread!!!\n\n\n");
    return -1;
  }
  nextpid--;

  acquire(&ptable.lock);

  if (curmaster->bl.cnt == 0)
  { 
    sz = curmaster->sz;
    curmaster->sz +=  2*PGSIZE;
  }
  else
  {
    curmaster->bl.cnt--;
    sz = curmaster->bl.blanklist[curmaster->bl.cnt];
  }
  
  pgdir = curmaster->pgdir;
  nt->tsb = sz; // save in tsb to deallocate easily
  if((sz = allocuvm(pgdir, sz, sz + 2*PGSIZE)) == 0)
    goto bad;

  release(&ptable.lock);

  for(i = 0; i < NOFILE; i++)
    if(curmaster->ofile[i])
      nt->ofile[i] = curmaster->ofile[i];
  nt->cwd = curmaster->cwd;  


  safestrcpy(nt->name, curmaster->name, sizeof(curmaster->name));
  
  sp = sz - 4;
  *(uint*)sp = (uint)arg;
  sp -= 4;
  *(uint*)sp = 0xffffffff;  // fake return PC - should not pop from stack

  nt->sz = sz;
  nt->pgdir = pgdir;
  *nt->tf = *curmaster->tf;
  nt->tf->esp = sp;
  nt->tf->eip = (uint)start_routine;
  nt->pid = curmaster->pid;
  nt->master = curmaster;
  
  *thread = nt->tid;

  acquire(&ptable.lock);
  curmaster->nthread++;
  nt->state = RUNNABLE;
  queue_push(&mlfq_0, &(nt->p_node));
  //TODO: time share between LWPs
  release(&ptable.lock);
  return 0;

bad:
  nt->state = UNUSED;
  cprintf("bad!\n");
  release(&ptable.lock);
  return -1;
}

void 
thread_exit(void* retval)
{
  struct proc *curthread = myproc();
  struct proc *temp;
  int fd, hasslavenotend;
  
  if(curthread == initproc)
    panic("init exiting");

  acquire(&ptable.lock);
  if (curthread->master == curthread) //if master thread called thread_exit
    goto master;
  release(&ptable.lock);

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curthread->ofile[fd]){
      curthread->ofile[fd] = 0;
    }
  }

  curthread->cwd = 0;
  curthread->master->nthread--;

  acquire(&ptable.lock);

  curthread->ret_val = retval;
  
  // Jump into the scheduler, never to return.
  curthread->state = ZOMBIE;
  // Parent might be sleeping in wait().
  wakeup1(curthread->master);
  
  sched();
  panic("zombie exit");

master:
  hasslavenotend = 0;
  for(temp = ptable.proc; temp < &ptable.proc[NPROC]; temp++){
    if(temp->master == curthread && temp != curthread){
      if (temp->state == ZOMBIE)
      {
        kfree(temp->kstack);
        temp->kstack = 0;
        temp->state = UNUSED;
        temp->cwd = 0;
        temp->master->bl.blanklist[temp->master->bl.cnt] = temp->tsb;
        temp->master->bl.cnt++;
        deallocuvm(temp->pgdir, temp->sz, temp->tsb);
        temp->master = 0;
        curthread->nthread--;
      }
      else
      {
        hasslavenotend = 1;
        // cprintf("slave %d not end yet\n", temp->tid);
      }
    }
  }

  if (hasslavenotend)
  {
    sleep(curthread, &ptable.lock);
    goto master;
  }
  release(&ptable.lock);
  return;
}

int
thread_join(thread_t thread, void** retval)
{
  struct proc *p;
  struct proc *curproc = myproc();
  int havethreads = 0;
  
  acquire(&ptable.lock);

  if (curproc != curproc->master) //if join called by slave
  {
    release(&ptable.lock);
    return -1;
  }
  

  for(;;){
    havethreads = 0;
    // Scan through table looking for exited children.
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->tid != thread)
        continue;
      havethreads = 1;
      if(p->state == ZOMBIE){
        *retval = p->ret_val;
        kfree(p->kstack);
        p->kstack = 0;
        p->state = UNUSED;
        p->cwd = 0;
        p->master->bl.blanklist[p->master->bl.cnt] = p->tsb;
        p->master->bl.cnt++;
        deallocuvm(p->pgdir, p->sz, p->tsb);
        p->master = 0;
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
}
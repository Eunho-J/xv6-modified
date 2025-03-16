# Design Document

## LWP

### Basic implementing concepts

 As every LWP running rules are tied with LWP Group, it will be easy to manage thread when threads are tied to list in their process. But as we cannot know how many thread will be created in a process, "Linked List" will be a good option for list implementation. In this case, we can reuse "Linked list queue" algorithm implemented for the MLFQ & Stride scheduler.

 When scheduling, scheduler should be able to know if the process has threads, and if so, how many. 

 Variable `proc->tc` notifies the number of LWPs for the process. For default, it's value is `1`. `thread_create()` increases `tc` while `thread_exit()` decreases `tc`. (when `tc < 1`, scheduler throws panic)

 For the stability of the system, the maximum number of thread(LWP)s in the entire system is `NTHREAD`(defined in `proc.h`).

### Interaction with other services in xv6

#### Interaction with system calls

 Interaction with system calls can be distinguished by ***'per process'*** and ***'per thread'*** depending on the extent of the effect of the system call.

- **per process**
  - `sys_exit()` : terminates the process after terminating every LWPs in the process(means 'in the same LWP group').
  - `sys_fork()` : copy the process into the child process including LWPs.
  - `sys_exec()` : terminate every LWPs in the process and load new program executed by the exec system call.
  - `sys_sbrk()` : extend the process memory area. Extended area will be shared with LWPs in the process.
  - `sys_kill()` : terminate every LWPs in the process and kill the process. As `sys_kill()` is per process system call, `killed` variable needs to be remain in the `proc` structure, not `thread` structure.
- **per thread**
  - Basic Operations(create, join, exit) : These operations are managing threads.
  - `sys_sleep()` : Only the requested thread sleeps for the requested time.

#### Interaction with scheduler(MLFQ & Stride)

​	For interaction with MLFQ & Stride scheduler, we need to optimize scheduler. This will be dealt with in detail in the next paragraph.

## Scheduler Optimization

 To optimize scheduler for LWP running, 'time allotment' and 'time quantum' needs to be rearranged as shown below.

#### MLFQ

- Time quantum
  - Highest priority queue: `5 ticks`
  - Middle priority queue: `10 ticks`
  - Lowest priority queue: `20 ticks`
- Time allotment(Turn allotment)
  - Highest priority queue: `20 ticks(4 turns)`
  - Middle priority queue: `40 ticks(4 turns)`
- Priority boost period
  - Once a `200 ticks`

#### Stride

- Time quantum: `5 ticks`


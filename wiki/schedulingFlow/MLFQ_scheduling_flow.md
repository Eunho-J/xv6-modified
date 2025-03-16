# Basic Flow of MLFQ

### execution rules for process & leveled queues

- process (working on MLFQ scheduler) : takes **1 execution per turn**, and queue level increases after taking **5 turns** *(5 ticks for level 0, 10 ticks for level 1)* in the queue.

- level 0 (highest priority) : takes **1 tick** per execution

- level 1 (middle priority) : takes **2 ticks** per execution

- level 2 (lowest priority) : takes **4 ticks** per execution

***

### priority boost

- priority boost occurs **once a 100 ticks**.

- check `mlfq_tickCount >= 100`

- priority boost works like this :

  1. `pushall(highest_queue, popall(middle_queue))` : push all processes in the middle queue to highest queue.
  2. `pushall(middle_queue, popall(lowest_queue))` : push all processes in the lowest queue to middle queue.
  3. `reset_tickCount()` : reset `tickCount` of MLFQ scheduler(`MLFQ_tickCount`) & every processes in the MLFQ scheduler(`proc->tickCount`).

  - this works as `level -= 1` to all processes in the middle & lowest priority queue.

***

## Details about Processes & Trap

#### Trap

- for every timer interrupt: (this works only if it is MLFQ's turn in Stride scheduling)

  - increases `uint MLFQ_tickCount` and `proc->tickCount`
  - check `ticks_checker` and determine whether process to call `yield()` or keep execution.

#### Queue

- size of queue: `NPROC`

- pop from queue & execute.
- pop() increases `turnCount` of popped process.
- after execution of a process (when scheduler calls `yield()` ),
  - if `proc->turnCount < 5 && queue->level < 2` : push() to popped queue & set `proc->turnCount +=1`.
  - if `proc->turnCount >= 5 && queue->level < 2` : push() to 1 level lower queue & set `proc->turnCount = 0`.
  - if `queue->level = 2` : push() to popped queue. *(do not increase `turnCount` to have less overhead)*

#### Process

- has `uint ticks_checker` variable for checking ticks taken for the process running (is defined in trap.c)

- has `uint turnCount` variable for checking turns taken by the process (this will be used for Stride with different purpose)
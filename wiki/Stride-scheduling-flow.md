# Basic Flow of Stride algorithm

### remaining tickets & stride concepts

- for user request of CPU share, system allocates requested amount of tickets from `int shareleft` to `proc->share`. (if there are not enough remaining tickets, `proc->tickets = 0` and it means the process will be scheduled with MLFQ)
- maximum value of `remaining_tickets` is 80 to assure that MLFQ owns 20 tickets, and this will assure that MLFQ owns more than 20% portion of CPU. (this will be handled in details at [Combined scheduling flow](./Flow of Combined Scheduler))![](https://github.com/Eunho-J/xv6-modified/wiki/schedulingFlow/res/stride_alocateTickets.jpg)
- stride is inversely proportional to owning share.
- number of available tickets is same as `100 - shareleft`.
- stride of process is same as `100 / proc->share`.

***

### time slices

- every processes in the Stride scheduler(as MLFQ scheduler will be a process in Stride scheduler, yes it will be included in 'the' processes. this will be handled in details at [Combined scheduling flow](./Flow of Combined Scheduler)) will run for **4 ticks** per one step(turn).

***

## Details about Processes & Scheduler

### Trap

- if `proc->tickets` is 0, the process will be considered as MLFQ process and scheduled in MLFQ queues.
- has `uint ticks_checker` 
- for every timer interrupt:
  - increases `uint ticks`
  - determine whether turnover process or not by comparing `ticks - ticks_checker >= 4`. If turnover occurs, at the point that process turn comes back, `ticks_checker` is set to current `ticks`.

### Queue (sorted)

- pop from queue & execute.
- pop increases `turnCount` of popped process. (`proc->turnCount`)
- after execution of a process (when scheduler calls `yield()`), push() to the Stride queue.
- `push(proc)` will push process to the queue in the increasing order with comparing 'total distance' with processes in the queue. This will assure that the queue is sorted.

### Process

- has `uint turnCount` variable for checking turns taken by the process. (this will be used for MLFQ with different purpose)
- calculate 'total distance' from `proc->turnCount` & stride of process : `proc->turnCount * 100 / proc->tickets` (maximum remaining tickets is 80, but as MLFQ always have 20 share, total system tickets are 100)

***

## Further Considerations

- guarantee share for `yield()`.
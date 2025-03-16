# Interfaces

#### `int sys_yield(void)`

​	This systemcall will make process to giveup its current turn. On MLFQ, rest time of the turn will be vanished. On Stride, exhausted time will be measured with ticks as units(heaving) to guaruntee share at the most.

***

#### `int sys_getlev(void)`

​	This systemcall will look for the process in the Stride first. After, look for each queue in the MLFQ. It returns **`queue->level`** if the process has found in the MLFQ queues, but returns **`4`** if the process has found in the Stride queue. Not found cannot be happen, but if failed to find, returns **`-1`**.

***

#### `int set_cpu_share(int)`

​	Requests share to the Stride scheduler. If `remaining_tickets` is bigger than request, pop from the MLFQ queue, push the process into the Stride queue with distributing requested amounts of tickets, and returns guaranteed share amount. If not, it returns 0, and let the process remains in the MLFQ scheduling.

#### `int sys_set_cpu_share(void)`

​	This systemcall is a wrapper of the `int set_cpu_share(int)`.

***
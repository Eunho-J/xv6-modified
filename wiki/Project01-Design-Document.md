# __MLFQ__ : _Multi-Level Feedback Queue_
- 3-level feedback queue
- Each level of queue adopts _Round Robin policy(RR policy)_ with different time quantum
- Each queue has different time allotment
- To prevent starvation, priority boost is required

see more about MLFQ at [here](./schedulingFlow/MLFQ scheduling flow).

# __Stride__ : execution considered as step

- Guarantees portion of CPU to processes.
- Process make a request on their own.
- In this project, Stride will be used with MLFQ **simultaneously**.

see more about Stride at [here](./schedulingFlow/Stride scheduling flow).

# __Combination of _Stride Scheduling_ algorithm with MLFQ__
- System call that requests the portion of CPU
>```C
>int sys_cpu_share(int portion)
>```
>__sys_cpu_share__ returns ___portion___ when it succeeded to get requested portion of CPU time
- Total stride processes are able to get at most 80% of CPU time.
>For __exceeding request__, portion will not be guaranteed and process will run on MLFQ, so system call sys_cpu_share(portion) ***returns 0***.
- The rest 20% of CPU time should run for the MLFQ scheduling which is the default scheduling in this project

see basic concept idea about Combining MLFQ and Stride [here](./schedulingFlow/Flow of Combined Scheduler).
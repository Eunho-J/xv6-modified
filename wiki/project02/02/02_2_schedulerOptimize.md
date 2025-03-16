# Scheduler Optimization

LWP의 scheduling 방식은 Process가 scheduling 되었을 때 그 time quantum 안에서 Round Robin으로 scheduling  되기 때문에 기존의 MLFQ & Stride Scheduler의 코드부 변경은 time allotment와 time quantum, 그리고 priority boost 주기를 제외하고는 할 필요 자체가 없었다. 이는 thread를 따로 구현하여 proc에 속하게 하는 설계 덕분이기도 했다.*(error fix 제외)*



다만 Thread간의 RR Scheduling을 위해, `trap()`에서 timer interrupt 발생시 `tyield()`를 호출하여 Thread Switching이 발생하도록 하였다. 실행된 thread는 `tsched()`에 의해 다시 inqueue되므로 `sleep`을 제외하면 균등하게 Round Robin 방식으로 차례가 바뀌게 된다.

![](https://github.com/Eunho-J/xv6-modified/wiki/project02/02/res/scheduling.jpg)




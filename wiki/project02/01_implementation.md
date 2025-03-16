# Implementation

### Process & Thread

Process is an unit of task. Process can divide task into small pieces called thread. As process is an unit of task, each processes have individual spaces and resources, which cannot be disturbed by others. Threads, in the other hand, share space and resources in the process where they belong except having individual stacks.



###### Context Switch

Process context switch replaces the entire virtual memory and registers, while the thread switch replaces only the stack and registers.



### POSIX Thread

```c
int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine)(void *), void * arg);
```
Creates new thread. Created thread ID is stored in `pthread_t *thread`. After creation, the thread run function `start_routine` with arguments `arg`. When `start_routine` returns, `pthread_exit()` will be called internally.

```C
int pthread_join(pthread_t thread, void **ret_val);
```
Wait for the exit of thread `pthread_t thread`.  After exit of the `thread`, `ret_val` is filled with the value that `pthread_exit()` delivered.

```c
void pthread_exit(void *ret_val);
```
Terminate the thread and pass `ret_val` to another thread(or process) waiting for the exit of the thread.



### Thread in xv6

```c
int thread_create(thread_t * thread, void * (*start_routine)(vo
id *), void *arg);
```

- `thread` : return the thread id.

- `start_routine` : the pointer to the function to be threaded. The function has
  a single argument: pointer to void.
- `arg` : the pointer to an argument for the function to be threaded. To pass
  multiple arguments, send a pointer to a structure.
- `return` : On success, thread_create returns 0. On error, it returns a non-zero
  value.

```c
void thread_exit(void *retval);
```

- `retval` : Return value of the thread.

```c
int thread_join(thread_t thread, void **retval);
```

- `thread` : the thread id allocated on thread_create.
- `retval` : the pointer for a return value.
- `return` : On success, this function returns 0. On error, it returns a non-zero
  value.


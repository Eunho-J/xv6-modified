#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

int
getppid(void)
{
	return myproc()->parent->pid;
}

//Wrapper of getppid
int
sys_getppid(void)
{
	return getppid();
}

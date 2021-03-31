#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
	int loopCount = 0;
	int childPid = fork();

	if(childPid == 0) //Child process
	{
		while(loopCount++ < 100)
		{
			yield();
			printf(1,"Child\n");
		}
		exit();
	} 
	else if (childPid > 0) //Parent process
	{
		while(loopCount++ < 100)
		{
			yield();
			printf(1,"Parent\n");
		}
		wait();
	} else
	{
		printf(1,"fork error!\n");
	}
	exit();
}

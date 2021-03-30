#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
	int childPid = fork();

	int loopCount = 0;

	if(childPid == 0) //Child process
	{
		while(loopCount++ < 100)
		{
			printf(1,"Child\n");
			yield();
		}
		exit();
	} 
	else if (childPid > 0) //Parent process
	{
		while(loopCount++ < 100)
		{
			printf(1,"Parent\n");
			yield();
		}
		wait();
	} else
	{
		printf(1,"fork error!\n");
	}
	exit();
}

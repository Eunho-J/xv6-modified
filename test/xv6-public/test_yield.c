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
		// int temp=set_cpu_share(20);
		// if(temp == 0){
			// printf(1,"set cpu share!\n");
			while(loopCount++ < 20)
			{
				printf(1,"Child %d\n", loopCount);
				yield();
			}
		// }
		exit();
	} 
	else if (childPid > 0) //Parent process
	{
		while(loopCount++ < 20)
		{
			printf(1,"Parent %d\n",loopCount);
			yield();
		}
		wait();
	} else
	{
		printf(1,"fork error!\n");
	}
	exit();
}

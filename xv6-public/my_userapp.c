#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
	char *buf = "Hello xv6!";
	int ret_val;
	int pid, ppid;
	ret_val = myfunction(buf);
	printf(1, "Return value : 0x%x\n", ret_val);
	pid = getpid();
	ppid = getppid();
	printf(1, "My pid is %d\nMy ppid is %d\n", pid, ppid);

	exit();
}

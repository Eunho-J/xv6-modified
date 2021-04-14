#define QUEUE_MLFQ			0
#define QUEUE_STRIDE		1
#define IS_MLFQ_IN_STRIDE	-1

struct q_node {
	int pid;
	int isRunnable;
	uint tickets;
	uint turnCount;
	uint tickCount;
	uint distance;
	struct q_node* next;
};

struct q_header {
	int type;
	struct q_node* next;
};

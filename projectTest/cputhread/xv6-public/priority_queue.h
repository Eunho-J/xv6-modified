#define QUEUE_MLFQ			0
#define QUEUE_STRIDE		1
#define IS_MLFQ_IN_STRIDE	-1

struct q_node {
	int wasRunnable;
	int share;
	int level;
	uint turnCount;
	uint tickCount;
	uint distance;
	struct proc* proc;
	struct q_node* next;
};

struct q_header {
	int type;
	int level;
	struct q_node* next;
};

#define QUEUE_MLFQ		0
#define QUEUE_STRIDE	1

struct q_node {
	int procIndex;
	uint distance;
	struct q_node* next;
};

struct q_header {
	int type;
	struct q_node* next;
};

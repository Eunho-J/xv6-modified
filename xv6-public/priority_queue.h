#define QUEUE_MLFQ		0
#define QUEUE_STRIDE	1
#define IS_MLFQSCHED	1

struct q_node {
	struct proc* p;
	uint distance;
	int isMLFQ;
	struct q_node* next;
};

struct q_header {
	int type;
	struct q_node* next;
};

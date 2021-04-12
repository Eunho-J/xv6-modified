#define QUEUE_MLFQ	0
#define QUEUE_STRIDE	1

typedef struct node {
	int procIndex;
	unsigned int distance;
	struct node* next;
} Node;

typedef struct header {
	int type;
	struct node* next;
} Header;

#include "types.h"
#include "defs.h"
#include "proc.h"
#include "priority_queue.h"

struct q_node* queue_newNode(int index, uint d)
{
	struct q_node* temp;
	temp->procIndex = index;
	temp->distance = d;
	temp->next = 0;
	return temp;
}

struct q_header* queue_newHeader(int t)
{
	struct q_header* temp;
	temp->type = t;
	temp->next = 0;
	return temp;
}

int queue_push(struct q_header** header, struct q_node** node)
{
	//printf("push called\n");
	if((*header)->next == 0) {
		(*node)->next = (*header)->next;
		(*header)->next = (*node);
	} else if((*header)->type == QUEUE_STRIDE){
		struct q_node* temp = (*header)->next;
		if(temp->distance > (*node)->distance) {
			(*node)->next = (*header)->next;
			(*header)->next = (*node);
		}else {
			while( temp->next != 0 ) {
				if(temp->next->distance <= (*node)->distance) {
					temp = temp->next;
				} else break;
			}
			(*node)->next = temp->next;
			temp->next = (*node);
		}
	} else if((*header)->type == QUEUE_MLFQ){
		struct q_node* temp = (*header)->next;
		while( temp->next != 0 ) {
			temp = temp->next;		
		}
		(*node)->next = temp->next;
		temp->next = (*node);
	}
	return 0;
}

struct q_node* queue_pop(struct q_header** header)
{
	//printf("pop called\n");
	struct q_node* temp = (*header)->next;
	//printf("pop called 2\n");
	if((*header)->next != 0){ 
		(*header)->next = (*header)->next->next;
		//printf("pop called 3\n");
	} //else printf("pop called 4\n");
	return temp;
}

struct q_node* queue_popall(struct q_header** header)
{
	struct q_node* temp = (*header)->next;
	(*header)->next = 0;
	return temp;
}

int queue_pushall(struct q_header** header, struct q_node** frontNode)
{
	struct q_node* temp = (*header)->next;
	if(temp == 0){
		(*header)->next = (*frontNode);
	} else {
		while(temp->next != 0){
			temp = temp->next;
		}
		temp->next = (*frontNode);
	}
	return 1;
}

int queue_isEmpty(struct q_header** header)
{
	//printf("isEmpty called\n");
	return (*header)->next == 0;
}

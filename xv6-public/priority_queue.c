#include "types.h"
#include "defs.h"
#include "priority_queue.h"
#include "param.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"

struct q_node* queue_newNode(struct proc* proc, int ismlfq)
{
	struct q_node* temp = (struct q_node*)kalloc();
	temp->p = proc;
	temp->isMLFQ = ismlfq;
	temp->distance = 0;
	temp->next = 0;
	return temp;
}

struct q_header* queue_newHeader(int t)
{
	struct q_header* temp = (struct q_header*)kalloc();
	temp->type = t;
	temp->next = 0;
	return temp;
}

void queue_freeNode(struct q_node** node)
{
	(*node)->p = 0;
	kfree((char*)(*node));
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
	if(temp != 0){ 
		(*header)->next = temp->next;
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

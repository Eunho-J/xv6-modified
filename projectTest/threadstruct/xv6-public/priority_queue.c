#include "types.h"
#include "defs.h"
#include "priority_queue.h"
#include "param.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"

int queue_push(struct q_header* header, struct q_node* node)
{
	//printf("push called\n");
	if(header->next == 0) {
		header->next = node;
	} else if(header->type == QUEUE_STRIDE){
		struct q_node* temp = header->next;
		if(temp->distance > node->distance) {
			node->next = temp;
			header->next = node;
		}else {
			while( temp->next != 0 ) {
				if(temp->next->distance <= node->distance) {
					temp = temp->next;
				} else break;
			}
			node->next = temp->next;
			temp->next = node;
		}
	} else if(header->type == QUEUE_MLFQ){
		struct q_node* temp = header->next;
		while( temp->next != 0 ) {
			temp = temp->next;		
		}
		temp->next = node;
		node->next = 0;
	}
	return 0;
}

struct q_node* queue_pop(struct q_header* header)
{
	struct q_node* temp = 0;
	if(!queue_isEmpty(header)){
		temp = header->next;
		if(temp != 0){ 
			header->next = temp->next;
			temp->next = 0;
		}
	}
	
	return temp;
}

struct q_node* queue_popall(struct q_header* header)
{
	struct q_node* temp = header->next;
	header->next = 0;
	return temp;
}

int queue_pushall(struct q_header* header, struct q_node* frontNode)
{
	struct q_node* temp = header->next;
	if(temp == 0){
		header->next = frontNode;
	} else {
		while(temp->next != 0){
			temp = temp->next;
		}
		temp->next = frontNode;
	}
	return 1;
}

int queue_isEmpty(struct q_header* header)
{
	//printf("isEmpty called\n");
	return header->next == 0;
}

int queue_resetTickCount(struct q_header* stride)
{
	struct q_node* temp = stride->next;
	while(temp != 0){
		temp->tickCount = 0;
		temp->distance = 0;
		temp->turnCount = 0;
		// cprintf("pid: %d / dis: %d / tC: %d\n", temp->pid, temp->distance, temp->tickCount);
		temp = temp->next;
	}
	return 1;
}

int queue_findPid(struct q_header* header, int p)
{
	struct q_node* temp = header->next;
	while(temp != 0){
		if(temp->proc->pid == p){
			return 1;
		}
	}
	return 0;
}
#include "types.h"
#include "defs.h"
#include "proc.h"
#include "priority_queue.h"

Node* queue_newNode(int index, unsigned int d)
{
	Node* temp = (Node*)malloc(sizeof(Node));
	temp->procIndex = index;
	temp->distance = d;
	temp->next = NULL;
	return temp;
}

Header* queue_newHeader(int t)
{
	Header* temp = (Header*)malloc(sizeof(Header));
	temp->type = t;
	temp->next = NULL;
	return temp;
}

int queue_push(Header** header, Node** node)
{
	//printf("push called\n");
	if((*header)->next == NULL) {
		(*node)->next = (*header)->next;
		(*header)->next = (*node);
	} else if((*header)->type == QUEUE_STRIDE){
		Node* temp = (*header)->next;
		if(temp->distance > (*node)->distance) {
			(*node)->next = (*header)->next;
			(*header)->next = (*node);
		}else {
			while( temp->next != NULL ) {
				if(temp->next->distance <= (*node)->distance) {
					temp = temp->next;
				} else break;
			}
			(*node)->next = temp->next;
			temp->next = (*node);
		}
	} else if((*header)->type == QUEUE_MLFQ){
		Node* temp = (*header)->next;
		while( temp->next != NULL ) {
			temp = temp->next;		
		}
		(*node)->next = temp->next;
		temp->next = (*node);
	}
	return 0;
}

Node* queue_pop(Header** header)
{
	//printf("pop called\n");
	Node* temp = (*header)->next;
	//printf("pop called 2\n");
	if((*header)->next != NULL){ 
		(*header)->next = (*header)->next->next;
		//printf("pop called 3\n");
	} //else printf("pop called 4\n");
	return temp;
}

Node* queue_popall(Header** header)
{
	Node* temp = (*header)->next;
	(*header)->next = NULL;
	return temp;
}

int queue_pushall(Header** header, Node** frontNode)
{
	Node* temp = (*header)->next;
	if(temp == NULL){
		(*header)->next = (*frontNode);
	} else {
		while(temp->next != NULL){
			temp = temp->next;
		}
		temp->next = (*frontNode);
	}
	return 1;
}

int queue_isEmpty(Header** header)
{
	//printf("isEmpty called\n");
	return (*header)->next == NULL;
}

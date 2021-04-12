#include "types.h"
#include "defs.h"
#include "proc.h"
#include "priority_queue.h"

Node* newNode(int index, uint p)
{
	Node* temp = (Node*)malloc(sizeof(Node));
	temp->procIndex = index;
	temp->priority = p;
	temp->next = NULL;

	return temp;
}

int push(Header* header, Node* node)
{
	if(header->next == NULL)
		header->next = node;

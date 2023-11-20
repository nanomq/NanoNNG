#include "nng/supplemental/nanolib/linkedlist.h"
#include "core/nng_impl.h"

int linkedList_replace_head(struct linkedList *list,
							void *data,
							unsigned long long expiredAt)
{
	if (list->size == 0) {
		log_error("linkedList_replace_head failed list is empty!\n");
		return -1;
	}

	free(list->head->data);

	list->head->data = data;
	list->head = list->head->next;
	list->tail = list->tail->next;

	return 0;
}

int linkedList_init(struct linkedList **list,
					unsigned int cap,
					unsigned int overWrite,
					unsigned long long expiredAt)
{
	struct linkedList *newList;

	newList = (struct linkedList *)nni_alloc(sizeof(struct linkedList));
	if (newList == NULL) {
		log_error("alloc new linkedList failed\n");
		return -1;
	}

	newList->cap = cap;
	newList->size = 0;
	newList->overWrite = overWrite;
	newList->expiredAt = expiredAt;
	newList->head = NULL;
	newList->tail = NULL;

	*list = newList;

	return 0;
}

int linkedList_enqueue(struct linkedList *list,
					   void *data,
					   unsigned long long expiredAt)
{
	if (list->size == list->cap) {
		if (list->overWrite == 0) {
			log_error("Linked list is full, and do not allow to overwrite!\n");
			return -1;
		} else {
			return linkedList_replace_head(list, data, expiredAt);
		}
	}

	struct linkedListNode *newNode = NULL;
	newNode = (struct linkedListNode *)nni_alloc(sizeof(struct linkedListNode));
	if (newNode == NULL) {
		log_error("Linked list alloc new node failed!\n");
		return -1;
	}
	newNode->data = data;
	newNode->expiredAt = expiredAt;

	if (list->head == NULL) {
		list->head = newNode;
		list->tail = newNode;
	}

	newNode->next = list->head;
	newNode->prev = list->tail;

	list->tail->next = newNode;
	list->head->prev = newNode;
	list->tail = newNode;
	list->size++;

	return 0;
}

int linkedList_dequeue(struct linkedList *list,
					   void **data)
{
	if (list->size == 0) {
		log_error("Linked list is empty, dequeue failed!\n");
		return -1;
	}
	struct linkedListNode *node = list->head;

	*data = node->data;

	struct linkedListNode *tmp;
	if (list->size == 1) {
		tmp = list->head;
		free(tmp);
		list->head = NULL;
		list->tail = NULL;
		list->size = 0;
		return 0;
	}

	tmp = list->head;

	list->tail->next = list->head->next;
	list->head->next->prev = list->tail;
	list->head = list->head->next;

	free(tmp);

	list->size--;

	return 0;
}

int linkedList_release(struct linkedList *list)
{
	int ret = 0;
	if (list == NULL) {
		return -1;
	}

	while (list->size != 0) {
		void *data;
		ret = linkedList_dequeue(list, &data);
		if (ret != 0) {
			log_error("linkedList_dequeue failed!\n");
			return -1;
		}
		free(data);
	}

	free(list);

	return 0;
}

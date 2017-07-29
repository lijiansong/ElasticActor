#ifndef _SD_QUEUE_H_
#define _SD_QUEUE_H_

#include <mola/framework.h>
#include <stdint.h>

typedef EVENT_T Item;

typedef struct node * PNode;
typedef struct node
{
  Item data;
  PNode next;
}Node;

typedef struct
{
  PNode front;
  PNode rear;
  int size;
}Queue;

static Queue *InitQueue();

static void DestroyQueue(Queue *pqueue);

static void ClearQueue(Queue *pqueue);

static int IsEmpty(Queue *pqueue);

static int GetSize(Queue *pqueue);

static PNode GetFront(Queue *pqueue,Item *pitem);

static PNode GetRear(Queue *pqueue,Item *pitem);

static PNode EnQueue(Queue *pqueue,Item item);

static PNode DeQueue(Queue *pqueue,Item *pitem);

static void QueueTraverse(Queue *pqueue,void (*visit)());

static Queue *InitQueue()
{
  Queue *pqueue = (Queue *)malloc(sizeof(Queue));
  if(pqueue!=NULL)
    {
      pqueue->front = NULL;
      pqueue->rear = NULL;
      pqueue->size = 0;
    }
  return pqueue;
}

static void DestroyQueue(Queue *pqueue)
{
  if(IsEmpty(pqueue)!=1)
    ClearQueue(pqueue);
  free(pqueue);
}

static void ClearQueue(Queue *pqueue)
{
  while(IsEmpty(pqueue)!=1)
    {
      DeQueue(pqueue,NULL);
    }

}

static int IsEmpty(Queue *pqueue)
{
  if(pqueue->front==NULL&&pqueue->rear==NULL&&pqueue->size==0)
    return 1;
  else
    return 0;
}

static int GetSize(Queue *pqueue)
{
  return pqueue->size;
}

static PNode GetFront(Queue *pqueue,Item *pitem)
{
  if(IsEmpty(pqueue)!=1&&pitem!=NULL)
    {
      *pitem = pqueue->front->data;
    }
  return pqueue->front;
}


static PNode GetRear(Queue *pqueue,Item *pitem)
{
  if(IsEmpty(pqueue)!=1&&pitem!=NULL)
    {
      *pitem = pqueue->rear->data;
    }
  return pqueue->rear;
}

static PNode EnQueue(Queue *pqueue,Item item)
{
  PNode pnode = (PNode)malloc(sizeof(Node));
  if(pnode != NULL)
    {
      pnode->data = item;
      pnode->next = NULL;

      if(IsEmpty(pqueue))
        {
          pqueue->front = pnode;
        }
      else
        {
          pqueue->rear->next = pnode;
        }
      pqueue->rear = pnode;
      pqueue->size++;
    }
  return pnode;
}

static PNode DeQueue(Queue *pqueue,Item *pitem)
{
  PNode pnode = pqueue->front;
  if(IsEmpty(pqueue)!=1&&pnode!=NULL)
    {
      if(pitem!=NULL)
        *pitem = pnode->data;
      pqueue->size--;
      pqueue->front = pnode->next;
      free(pnode);
      if(pqueue->size==0)
        pqueue->rear = NULL;
    }
  return pqueue->front;
}

static void QueueTraverse(Queue *pqueue,void (*visit)())
{
  PNode pnode = pqueue->front;
  int i = pqueue->size;
  while(i--)
    {
      visit(pnode->data);
      pnode = pnode->next;
    }

}

#endif // _SD_QUEUE_H_

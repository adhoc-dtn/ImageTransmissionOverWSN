/*!
 ******************************************************************************
 * \brief 連結リスト
 *
 ******************************************************************************
 */

#include <stdio.h>
#include <malloc.h>

typedef struct _cell_t
{
  struct _cell_t *next;
  int data;
}cell_t;

extern int  setList(cell_t *p, int data);
extern int  deleteList(cell_t* p, int data);
extern void allDeleteList(cell_t *p);
extern void printList(cell_t *p);
cell_t g_cell;//セルの先頭（このセルにはデータを入れない）

int main(int argc, char** argv)
{
  int input;
  char select;
  g_cell.next = NULL;//先頭のセルの初期化
  
  while (1) {
    fflush(stdin);    
    printf("-----------------------------------\n"
	   "input something following alphabet.\n"
	   "input: i, delete number d, delete all list a, print p, exit e\n");
    select = getchar();
    switch (select) {
    case ('i'):
      printf("insert (input number): ");
      fflush(stdin);
      input = getchar();
      setList(&g_cell, input);
      printf("insert %d has successfly done.\n\n",input);
      
      break;
    case ('d'):
      printf("delete (input number): ");
      input = getchar();
      deleteList(&g_cell, input);
      printf("delete %d has successfly done.\n",input);
      break;
    case ('a'):
      printf("delete all\n");
      allDeleteList(&g_cell);
      printf("deleting all list has successfly done.\n");
      break;
    case ('p'):
      printf("print list\n");
      printList(&g_cell);
      break;
    case ('e'):
      printf("exit now...\n");
      return (0);
      break;
    default :
      printf("input number through 1-4\n");
    }
    
  }
  return(0);
}

/*!
 ******************************************************************************
 * \fn int setList(cell_t* p, int data)
 * \param *p 挿入したいセルのポインタ（この直後にセルを挿入する）
 * \param data データ
 * \return -1の時はデータのセットができなかった時、正常時は0を返す
 *
 ******************************************************************************
 */
int
setList(cell_t* p, int data)
{
  cell_t *tmp;
  tmp = (cell_t*)malloc(sizeof(cell_t));
  if(tmp == NULL)
  {
    return(-1);
  }

  tmp->next = p->next;
  tmp->data = data;
  p->next = tmp;
  return(0);
}

/*!
 ******************************************************************************
 * \fn int deleteList(cell_t* p)
 * \brief 引数で受け取ったセルの次のポインタのデータを削除
 * \param *p 削除したいセルを指しているポインタ
 * \return -1:エラー 0:正常終了
 *
 ******************************************************************************
 */
int
deleteList(cell_t* p, int data)
{
  cell_t *current, *prev;
  
  prev = p;
  for (current = p->next; current != NULL; current = current->next) 
    ;
    
  if (current == NULL)
  {
    return(-1);
  }
  current    = prev->next;
  prev->next = current->next;
  free(current);
  return(0);
}

/*!
 ******************************************************************************
 * \fn void allDeleteList(cell_t *p)
 * \brief データの全削除
 * \param *p 先頭のセルのポインタ
 * \return None
 *
 ******************************************************************************
 */
void
allDeleteList(cell_t *p)
{
  cell_t *tmp;
  while (p->next != NULL)
  {
    tmp = p->next;
    p->next = tmp->next;
    free(tmp);
  }
}

void printList(cell_t *p) 
{
  int i;
  cell_t *current, *prev;
  for (i=0,current = p->next; current != NULL; current = current->next) 
    printf("[%3d]\t%d\n",i,current->data);
  
  
}

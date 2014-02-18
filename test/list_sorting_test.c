/*リスト構造(nextが次要素ポインタ持つようなリスト)をもつ構造体をマージソートするテストプログラム*/
/* 1 qsortはリスト相手にソートができない
 * (連続的にメモリが割り当てられている場合にしか動作しない)
 */
  
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/types.h>
#include <pthread.h>
#include <errno.h>

#define SIZE 20

struct data {
    int a;
    int b;
    int c;
    struct data  *next;
};

struct data_list {
    u_int list_size;   //リストサイズ
    struct data head; //ヘッド ただし要素はnext以降に入れること
    struct data_list *next;
};

void mergeSort    (struct data  *head,       u_int size);
static void merge (struct data  *list1_head, u_int size_left, struct data  *list2_head, u_int size_right, struct data  *original_head);

int main(void)
{
    int i;
    struct data_list  *new_node, *sort_datalist;
    struct data  *new, *cur, *prev;

    /* 乱数の生成 */
    srand( (unsigned int)time( NULL ) );
    
    new_node            = (struct data_list *)malloc(sizeof(struct data_list)); //headはmallocになるはず
    //    new_node            = (struct data_list *)malloc(sizeof(struct data_list)); //headはmallocになるはず

    new_node->list_size = 0;
    new_node->next      = NULL;

    
    cur = &new_node->head;
    for( i = 0; i < SIZE ; i++ ){
        new = (struct data  *)malloc(sizeof(struct data ));  //ここ、メモリが連番で確保されるとは限らない。
        new->a = rand() % 100;   /* 0～99の乱数 */
        new->b = rand() % 100;
        new->c = rand() % 100;
        new->next  = NULL;
        printf( "%d\t", new->a );
        printf( "%d\t", new->b );
        printf( "%d\t", new->c );
        printf( "\n" );
        
        cur->next = new;
        cur = cur->next;
        new_node->list_size++;

    }

    /* struct data 構造体のbを基準にソート */
    printf( "\n--struct data 構造体のbを基準にソート--\n\n" );
    mergeSort( &new_node->head, new_node->list_size);

    //new_nodeはいらなくなる、はず。
    
    cur =   &new_node->head;
    /* ソート後のデータを表示 */
    while( cur != NULL  ){
       
        printf( "%d\t", cur->a );
        printf( "%d\t", cur->b );
        printf( "%d\t", cur->c );
        printf( "\n" );
        cur = cur->next;
    }

    return 0;
}

//merge sort (head->nextから要素が入ってくる
//sort_resultに要素が次々に入る
void mergeSort (struct data  *head, u_int size)  {

    struct data *data_left_head, *data_left, *data_right_head, *data_right_set;
    struct data *data_cur,        *data_prev;
    u_int        size_left_set, size_right_set;
    u_short      numl, numr;
    printf("[ sort  ] head %p size %d\n"
           ,head, size);
    
    //    data_left_head  = (struct data *)malloc(sizeof(struct data));
    if ((data_right_head = (struct data *)malloc(sizeof(struct data))) == NULL) {
        err(EXIT_FAILURE, "cannot allocate memory exit.\n"); //右側だけ必要
    }
    data_right_head->next = NULL;
    
    if( size > 1 ){
        u_int size_left_set  = size / 2;
        u_int size_right_set = size - size_left_set; 

        data_left_head        = head; //head自体に要素はなく、nextから要素である
        data_left             = data_left_head->next;
        for(numl = 0 ; numl < size_left_set ; data_left = data_left->next, numl++) 
            ;
        data_right_head->next = data_left;
        

        mergeSort(data_left_head,  size_left_set );
        mergeSort(data_right_head, size_right_set);
        merge    (data_left_head, size_left_set, data_right_head, size_right_set, head);
        //途中経過
        data_prev      = head;
        data_cur       = head->next;
        
        while (data_cur!=NULL ) {
            printf ("%d\t",data_cur->a);
            data_cur = data_cur->next;
        }
        
        printf("\n");
        
    }
    //    free(data_left_head);
    free(data_right_head); 
}

//merge 
static void merge (struct data  *list1_head, u_int size_left, struct data  *list2_head, u_int size_right, struct data  *original_head) {

    u_int        numl = 0, numr = 0, numAll = 0, sizeAll;
    struct data *data_cur, *data_prev, *data_left_cur,  *data_left_prev, *data_right_cur, *data_right_prev;
    printf("[ merge ] lefthead %p leftsize %d righthead %p rightsize %d originalhead %p\n"
           ,list1_head
           ,size_left
           ,list2_head
           ,size_right
           ,original_head);

    data_prev       = original_head;
    //    data_cur        = original_head->next;
    data_left_prev  = list1_head;
    data_left_cur   = data_left_prev->next;
    data_right_prev = list2_head;
    data_right_cur  = data_right_prev->next;
    
    while( numl < size_left || numr < size_right){
        if( numr >= size_right  || (numl < size_left && data_left_cur->a < data_right_cur->a)){
            data_prev->next = data_left_cur;
            data_left_cur   = data_left_cur->next;
            data_prev       = data_prev->next;
            numl++;
        } else {
            data_prev->next = data_right_cur;
            data_right_cur  = data_right_cur->next;
            data_prev       = data_prev->next;
            numr++;
        }
    }

    data_prev->next = NULL;
   
}

#include <stdio.h>

enum {
    IMAGE_DATA_PACKET=0,      /* 画像データ                */
    AMOUNT_DATA_SEND_MESS,  /* 転送データ量通知メッセージ */
    INITIAL_PERIOD_MESS,    /* 初期周期通知メッセージ    */
    NEW_PERIOD_MESS,        /* 新周期通知メッセージ*/
    TYPE_NUM,               /* 要素数*/
};
int main()
{
    int i;
    printf("IMAGE_DATA_PACKET     %d\n"
           "AMOUNT_DATA_SEND_MESS %d\n"
           "INITIAL_PERIOD_MESS   %d\n"
           "NEW_PERIOD_MESS       %d\n"
           "TYPE_ALL              %d\n"
           ,IMAGE_DATA_PACKET
           ,AMOUNT_DATA_SEND_MESS
           ,INITIAL_PERIOD_MESS
           ,NEW_PERIOD_MESS
           ,TYPE_NUM);
    return 0;
}
    

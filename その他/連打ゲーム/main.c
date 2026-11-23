#include <stdio.h>
#include <windows.h>
#include <conio.h>

int main(void) {

    int push = 0;
    DWORD ts, ts2;
    int for_all;

    printf("エンターキーをできるだけ早く20連打してね\n");
    printf("それではいきます。\n\n");
    printf("　3");
    Sleep(1000);
    printf("　2");
    Sleep(1000);
    printf("　1\n\n");
    Sleep(1000);
    printf("スタート\n");
    
    ts = GetTickCount();
    
    while(push < 20) {
        for_all = _getch();
        if (for_all == '\r') {
            push++;
            printf("%d回目\n", push);
        }
    }

    ts2 = GetTickCount();
    printf("\n今回の記録は%.4f秒でした\n", (double)(ts2-ts)/1000.0);
    
}
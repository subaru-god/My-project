#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <windows.h>

//gcc -finput-charset=cp932 -fexec-charset=cp932 main.c -o main && main.exe

int make_random_num(void) {
    int new_num = rand() %100 + 1;
    return (new_num);
}

int point_sys(int point, int sys) {
    if(sys == 0) {
        point = point * 1.5;
        if(point == 1 || point == 0) {
            point = 2;
        }
    }
    return point;
}

int main(void){
    int new_num = 0, count, cache1, point, use, sys, mux_point, start_point;
    char input[5];
    char input1;
    char High[] = "High";
    char Low[] = "Low";

    start:
    printf("\nHigh&Lowゲームへようこそ！！\n\n最小の数が1   ||   最大の数が100です。\n\n");
    printf("ランダムに数字が表示されるので\n次の数字が今の数字より大きい(High)か小さい(Low)かを予想してね。\n\n");
    printf("まずは最初の所持ポイントを決めます\n\n");
    system("pause");
    srand((unsigned int)time(NULL));
    new_num = make_random_num();
    point = rand() % 801 + 200;
    start_point = point;
    printf("あなたの最初の所持ポイントは%dPです。\n\n", point);
    mux_point = point;
    count = 1;

    while(1) {

        printf("%d回目\n%dP\n\n", count, point);
        do {
            printf("%dよりも大きい(High)？小さい(Low)？：", new_num);
            scanf(" %4s", input);
/*        } while(strcmp(input, High) != 0 && strcmp(input, Low) != 0);

        do { */
            printf("何ポイント掛ける？(半角数字のみで)：");
            if(scanf(" %d", &use) != 1) {
                while (getchar() != '\n');
                use = 0;
            }
        } while((strcmp(input, High) != 0 && strcmp(input, Low) != 0) || use > point || use <= 0);

        point -= use;

        printf("結果は...");
        cache1 = new_num;
        Sleep(1000);

        new_num = make_random_num();
        printf("%d\n\n", new_num);

        if(cache1 == new_num) {
            printf("同じ数なのでもう一度\n\n");
            point += use;
        } else if (strcmp(input, Low)==0 && cache1>new_num) {
            sys = 0;
            cache1 = point;
            point += point_sys(use, sys);
            count++;
            printf("正解！！  %dP獲得！！\n\n", point-cache1);
        } else if (strcmp(input, High)==0 && cache1<new_num) {
            sys = 0;
            cache1 = point;
            point += point_sys(use, sys);
            count++;
            printf("正解！！  %dP獲得！！\n\n", point-cache1);
        }else {
            printf("残念  %dP失いました\n\n", use);
            count++;
        }
        
        if(count <= 20) {
            printf("ゲーム終了！\n%dPから最終的に%dPまで%dP変化しました\nまた、最初の%dPから最大%dPまで\n%dP増やしました。\n\n", start_point, point,point-start_point, start_point, mux_point, mux_point-start_point);
            break;
        }else if(mux_point<point) {
            mux_point = point;
        }
    }

/*    do {
        printf("もういちどやる？ y/n：");
        scanf(" %c", &input1);
    } while(input1 != 'y' && input1 != 'n');
    
    if(input1 == 'y') {
        goto start;
    }else {
        return 0;
    }
    

*/
}
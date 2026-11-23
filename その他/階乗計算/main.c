#include <stdio.h>

int main(void) {
    int input_num;
    double answer,sub,sub2;
    char txt;
    while(1) {
        printf("好きな数字を入力してください：");
        scanf(" %d", &input_num);
        
        sub = input_num;
        printf("\n%d!=\n%d", input_num,input_num);
        
        while(sub>1) {
            sub--;
            printf("×%.0f", sub);
        }
        
        printf("=\n");

        sub = input_num;
        sub2 = 1;

        while(sub>0) {
            sub2 = sub*sub2;
            sub--;
        }

        printf("%.0f\n\n", sub2);

        continue_choice:
        printf("続行しますか？ y/n：");
        scanf(" %c", &txt);

        if (txt == 'n') {
            return(0);
        } else if(txt == 'y') {
            printf("\n");
            continue;
        } else {
            printf("\n半角英字のyかnを入力してください\n");
            goto continue_choice;
        }
        
    }
}

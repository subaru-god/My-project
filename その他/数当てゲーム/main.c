#include <stdio.h>
#include <stdlib.h>
#include <time.h>

    int answer,input,count,y;
    time_t set_time,set_time2;

int check_answer(int answer){
    printf("%d‰ñ–Ú\n—\‘z‚Ì”š‚ğ“ü—Í‚µ‚Ä‚Ë:", count);
    scanf(" %d", &input);

    if(input < answer) {
        printf("‚à‚Á‚Æy‘å‚«‚¢‚æzI\n\n");
        return(0);
    } else if (input > answer) {
        printf("‚à‚Á‚Æy¬‚³‚¢‚æzI\n\n");
        return(0);
    } else {
        printf("\n³‰ğ!\n");
        return(1);
    }

}

int main(void) {

    srand((unsigned int)time(NULL));
    answer = rand() %100 + 1;
    set_time = time(NULL);

    while(1){
        count++;

        y = check_answer(answer);

        if(y==0) {
            printf("‚à‚¤ˆê‰ñl‚¦‚æ‚¤\n");
        } else if (y==1) {
            set_time2 = time(NULL);
            printf("%d‰ñ‚©‚©‚è‚Ü‚µ‚½\n", count);
            printf("%d•b‚©‚©‚è‚Ü‚µ‚½\n(%d`%d)\n", set_time2-set_time, set_time, set_time2);
            break;
        }
    }
    return(0);
}
#include <stdio.h>

int createBoard(int bord[3][3]) {
    inti, j;
    
    printf("\n");
    printf(" -1-+-2-+-3-+-\n")

    for(i = 0; i < 3; i++) {
        for(j = 0; j < 3; j++) {
            
            if(bord[i][j] == 1) {
                printf("Z");
            }else if(board[i][j] == 2) {
                printf("~");
            }else {
                printf("@");
            }
            
            if(j < 2) {
                printf("|");
            }
        }
        printf("\n");
        
        if(i < 2) {
            printf("---+---+---\n");
        }
    }
    printf("\n");
}

int main(void) {
    int board[3][3]

    for
}
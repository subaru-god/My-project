#include <stdio.h>

int main(void) {

    int player[2] = {0, 0};
    char key;
    
    while(1) {
    printf("\nwasd‚Ì‚¢‚¸‚ê‚©‚ğ“ü—Í‚µ‚Ä‚­‚¾‚³‚¢\nŒ»İ(%d,%d)‚É‚¢‚Ü‚·", player[0], player[1]);
    scanf(" %c", &key);
    
    switch(key) {
        case('w'):
                player[1]--;
                break;
        case('a'):
                player[0]--;
                break;
        case('s'):
                player[1]++;
                break;
        case('d'):
                player[0]++;
                break;
    }
    }
}
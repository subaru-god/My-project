#include <stdio.h>

int kakeru(int x, int y) {
    return x * y;
}

int waru(int x, int y) {
    return x / y;
}

int tasu(int x, int y) {
    return x + y;
}

int hiku(int x, int y) {
    return x - y;
}

int amari(int x, int y) {
    return x % y;
}

int main() {

    int a, b;
    int result;
    char ope;
    printf("Enter number a: ");
    if (scanf("%d", &a) != 1) {
        printf("Invalid input for number a\n");
        return 1;
    }
    printf("Enter number b: ");
    if (scanf("%d", &b) != 1) {
        printf("Invalid input for number b\n");
        return 1;
    }
    printf("Enter ope: ");
    scanf(" %c", &ope);

    switch (ope ) {
        case '+':
            result = tasu(a, b);
            break;
        case '-':
            result = hiku(a, b);
            break;
        case '*':
            result = kakeru(a, b);
            break;
        case '/':
            result = waru(a, b);
            break;
        case '%':
            result = amari(a, b);
            break;
        default:
            printf("Invalid operation\n");
            return 1;
    }


if ((ope == '/' || ope == '%') && b == 0) {
        printf("Error: Division by zero\n");
        return 1;
    }

    printf("a %c b = %d\n", ope, result);
    return 0;
}

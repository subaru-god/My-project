#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(void) {
    int rest_stones, get_stones_com, get_stones_user, take_stones;
    char for_all;

    // 乱数の種はプログラム開始時に1度だけ設定する
    srand((unsigned int)time(NULL));

    while (1) {
        // ゲーム開始ごとにスコアを初期化
        get_stones_com = 0;
        get_stones_user = 0;
        rest_stones = rand() % 30 + 1;

        printf("==============================\n");
        printf("ゲーム開始！残りの石の数: %d\n", rest_stones);
        printf("==============================\n\n");

        while (rest_stones > 0) {
            // --- プレイヤーのターン ---
            printf("【あなたのターン】\n");
            do {
                printf("1~5個の石を取ってください (残り: %d個): ", rest_stones);
                scanf("%d", &take_stones);

                if (take_stones < 1 || take_stones > 5 || take_stones > rest_stones) {
                    printf("\n無効な入力です。1~5個の間で、残りの石の数以下の数を指定してください。\n\n");
                }
            } while (take_stones < 1 || take_stones > 5 || take_stones > rest_stones);

            rest_stones -= take_stones;
            get_stones_user += take_stones;
            printf("%d個の石を取りました。 \n(残りの石の数: %d)\n\n", take_stones, rest_stones);

            // プレイヤーが石を取って0になったらループを抜ける
            if (rest_stones == 0) {
                break;
            }

            // --- コンピュータのターン ---
            printf("【コンピュータのターン】...\n");
            if (rest_stones > 5) {
                take_stones = rand() % 5 + 1;
            } else {
                take_stones = rand() % rest_stones + 1;
            }

            rest_stones -= take_stones;
            get_stones_com += take_stones;
            printf("コンピュータは %d個の石を取りました。 \n(残りの石の数: %d)\n\n", take_stones, rest_stones);
        }

        // --- 勝敗判定（石がすべてなくなった後） ---
        printf("------------------------------\n");
        printf("ゲーム終了！\n");
        printf("あなたが取った石の数: %d\n", get_stones_user);
        printf("コンピュータが取った石の数: %d\n", get_stones_com);

        if (get_stones_user > get_stones_com) {
            printf("⇒ あなたの勝ちです！\n");
        } else if (get_stones_user < get_stones_com) {
            printf("⇒ コンピュータの勝ちです！\n");
        } else {
            printf("⇒ 引き分けです！\n");
        }
        printf("------------------------------\n\n");

        // --- 再戦確認 ---
        do {
            printf("もう一度やりますか？ (y/n): ");
            scanf(" %c", &for_all);
        } while (for_all != 'y' && for_all != 'Y' && for_all != 'n' && for_all != 'N');

        if (for_all == 'n' || for_all == 'N') {
            printf("ゲームを終了します。お疲れ様でした！\n");
            break;
        }
        printf("\n");
    }

    return 0;
}
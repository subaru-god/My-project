#include <stdio.h>
#include <string.h>
#include <time.h>

/* BCC55用の真偽値定義 */
#define bool int
#define true 1
#define false 0

/* 64ビット整数（試行回数用） */
unsigned __int64 total_attempts = 0;
bool is_found = false;
clock_t start_time;

/* 使用する文字セット（英大小文字 + 数字 + 記号 = 計94文字） */
const char* charset = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";

/* 再帰的にパスワードを生成して検証する関数 */
void brute_force_core(const char* target, char* current, int position, int max_length) {
    int i;
    int charset_len;

    if (is_found) return;

    /* 指定された桁数に達したらパスワードを検証 */
    if (position == max_length) {
        current[position] = '\0';
        total_attempts++;

        /* ーーー★リアルタイム表示の改良箇所★ーーー */
        /* パソコンが速すぎるため、10万回に1回だけ画面を書き換えて目に見える速度にする */
        if (total_attempts % 100 == 0) {
            clock_t current_time = clock();
            double elapsed = (double)(current_time - start_time) / CLOCKS_PER_SEC;
            double speed = 0;
            if (elapsed > 0) {
                speed = (double)total_attempts / elapsed;
            }
            
            /* \r を使って、同じ行の文字をリアルタイムに書き換える */
            /* 現在試している文字列「current」をハッキング画面風に表示 */
            printf("\r?? [解析パターン]: %-8s | 試行: %I64u回 | 速度: %.0f 回/秒", current, total_attempts, speed);
            fflush(stdout); /* 画面を強制的に最新状態にする */
        }

        /* パスワードが一致した場合 */
        if (strcmp(current, target) == 0) {
            clock_t end_time;
            double elapsed;
            is_found = true;
            end_time = clock();
            elapsed = (double)(end_time - start_time) / CLOCKS_PER_SEC;
            
            /* 行をクリアして結果をきれいに表示 */
            printf("\r                                                                                \r");
            printf("?? 【解読成功】 パスワードは 「%s」 でした！\n", current);
            printf("??  かかった時間: %.2f 秒\n", elapsed);
            printf("?? 総試行回数  : %I64u 回\n", total_attempts);
        }
        return;
    }

    /* 文字セットの全パターンをループ */
    charset_len = strlen(charset);
    for (i = 0; i < charset_len; i++) {
        current[position] = charset[i];
        brute_force_core(target, current, position + 1, max_length);
        if (is_found) return;
    }
}

/* 総当たり攻撃を開始する関数 */
void start_brute_force(const char* target) {
    int target_len;
    char current_attempt[16]; /* 余裕を持たせた配列サイズ */
    int length;

    target_len = strlen(target);
    memset(current_attempt, 0, sizeof(current_attempt));

    printf("==================================================\n");
    printf(" ?? パスワード解読シミュレーター（リアルタイム版）\n");
    printf(" ターゲット: [%s] (%d桁)\n", target, target_len);
    printf("==================================================\n\n");

    start_time = clock();
    
    /* 1桁からターゲットの桁数まで順番に試す */
    for (length = 1; length <= target_len; length++) {
        if (is_found) break;
        printf("\n[%d桁の探索を開始します...]\n", length);
        brute_force_core(target, current_attempt, 0, length);
    }

    if (!is_found) {
        printf("\n探索が終了しましたが、見つかりませんでした。\n");
    }
}

int main() {
    /* 【テスト用ターゲットパスワード】 */
    /* 最初は動きを見るために 3?4 文字で試すのがおすすめです */
    /* 例: 「k@4!」や「A5*」など */
    const char* secret_password ; 
    printf("パスワードを入力してください：");
    scanf("%s", secret_password);
    start_brute_force(secret_password);

    return 0;
}

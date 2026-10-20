#include <windows.h>
#include <vector>
#include <cmath>
#include <fstream>

// 盤面定義
const int BOARD_SIZE = 8;
const int CELL_SIZE = 50;
const int OFFSET_X = 40;
const int OFFSET_Y = 40;
const int PANEL_HEIGHT = 60; // 下部ステータス用

int board[BOARD_SIZE][BOARD_SIZE] = { 0 };
int currentTurn = 1; // 1: 人間(黒), 2: AI(白)
bool isGameOver = false;

// AIレベル (1: 初級=ランダム, 2: 中級=枚数優先, 3: 上級=評価関数)
int aiLevel = 3; 

// 追加学習用：今回の対局履歴を保存する構造体
struct HistoryRecord {
    int boardState[BOARD_SIZE][BOARD_SIZE];
    int moveX;
    int moveY;
    int player;
};
std::vector<HistoryRecord> gameHistory;

// 8方向の移動ベクトル
const int dx[8] = { 0, 0, -1, 1, -1, 1, -1, 1 };
const int dy[8] = { -1, 1, 0, 0, -1, -1, 1, 1 };

// 指定したマスに石を置いたとき、裏返せる相手の石のリストを取得
std::vector<POINT> GetFlipDiscs(int x, int y, int player) {
    std::vector<POINT> flips;
    if (x < 0 || x >= BOARD_SIZE || y < 0 || y >= BOARD_SIZE) return flips;
    if (board[y][x] != 0) return flips;

    int opponent = (player == 1) ? 2 : 1;

    for (int i = 0; i < 8; i++) {
        int nx = x + dx[i];
        int ny = y + dy[i];
        std::vector<POINT> temp;

        while (nx >= 0 && nx < BOARD_SIZE && ny >= 0 && ny < BOARD_SIZE && board[ny][nx] == opponent) {
            POINT pt = { nx, ny };
            temp.push_back(pt);
            nx += dx[i];
            ny += dy[i];
        }

        if (nx >= 0 && nx < BOARD_SIZE && ny >= 0 && ny < BOARD_SIZE && board[ny][nx] == player) {
            flips.insert(flips.end(), temp.begin(), temp.end());
        }
    }
    return flips;
}

// 指定プレイヤーがどこかに石を置けるか判定
bool CanMove(int player) {
    for (int y = 0; y < BOARD_SIZE; y++) {
        for (int x = 0; x < BOARD_SIZE; x++) {
            if (!GetFlipDiscs(x, y, player).empty()) {
                return true;
            }
        }
    }
    return false;
}

// ゲーム初期化
void InitGame() {
    for (int y = 0; y < BOARD_SIZE; y++) {
        for (int x = 0; x < BOARD_SIZE; x++) {
            board[y][x] = 0;
        }
    }
    board[3][3] = 2; board[3][4] = 1;
    board[4][3] = 1; board[4][4] = 2;
    currentTurn = 1;
    isGameOver = false;
    gameHistory.clear();
}

// 棋譜（追加学習データ）をファイルに保存する関数
void SaveHistoryToFile() {
    std::ofstream file("othello_history.txt", std::ios::app);
    if (!file.is_open()) return;

    file << "=== NEW GAME (AI Level: " << aiLevel << ") ===\n";
    for (const auto& h : gameHistory) {
        file << "Player: " << h.player << " Move:(" << h.moveX << "," << h.moveY << ")\n";
    }
    file.close();
}

// AIの指し手決定（レベルに応じた思考）
POINT GetAIBestMove(int aiPlayer) {
    std::vector<POINT> validMoves;
    for (int y = 0; y < BOARD_SIZE; y++) {
        for (int x = 0; x < BOARD_SIZE; x++) {
            if (!GetFlipDiscs(x, y, aiPlayer).empty()) {
                POINT pt = { x, y };
                validMoves.push_back(pt);
            }
        }
    }

    if (validMoves.empty()) return { -1, -1 };

    // レベル1: ランダム
    if (aiLevel == 1) {
        int idx = rand() % validMoves.size();
        return validMoves[idx];
    }

    // レベル2: 最も多く裏返せる手を選ぶ
    if (aiLevel == 2) {
        POINT bestMove = validMoves[0];
        size_t maxFlips = 0;
        for (const auto& move : validMoves) {
            auto flips = GetFlipDiscs(move.x, move.y, aiPlayer);
            if (flips.size() > maxFlips) {
                maxFlips = flips.size();
                bestMove = move;
            }
        }
        return bestMove;
    }

    // レベル3: 上級（角や辺を重視する評価関数 + オンライン/ONNXモデル拡張用）
    // 角(0,0など)を最優先し、X打ちやC打ちを避ける重み付け
    int weight[8][8] = {
        { 30, -12,  0,  0,  0,  0, -12,  30 },
        {-12, -15, -3, -3, -3, -3, -15, -12 },
        {  0,  -3,  0,  0,  0,  0,  -3,   0 },
        {  0,  -3,  0,  0,  0,  0,  -3,   0 },
        {  0,  -3,  0,  0,  0,  0,  -3,   0 },
        {  0,  -3,  0,  0,  0,  0,  -3,   0 },
        {-12, -15, -3, -3, -3, -3, -15, -12 },
        { 30, -12,  0,  0,  0,  0, -12,  30 }
    };

    POINT bestMove = validMoves[0];
    int maxScore = -9999;
    for (const auto& move : validMoves) {
        int score = weight[move.y][move.x];
        // さらに裏返せる枚数も少し加味
        auto flips = GetFlipDiscs(move.x, move.y, aiPlayer);
        score += (int)flips.size();

        if (score > maxScore) {
            maxScore = score;
            bestMove = move;
        }
    }
    return bestMove;
}

// 手番の更新処理
void AdvanceTurn(HWND hwnd) {
    int nextTurn = (currentTurn == 1) ? 2 : 1;

    if (CanMove(nextTurn)) {
        currentTurn = nextTurn;
    } else if (CanMove(currentTurn)) {
        MessageBoxA(hwnd, (nextTurn == 1) ? "黒はパスです。" : "白はパスです。", "パス", MB_OK);
    } else {
        isGameOver = true;
        SaveHistoryToFile(); // ゲーム終了時に学習用データを保存

        int blackCount = 0, whiteCount = 0;
        for (int y = 0; y < BOARD_SIZE; y++) {
            for (int x = 0; x < BOARD_SIZE; x++) {
                if (board[y][x] == 1) blackCount++;
                if (board[y][x] == 2) whiteCount++;
            }
        }
        char msg[120];
        wsprintfA(msg, "ゲーム終了!\n黒(あなた): %d\n白(AI): %d\n\n(学習データを記録しました)", blackCount, whiteCount);
        MessageBoxA(hwnd, msg, "対局結果", MB_OK);
    }
}

// ウィンドウプロシージャ
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE:
        InitGame();
        return 0;

    // 右クリックでAIレベルを変更するメニューを表示
    case WM_RBUTTONDOWN:
        {
            HMENU hMenu = CreatePopupMenu();
            AppendMenuA(hMenu, MF_STRING | (aiLevel == 1 ? MF_CHECKED : 0), 1, "AIレベル 1: 初級 (ランダム)");
            AppendMenuA(hMenu, MF_STRING | (aiLevel == 2 ? MF_CHECKED : 0), 2, "AIレベル 2: 中級 (枚数優先)");
            AppendMenuA(hMenu, MF_STRING | (aiLevel == 3 ? MF_CHECKED : 0), 3, "AIレベル 3: 上級 (戦略評価)");
            AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuA(hMenu, MF_STRING, 4, "ゲームをリセット");

            POINT pt;
            GetCursorPos(&pt);
            TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(hMenu);
        }
        return 0;

    case WM_COMMAND:
        {
            int id = LOWORD(wParam);
            if (id >= 1 && id <= 3) {
                aiLevel = id;
                char title[50];
                wsprintfA(title, "AIオセロ対戦アプリ (AIレベル: %d)", aiLevel);
                SetWindowTextA(hwnd, title);
            } else if (id == 4) {
                InitGame();
                InvalidateRect(hwnd, NULL, TRUE);
            }
        }
        return 0;

    case WM_LBUTTONDOWN:
        {
            if (isGameOver) return 0;

            if (currentTurn == 1) {
                int mouseX = LOWORD(lParam);
                int mouseY = HIWORD(lParam);
                int bx = (mouseX - OFFSET_X) / CELL_SIZE;
                int by = (mouseY - OFFSET_Y) / CELL_SIZE;

                auto flips = GetFlipDiscs(bx, by, 1);
                if (!flips.empty()) {
                    // 履歴の記録 (追加学習用)
                    HistoryRecord record;
                    memcpy(record.boardState, board, sizeof(board));
                    record.moveX = bx;
                    record.moveY = by;
                    record.player = 1;
                    gameHistory.push_back(record);

                    // 石を置く
                    board[by][bx] = 1;
                    for (const auto& pt : flips) {
                        board[pt.y][pt.x] = 1;
                    }
                    InvalidateRect(hwnd, NULL, TRUE);

                    AdvanceTurn(hwnd);
                    InvalidateRect(hwnd, NULL, TRUE);

                    // AIのターン
                    if (currentTurn == 2 && !isGameOver) {
                        POINT aiMove = GetAIBestMove(2);
                        if (aiMove.x != -1) {
                            auto aiFlips = GetFlipDiscs(aiMove.x, aiMove.y, 2);
                            
                            HistoryRecord aiRecord;
                            memcpy(aiRecord.boardState, board, sizeof(board));
                            aiRecord.moveX = aiMove.x;
                            aiRecord.moveY = aiMove.y;
                            aiRecord.player = 2;
                            gameHistory.push_back(aiRecord);

                            board[aiMove.y][aiMove.x] = 2;
                            for (const auto& pt : aiFlips) {
                                board[pt.y][pt.x] = 2;
                            }
                        }
                        AdvanceTurn(hwnd);
                        InvalidateRect(hwnd, NULL, TRUE);
                    }
                }
            }
        }
        return 0;

    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            // 背景（ボードの緑色）
            HBRUSH hGreen = CreateSolidBrush(RGB(0, 128, 0));
            HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hGreen);
            Rectangle(hdc, OFFSET_X, OFFSET_Y, OFFSET_X + BOARD_SIZE * CELL_SIZE, OFFSET_Y + BOARD_SIZE * CELL_SIZE);
            SelectObject(hdc, hOldBrush);
            DeleteObject(hGreen);

            // 格子線
            HPEN hPen = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
            HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
            for (int i = 0; i <= BOARD_SIZE; i++) {
                MoveToEx(hdc, OFFSET_X + i * CELL_SIZE, OFFSET_Y, NULL);
                LineTo(hdc, OFFSET_X + i * CELL_SIZE, OFFSET_Y + BOARD_SIZE * CELL_SIZE);
                MoveToEx(hdc, OFFSET_X, OFFSET_Y + i * CELL_SIZE, NULL);
                LineTo(hdc, OFFSET_X + BOARD_SIZE * CELL_SIZE, OFFSET_Y + i * CELL_SIZE);
            }

            // 【サポート機能】人間（黒ターン）が置ける場所に薄いドットを表示
            if (currentTurn == 1 && !isGameOver) {
                HBRUSH hHintBrush = CreateSolidBrush(RGB(0, 100, 0));
                SelectObject(hdc, hHintBrush);
                HPEN hNullPen = (HPEN)GetStockObject(NULL_PEN);
                SelectObject(hdc, hNullPen);

                for (int y = 0; y < BOARD_SIZE; y++) {
                    for (int x = 0; x < BOARD_SIZE; x++) {
                        if (!GetFlipDiscs(x, y, 1).empty()) {
                            int cx = OFFSET_X + x * CELL_SIZE;
                            int cy = OFFSET_Y + y * CELL_SIZE;
                            Ellipse(hdc, cx + 18, cy + 18, cx + CELL_SIZE - 18, cy + CELL_SIZE - 18);
                        }
                    }
                }
                DeleteObject(hHintBrush);
            }

            SelectObject(hdc, hOldPen);
            DeleteObject(hPen);

            // 石の描画
            for (int y = 0; y < BOARD_SIZE; y++) {
                for (int x = 0; x < BOARD_SIZE; x++) {
                    int cx = OFFSET_X + x * CELL_SIZE;
                    int cy = OFFSET_Y + y * CELL_SIZE;

                    if (board[y][x] == 1) {
                        HBRUSH hBlack = CreateSolidBrush(RGB(0, 0, 0));
                        SelectObject(hdc, hBlack);
                        Ellipse(hdc, cx + 4, cy + 4, cx + CELL_SIZE - 4, cy + CELL_SIZE - 4);
                        DeleteObject(hBlack);
                    } else if (board[y][x] == 2) {
                        HBRUSH hWhite = CreateSolidBrush(RGB(255, 255, 255));
                        SelectObject(hdc, hWhite);
                        Ellipse(hdc, cx + 4, cy + 4, cx + CELL_SIZE - 4, cy + CELL_SIZE - 4);
                        DeleteObject(hWhite);
                    }
                }
            }

            // 下部ステータス（現在のターンとAIレベル表示）
            SetBkMode(hdc, TRANSPARENT);
            char status[100];
            wsprintfA(status, "ターン: %s  |  [右クリック]でAI設定・リセット", (currentTurn == 1) ? "黒(あなた)" : "白(AI思考中...)");
            TextOutA(hdc, OFFSET_X, OFFSET_Y + BOARD_SIZE * CELL_SIZE + 15, status, lstrlenA(status));

            EndPaint(hwnd, &ps);
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    const char* CLASS_NAME = "AI_Othello_Window_v2";

    WNDCLASSA wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    RegisterClassA(&wc);

    // ウィンドウサイズをステータス分少し縦長に拡張 (幅490, 高さ570)
    HWND hwnd = CreateWindowExA(
        0, CLASS_NAME, "AIオセロ対戦アプリ (AIレベル: 3)",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 490, 570,
        NULL, NULL, hInstance, NULL
    );

    if (hwnd == NULL) return 0;

    ShowWindow(hwnd, nCmdShow);

    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}
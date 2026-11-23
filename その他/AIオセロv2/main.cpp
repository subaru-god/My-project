#include <windows.h>
#include <vector>
#include <cmath>
#include <fstream>
#include <algorithm>
#include <stdio.h>

// ONNX Runtime ヘッダ
#include "D:\codes\その他\AIオセロv2\onnxruntime\include\onnxruntime_cxx_api.h"

// 盤面定義
const int BOARD_SIZE = 8;

int board[BOARD_SIZE][BOARD_SIZE] = { 0 };
int currentTurn = 1; // 1: 黒, 2: 白
bool isGameOver = false;

// ゲームモード (1: 人対人, 2: 人対AI, 3: AI対AI)
int gameMode = 2; 

// AIレベル (1: 初級=ランダム, 2: 中級=確率選択, 3: 上級=最強最善手)
int aiLevel = 3;

// サポート機能レベル (0: OFF, 1: 置ける場所すべて, 2: 置ける場所 + AIおすすめ手)
int supportLevel = 2;

// キーボード操作用カーソル座標
int cursorX = 3;
int cursorY = 3;

// リアルタイム予想勝率 (-1.0(白有利) ～ 1.0(黒有利))
float currentEvalValue = 0.0f;

// AIのおすすめ手
POINT aiBestHint = { -1, -1 };

// AI思考メッセージ
char aiStatusMessage[100] = "準備完了";

// ONNX Runtime グローバル変数
Ort::Env* ortEnv = nullptr;
Ort::Session* ortSession = nullptr;
bool useAI = false;

const int dx[8] = { 0, 0, -1, 1, -1, 1, -1, 1 };
const int dy[8] = { -1, 1, 0, 0, -1, -1, 1, 1 };

// --- 画面サイズに応じた動的レイアウト構造体 ---
struct Layout {
    int cellSize;
    int offsetX;
    int offsetY;
};

// ウィンドウの現在のクライアント領域から最適なセルサイズとオフセットを計算
Layout GetLayout(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    int clientWidth = rc.right - rc.left;
    int clientHeight = rc.bottom - rc.top;

    int ox = 30;
    int oy = 30;
    int maxW = clientWidth - (ox * 2);
    int maxH = clientHeight - oy - 110; // 下部ステータス用スペース(110px)を確保

    int boardPx = std::min(maxW, maxH);
    if (boardPx < 160) boardPx = 160; // 最小サイズ保証
    int cell = boardPx / 8;

    // 盤面を水平方向に中央寄せ
    int actualBoardW = cell * 8;
    ox = (clientWidth - actualBoardW) / 2;

    return { cell, ox, oy };
}

// --- 1. ゲームルール & AI推論 ---

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

bool CanMove(int player) {
    for (int y = 0; y < BOARD_SIZE; y++) {
        for (int x = 0; x < BOARD_SIZE; x++) {
            if (!GetFlipDiscs(x, y, player).empty()) return true;
        }
    }
    return false;
}

void InitAIModel(HWND hwnd) {
    try {
        ortEnv = new Ort::Env(ORT_LOGGING_LEVEL_WARNING, "OthelloAI");
        Ort::SessionOptions sessionOptions;
        sessionOptions.SetIntraOpNumThreads(4);
        sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        const wchar_t* modelPath = L"othello_model.onnx";
        ortSession = new Ort::Session(*ortEnv, modelPath, sessionOptions);
        useAI = true;
        snprintf(aiStatusMessage, sizeof(aiStatusMessage), "AIモデル読み込み成功");
    } catch (...) {
        useAI = false;
        snprintf(aiStatusMessage, sizeof(aiStatusMessage), "AIモデル読み込み失敗");
    }
}

POINT GetAIMoveByModel(int aiPlayer) {
    std::vector<POINT> validMoves;
    for (int y = 0; y < BOARD_SIZE; y++) {
        for (int x = 0; x < BOARD_SIZE; x++) {
            if (!GetFlipDiscs(x, y, aiPlayer).empty()) {
                validMoves.push_back({ x, y });
            }
        }
    }
    if (validMoves.empty()) return { -1, -1 };

    if (aiLevel == 1) {
        return validMoves[rand() % validMoves.size()];
    }

    if (!useAI || ortSession == nullptr) {
        return validMoves[0];
    }

    std::vector<float> inputTensorValues(1 * 2 * 8 * 8, 0.0f);
    int myP = aiPlayer;
    int opP = (aiPlayer == 1) ? 2 : 1;
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            if (board[y][x] == myP) inputTensorValues[y * 8 + x] = 1.0f;
            else if (board[y][x] == opP) inputTensorValues[64 + y * 8 + x] = 1.0f;
        }
    }

    try {
        Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU);
        std::vector<int64_t> inputShape = { 1, 2, 8, 8 };
        Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
            memoryInfo, inputTensorValues.data(), inputTensorValues.size(), inputShape.data(), inputShape.size()
        );
        const char* inputNames[] = { "board_input" };
        const char* outputNames[] = { "policy_output", "value_output" };

        auto outputTensors = ortSession->Run(
            Ort::RunOptions{ nullptr }, inputNames, &inputTensor, 1, outputNames, 2
        );

        float* policyData = outputTensors[0].GetTensorMutableData<float>();
        float* valueData = outputTensors[1].GetTensorMutableData<float>();

        currentEvalValue = valueData[0];

        std::vector<std::pair<POINT, float>> scoredMoves;
        for (const auto& mv : validMoves) {
            int idx = mv.y * 8 + mv.x;
            scoredMoves.push_back({ mv, policyData[idx] });
        }

        std::sort(scoredMoves.begin(), scoredMoves.end(), [](const auto& a, const auto& b) {
            return a.second > b.second;
        });

        if (aiLevel == 2 && scoredMoves.size() > 1) {
            int topK = std::min((int)scoredMoves.size(), 3);
            return scoredMoves[rand() % topK].first;
        }

        return scoredMoves[0].first;

    } catch (...) {
        return validMoves[0];
    }
}

void UpdateSupportHint() {
    if (supportLevel == 2 && !isGameOver) {
        aiBestHint = GetAIMoveByModel(currentTurn);
    } else {
        aiBestHint = { -1, -1 };
    }
}

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
    currentEvalValue = 0.0f;
    cursorX = 3; cursorY = 3;
    snprintf(aiStatusMessage, sizeof(aiStatusMessage), "ゲーム開始");
    UpdateSupportHint();
}

bool AdvanceTurn(HWND hwnd) {
    int nextTurn = (currentTurn == 1) ? 2 : 1;
    if (CanMove(nextTurn)) {
        currentTurn = nextTurn;
        UpdateSupportHint();
        return true;
    } else if (CanMove(currentTurn)) {
        MessageBoxA(hwnd, (nextTurn == 1) ? "黒はパスです。" : "白はパスです。", "パス", MB_OK);
        UpdateSupportHint();
        return true;
    } else {
        isGameOver = true;
        int bCount = 0, wCount = 0;
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                if (board[y][x] == 1) bCount++;
                if (board[y][x] == 2) wCount++;
            }
        }
        char msg[120];
        snprintf(msg, sizeof(msg), "ゲーム終了!\n黒: %d石\n白: %d石", bCount, wCount);
        MessageBoxA(hwnd, msg, "結果発表", MB_OK);
        return false;
    }
}

// --- 個別メニュー表示関数群 ---

void ShowModeMenu(HWND hwnd) {
    HMENU hMenu = CreatePopupMenu();
    AppendMenuA(hMenu, MF_STRING | (gameMode == 1 ? MF_CHECKED : 0), 1, "人 vs 人");
    AppendMenuA(hMenu, MF_STRING | (gameMode == 2 ? MF_CHECKED : 0), 2, "人 vs AI");
    AppendMenuA(hMenu, MF_STRING | (gameMode == 3 ? MF_CHECKED : 0), 3, "AI vs AI (自動対戦)");

    POINT pt;
    GetCursorPos(&pt);
    TrackPopupMenu(hMenu, TPM_CENTERALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);
}

void ShowLevelMenu(HWND hwnd) {
    HMENU hMenu = CreatePopupMenu();
    AppendMenuA(hMenu, MF_STRING | (aiLevel == 1 ? MF_CHECKED : 0), 20, "レベル 1 (初級 - ランダム)");
    AppendMenuA(hMenu, MF_STRING | (aiLevel == 2 ? MF_CHECKED : 0), 21, "レベル 2 (中級 - 確率選択)");
    AppendMenuA(hMenu, MF_STRING | (aiLevel == 3 ? MF_CHECKED : 0), 22, "レベル 3 (上級 - 最強)");

    POINT pt;
    GetCursorPos(&pt);
    TrackPopupMenu(hMenu, TPM_CENTERALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);
}

void ShowSupportMenu(HWND hwnd) {
    HMENU hMenu = CreatePopupMenu();
    AppendMenuA(hMenu, MF_STRING | (supportLevel == 0 ? MF_CHECKED : 0), 10, "サポート: OFF");
    AppendMenuA(hMenu, MF_STRING | (supportLevel == 1 ? MF_CHECKED : 0), 11, "サポート: LV1 (置ける場所すべて)");
    AppendMenuA(hMenu, MF_STRING | (supportLevel == 2 ? MF_CHECKED : 0), 12, "サポート: LV2 (+ AIおすすめ手)");

    POINT pt;
    GetCursorPos(&pt);
    TrackPopupMenu(hMenu, TPM_CENTERALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);
}

void ShowAllMenu(HWND hwnd) {
    HMENU hMenu = CreatePopupMenu();
    AppendMenuA(hMenu, MF_STRING | (gameMode == 1 ? MF_CHECKED : 0), 1, "モード: 人 vs 人");
    AppendMenuA(hMenu, MF_STRING | (gameMode == 2 ? MF_CHECKED : 0), 2, "モード: 人 vs AI");
    AppendMenuA(hMenu, MF_STRING | (gameMode == 3 ? MF_CHECKED : 0), 3, "モード: AI vs AI (自動対戦)");
    AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
    
    AppendMenuA(hMenu, MF_STRING | (aiLevel == 1 ? MF_CHECKED : 0), 20, "AIレベル: 1 (初級)");
    AppendMenuA(hMenu, MF_STRING | (aiLevel == 2 ? MF_CHECKED : 0), 21, "AIレベル: 2 (中級)");
    AppendMenuA(hMenu, MF_STRING | (aiLevel == 3 ? MF_CHECKED : 0), 22, "AIレベル: 3 (上級)");
    AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);

    AppendMenuA(hMenu, MF_STRING | (supportLevel == 0 ? MF_CHECKED : 0), 10, "サポート: OFF");
    AppendMenuA(hMenu, MF_STRING | (supportLevel == 1 ? MF_CHECKED : 0), 11, "サポート: LV1");
    AppendMenuA(hMenu, MF_STRING | (supportLevel == 2 ? MF_CHECKED : 0), 12, "サポート: LV2");
    
    AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hMenu, MF_STRING, 5, "ゲームをリセット");

    POINT pt;
    GetCursorPos(&pt);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);
}

// --- 2. ウィンドウプロシージャ & GUI ---

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE:
        InitGame();
        InitAIModel(hwnd);
        return 0;

    // ウィンドウサイズ変更時に再描画を要求
    case WM_SIZE:
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;

    // キーボード操作ハンドラ
    case WM_KEYDOWN:
        {
            switch (wParam) {
            case VK_LEFT:  
                if (!isGameOver) cursorX = (cursorX - 1 + 8) % 8; 
                break;
            case VK_RIGHT: 
                if (!isGameOver) cursorX = (cursorX + 1) % 8; 
                break;
            case VK_UP:    
                if (!isGameOver) cursorY = (cursorY - 1 + 8) % 8; 
                break;
            case VK_DOWN:  
                if (!isGameOver) cursorY = (cursorY + 1) % 8; 
                break;
            case VK_RETURN:
            case VK_SPACE:
                if (!isGameOver) {
                    bool isHumanTurn = (gameMode == 1) || (gameMode == 2 && currentTurn == 1);
                    if (isHumanTurn) {
                        auto flips = GetFlipDiscs(cursorX, cursorY, currentTurn);
                        if (!flips.empty()) {
                            board[cursorY][cursorX] = currentTurn;
                            for (const auto& pt : flips) board[pt.y][pt.x] = currentTurn;
                            InvalidateRect(hwnd, NULL, TRUE);
                            if (AdvanceTurn(hwnd)) {
                                InvalidateRect(hwnd, NULL, TRUE);
                            }
                        }
                    }
                }
                break;
            case 'M': ShowModeMenu(hwnd); break;
            case 'H': ShowSupportMenu(hwnd); break;
            case 'R': ShowLevelMenu(hwnd); break;
            }
            InvalidateRect(hwnd, NULL, TRUE);
        }
        return 0;

    case WM_RBUTTONDOWN:
        ShowAllMenu(hwnd);
        return 0;

    case WM_COMMAND:
        {
            int id = LOWORD(wParam);
            if (id >= 1 && id <= 3) {
                gameMode = id;
                InitGame();
                InvalidateRect(hwnd, NULL, TRUE);
            } else if (id >= 20 && id <= 22) {
                aiLevel = id - 20 + 1;
                InvalidateRect(hwnd, NULL, TRUE);
            } else if (id >= 10 && id <= 12) {
                supportLevel = id - 10;
                UpdateSupportHint();
                InvalidateRect(hwnd, NULL, TRUE);
            } else if (id == 5) {
                InitGame();
                InvalidateRect(hwnd, NULL, TRUE);
            }
        }
        return 0;

    case WM_LBUTTONDOWN:
        {
            if (isGameOver) return 0;
            bool isHumanTurn = (gameMode == 1) || (gameMode == 2 && currentTurn == 1);
            if (!isHumanTurn) return 0;

            Layout layout = GetLayout(hwnd);
            int mouseX = LOWORD(lParam);
            int mouseY = HIWORD(lParam);
            int bx = (mouseX - layout.offsetX) / layout.cellSize;
            int by = (mouseY - layout.offsetY) / layout.cellSize;

            if (bx >= 0 && bx < 8 && by >= 0 && by < 8) {
                cursorX = bx;
                cursorY = by;
                
                auto flips = GetFlipDiscs(bx, by, currentTurn);
                if (!flips.empty()) {
                    board[by][bx] = currentTurn;
                    for (const auto& pt : flips) board[pt.y][pt.x] = currentTurn;
                    InvalidateRect(hwnd, NULL, TRUE);

                    if (AdvanceTurn(hwnd)) {
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

            Layout layout = GetLayout(hwnd);
            int cs = layout.cellSize;
            int ox = layout.offsetX;
            int oy = layout.offsetY;

            // 背景ボード
            HBRUSH hGreen = CreateSolidBrush(RGB(0, 128, 0));
            HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hGreen);
            Rectangle(hdc, ox, oy, ox + 8 * cs, oy + 8 * cs);
            SelectObject(hdc, hOldBrush);
            DeleteObject(hGreen);

            // グリッド線
            HPEN hPen = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
            HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
            for (int i = 0; i <= 8; i++) {
                MoveToEx(hdc, ox + i * cs, oy, NULL);
                LineTo(hdc, ox + i * cs, oy + 8 * cs);
                MoveToEx(hdc, ox, oy + i * cs, NULL);
                LineTo(hdc, ox + 8 * cs, oy + i * cs);
            }

            // 【サポート表示】
            if (supportLevel >= 1 && !isGameOver) {
                bool isHumanTurn = (gameMode == 1) || (gameMode == 2 && currentTurn == 1);
                if (isHumanTurn) {
                    HBRUSH hHint = CreateSolidBrush(RGB(0, 100, 0));
                    SelectObject(hdc, hHint);
                    HPEN hNull = (HPEN)GetStockObject(NULL_PEN);
                    SelectObject(hdc, hNull);
                    for (int y = 0; y < 8; y++) {
                        for (int x = 0; x < 8; x++) {
                            if (!GetFlipDiscs(x, y, currentTurn).empty()) {
                                int cx = ox + x * cs;
                                int cy = oy + y * cs;
                                int margin = cs / 5;
                                Ellipse(hdc, cx + margin, cy + margin, cx + cs - margin, cy + cs - margin);
                            }
                        }
                    }
                    DeleteObject(hHint);

                    if (supportLevel == 2 && aiBestHint.x != -1) {
                        HPEN hGoldPen = CreatePen(PS_SOLID, std::max(2, cs / 15), RGB(255, 215, 0));
                        SelectObject(hdc, hGoldPen);
                        HBRUSH hNullBrush = (HBRUSH)GetStockObject(NULL_BRUSH);
                        SelectObject(hdc, hNullBrush);

                        int cx = ox + aiBestHint.x * cs;
                        int cy = oy + aiBestHint.y * cs;
                        int margin = cs / 8;
                        Ellipse(hdc, cx + margin, cy + margin, cx + cs - margin, cy + cs - margin);

                        DeleteObject(hGoldPen);
                    }
                }
            }

            // 【キーボードカーソル（赤の破線枠）の描画】
            if (!isGameOver) {
                bool isHumanTurn = (gameMode == 1) || (gameMode == 2 && currentTurn == 1);
                if (isHumanTurn) {
                    HPEN hCursorPen = CreatePen(PS_DASH, 2, RGB(255, 0, 0));
                    SelectObject(hdc, hCursorPen);
                    HBRUSH hNullBrush = (HBRUSH)GetStockObject(NULL_BRUSH);
                    SelectObject(hdc, hNullBrush);

                    int cx = ox + cursorX * cs;
                    int cy = oy + cursorY * cs;
                    Rectangle(hdc, cx + 2, cy + 2, cx + cs - 2, cy + cs - 2);

                    DeleteObject(hCursorPen);
                }
            }

            SelectObject(hdc, hOldPen);
            DeleteObject(hPen);

            // 石の描画
            for (int y = 0; y < 8; y++) {
                for (int x = 0; x < 8; x++) {
                    int cx = ox + x * cs;
                    int cy = oy + y * cs;
                    int margin = std::max(2, cs / 12);
                    if (board[y][x] == 1) {
                        HBRUSH hB = CreateSolidBrush(RGB(0, 0, 0));
                        SelectObject(hdc, hB);
                        Ellipse(hdc, cx + margin, cy + margin, cx + cs - margin, cy + cs - margin);
                        DeleteObject(hB);
                    } else if (board[y][x] == 2) {
                        HBRUSH hW = CreateSolidBrush(RGB(255, 255, 255));
                        SelectObject(hdc, hW);
                        Ellipse(hdc, cx + margin, cy + margin, cx + cs - margin, cy + cs - margin);
                        DeleteObject(hW);
                    }
                }
            }

            // ステータス & 操作方法の表示（盤面の下部に追従するため文字切れしません）
            SetBkMode(hdc, TRANSPARENT);
            char status1[120], status2[120], helpMsg[150];
            
            float blackWinRate = (1.0f + currentEvalValue) * 50.0f;
            if (blackWinRate < 0.0f) blackWinRate = 0.0f;
            if (blackWinRate > 100.0f) blackWinRate = 100.0f;

            snprintf(status1, sizeof(status1), "モード: %s (Lv.%d) | 黒: %.1f%% / 白: %.1f%%", 
                (gameMode == 1) ? "人vs人" : (gameMode == 2 ? "人vsAI" : "AIvsAI"), 
                aiLevel, blackWinRate, 100.0f - blackWinRate);
            
            snprintf(status2, sizeof(status2), "サポート: LV%d | 状態: %s", supportLevel, aiStatusMessage);
            snprintf(helpMsg, sizeof(helpMsg), "【操作】矢印:移動 / Enter:配置 / M:モード / H:サポート / R:AIレベル");

            int textY = oy + 8 * cs + 10;
            TextOutA(hdc, ox, textY, status1, lstrlenA(status1));
            TextOutA(hdc, ox, textY + 20, status2, lstrlenA(status2));
            TextOutA(hdc, ox, textY + 40, helpMsg, lstrlenA(helpMsg));

            EndPaint(hwnd, &ps);
        }
        return 0;

    case WM_TIMER:
        if (!isGameOver && gameMode >= 2) {
            bool isAITurn = (gameMode == 3) || (gameMode == 2 && currentTurn == 2);
            if (isAITurn) {
                POINT aiMove = GetAIMoveByModel(currentTurn);
                if (aiMove.x != -1) {
                    auto flips = GetFlipDiscs(aiMove.x, aiMove.y, currentTurn);
                    board[aiMove.y][aiMove.x] = currentTurn;
                    for (const auto& pt : flips) board[pt.y][pt.x] = currentTurn;
                    InvalidateRect(hwnd, NULL, TRUE);
                    AdvanceTurn(hwnd);
                    InvalidateRect(hwnd, NULL, TRUE);
                }
            }
        }
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        if (ortSession) delete ortSession;
        if (ortEnv) delete ortEnv;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    const char* CLASS_NAME = "AI_Othello_Ultimate";
    WNDCLASSA wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassA(&wc);

    // ★ WS_OVERLAPPEDWINDOW を指定して「サイズ変更可能・最大化可能」に拡張
    HWND hwnd = CreateWindowExA(
        0, CLASS_NAME, "AIオセロ - 完全ニューラルネットワーク駆動",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 520, 620,
        NULL, NULL, hInstance, NULL
    );

    if (hwnd == NULL) return 0;

    SetTimer(hwnd, 1, 200, NULL);
    ShowWindow(hwnd, nCmdShow);

    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
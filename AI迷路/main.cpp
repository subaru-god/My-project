#include <windows.h>
#include <vector>
#include <queue>
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <cstdio>

using namespace std;

// コントロールID定義
#define IDC_COMBO_MODE 101
#define IDC_COMBO_AI   102
#define IDC_COMBO_MAZE 103
#define IDC_BTN_START  104

// 定数定義
const int CELL_SIZE = 18;  // 1マスのサイズ(px)
const int MARGIN = 20;     // 外枠余白
const int TOP_PANEL = 100; // ヘッダーエリア高さ

const UINT_PTR TIMER_AI = 1;
const UINT_PTR TIMER_PLAYER = 2;
const UINT_PTR TIMER_CLOCK = 3;
const UINT_PTR TIMER_COUNTDOWN = 4;

// 1マスあたりの自動移動スピード (80ms間隔)
const int MOVE_SPEED_DELAY = 80;

// 保存用ログファイル名
const char* LOG_FILE_NAME = "player_style.hex";

// 16進数保存用のコンパクト構造体 (合計24バイト)
#pragma pack(push, 1)
struct GameRecordBinary {
    UINT32 totalSteps;           // 総歩数
    UINT32 clearTimeMs;          // クリアタイム(ms)
    UINT32 intersectionCount;    // 交差点/角での停止回数
    UINT32 wrongTurnsCount;      // 無効な入力（壁への入力）回数
    double totalReactionTimeMs;  // 思考時間合計(ms)
};
#pragma pack(pop)

// 座標構造体
struct Point {
    int x, y;
    Point(int _x = 0, int _y = 0) : x(_x), y(_y) {}
    bool operator==(const Point& other) const {
        return x == other.x && y == other.y;
    }
};

enum GameMode { MODE_VS_AI = 0, MODE_VS_HUMAN = 1 };

// グローバル変数
int mazeW = 21;
int mazeH = 15;

vector<vector<int> > maze;
Point p1Pos(1, 1);
Point p2Pos(1, 1);
Point goalPos(1, 1);

Point aiLastPos(1, 1);

Point p1Dir(0, 0);     // Player1 の現在の進行方向
bool p1IsMoving = false; // 移動中フラグ

Point p2Dir(0, 0);     // Player2 の現在の進行方向
bool p2IsMoving = false;

int p1Steps = 0;
int p2Steps = 0;
DWORD startTime = 0;
DWORD elapsedTime = 0;

GameMode currentMode = MODE_VS_AI;
int aiLevel = 5;      // デフォルト Lv.5
int mazeLevel = 5;    // 1 ? 10
bool gameOver = false;
int winner = 0;

// カウントダウン用
bool isCountingDown = false;
int countdownValue = 3;

// 思考ラグ計測用変数
DWORD stopStartTime = 0;  // 停止（思考開始）時刻
bool isP1Stopped = true;  // 停止中かどうか（移動中以外＝思考中）
int currentIntersectionCount = 0;
double currentReactionTimeMs = 0.0;
int currentWrongTurnsCount = 0;

// 学習済みの基準値
double humanAvgReactionMs = 400.0;
double humanErrorRate = 0.15;

// AI思考待ち用タイマー状態
DWORD aiWaitUntil = 0;

// UIハンドル
HWND hComboMode = NULL;
HWND hComboAI = NULL;
HWND hComboMaze = NULL;
HWND hBtnStart = NULL;

// 別の方向へ曲がれるか（進行方向変更が可能か）の判定
bool canChangeDirection(Point pos, Point currentDir) {
    if (currentDir.x == 0 && currentDir.y == 0) return true;

    Point leftDir(-currentDir.y, currentDir.x);
    Point rightDir(currentDir.y, -currentDir.x);

    Point leftPos(pos.x + leftDir.x, pos.y + leftDir.y);
    Point rightPos(pos.x + rightDir.x, pos.y + rightDir.y);

    bool leftOpen = (leftPos.x >= 0 && leftPos.x < mazeW && leftPos.y >= 0 && leftPos.y < mazeH && maze[leftPos.y][leftPos.x] != 1);
    bool rightOpen = (rightPos.x >= 0 && rightPos.x < mazeW && rightPos.y >= 0 && rightPos.y < mazeH && maze[rightPos.y][rightPos.x] != 1);

    Point frontPos(pos.x + currentDir.x, pos.y + currentDir.y);
    bool frontBlocked = (frontPos.x < 0 || frontPos.x >= mazeW || frontPos.y < 0 || frontPos.y >= mazeH || maze[frontPos.y][frontPos.x] == 1);

    return (leftOpen || rightOpen || frontBlocked);
}

// 統合16進数ログから全試合データを読み込み
void loadAllPlayerStyles() {
    FILE* fp = fopen(LOG_FILE_NAME, "r");
    if (!fp) return;

    UINT64 totalIntersections = 0;
    UINT64 totalWrongTurns = 0;
    double totalReactionTime = 0.0;

    char hexBuf[256];
    while (fgets(hexBuf, sizeof(hexBuf), fp)) {
        GameRecordBinary rec;
        unsigned char* pRec = (unsigned char*)&rec;
        int len = (int)strlen(hexBuf);

        for (int i = 0; i < (int)sizeof(GameRecordBinary) && (i * 2 + 1) < len; ++i) {
            unsigned int val = 0;
            sscanf(hexBuf + i * 2, "%02x", &val);
            pRec[i] = (unsigned char)val;
        }

        totalIntersections += rec.intersectionCount;
        totalWrongTurns += rec.wrongTurnsCount;
        totalReactionTime += rec.totalReactionTimeMs;
    }
    fclose(fp);

    if (totalIntersections > 0) {
        humanAvgReactionMs = totalReactionTime / totalIntersections;
        humanErrorRate = (double)totalWrongTurns / (double)totalIntersections;
        if (humanErrorRate > 0.6) humanErrorRate = 0.6;
    }
}

// 今回の試合結果を統合ログへ追記保存
void saveCurrentPlayerStyleHex() {
    GameRecordBinary rec;
    rec.totalSteps = (UINT32)p1Steps;
    rec.clearTimeMs = (UINT32)elapsedTime;
    rec.intersectionCount = (UINT32)currentIntersectionCount;
    rec.wrongTurnsCount = (UINT32)currentWrongTurnsCount;
    rec.totalReactionTimeMs = currentReactionTimeMs;

    FILE* fp = fopen(LOG_FILE_NAME, "a");
    if (!fp) return;

    unsigned char* pRec = (unsigned char*)&rec;
    for (size_t i = 0; i < sizeof(GameRecordBinary); ++i) {
        fprintf(fp, "%02X", pRec[i]);
    }
    fprintf(fp, "\n");
    fclose(fp);
}

void updateMazeSizeByLevel() {
    mazeW = 11 + (mazeLevel - 1) * 2 + 2; 
    if (mazeW % 2 == 0) mazeW++;

    mazeH = 9 + (mazeLevel - 1) * 2;
    if (mazeH % 2 == 0) mazeH++;

    goalPos = Point(mazeW - 2, mazeH - 2);
}

// 適度な難易度と視認性を重視したバランス型迷路生成
void generateMaze() {
    updateMazeSizeByLevel();
    maze.assign(mazeH, vector<int>(mazeW, 1));

    vector<Point> stack;
    maze[1][1] = 0;
    stack.push_back(Point(1, 1));

    int dx[] = {0, 0, 2, -2};
    int dy[] = {2, -2, 0, 0};

    while (!stack.empty()) {
        // 70%の確率で直近のパスを深掘り（見通しの良さを維持）、30%で分岐（適度な迷い要素）
        int idx = (rand() % 100 < 70) ? (int)stack.size() - 1 : rand() % stack.size();
        Point current = stack[idx];

        vector<int> neighbors;
        for (int i = 0; i < 4; ++i) {
            int nx = current.x + dx[i];
            int ny = current.y + dy[i];
            if (nx > 0 && nx < mazeW - 1 && ny > 0 && ny < mazeH - 1) {
                if (maze[ny][nx] == 1) {
                    neighbors.push_back(i);
                }
            }
        }

        if (!neighbors.empty()) {
            int dir = neighbors[rand() % neighbors.size()];
            int nx = current.x + dx[dir];
            int ny = current.y + dy[dir];

            maze[current.y + dy[dir] / 2][current.x + dx[dir] / 2] = 0;
            maze[ny][nx] = 0;
            stack.push_back(Point(nx, ny));
        } else {
            stack.erase(stack.begin() + idx);
        }
    }

    // わずかに1?2箇所の抜け道を作ることで行き止まり感を軽減
    int loopCount = 1 + (mazeLevel / 4);
    for (int i = 0; i < loopCount; ++i) {
        int rx = 1 + (rand() % (mazeW - 2));
        int ry = 1 + (rand() % (mazeH - 2));

        if (maze[ry][rx] == 1) {
            if (maze[ry - 1][rx] == 0 && maze[ry + 1][rx] == 0) {
                maze[ry][rx] = 0;
            } else if (maze[ry][rx - 1] == 0 && maze[ry][rx + 1] == 0) {
                maze[ry][rx] = 0;
            }
        }
    }

    maze[goalPos.y][goalPos.x] = 2;
}

void resizeWindowToFit(HWND hWnd) {
    int mazePixelWidth = mazeW * CELL_SIZE;
    int winWidth = MARGIN * 3 + mazePixelWidth * 2 + 32;
    if (winWidth < 540) winWidth = 540;
    int winHeight = TOP_PANEL + mazeH * CELL_SIZE + MARGIN + 70;

    SetWindowPos(hWnd, NULL, 0, 0, winWidth, winHeight, SWP_NOMOVE | SWP_NOZORDER);
}

void resetGame(HWND hWnd) {
    if (hComboMode) {
        int modeIdx = (int)SendMessage(hComboMode, CB_GETCURSEL, 0, 0);
        currentMode = (modeIdx == 1) ? MODE_VS_HUMAN : MODE_VS_AI;
    }
    if (hComboAI) {
        aiLevel = (int)SendMessage(hComboAI, CB_GETCURSEL, 0, 0) + 1;
    }
    if (hComboMaze) {
        mazeLevel = (int)SendMessage(hComboMaze, CB_GETCURSEL, 0, 0) + 1;
    }

    loadAllPlayerStyles();
    generateMaze();
    resizeWindowToFit(hWnd);

    p1Pos = Point(1, 1);
    p2Pos = Point(1, 1);
    p1Dir = Point(0, 0);
    p2Dir = Point(0, 0);
    p1IsMoving = false;
    p2IsMoving = false;

    aiLastPos = Point(1, 1);
    p1Steps = 0;
    p2Steps = 0;
    elapsedTime = 0;
    gameOver = false;
    winner = 0;

    currentIntersectionCount = 0;
    currentReactionTimeMs = 0.0;
    currentWrongTurnsCount = 0;

    stopStartTime = GetTickCount();
    isP1Stopped = true;
    aiWaitUntil = 0;

    KillTimer(hWnd, TIMER_AI);
    KillTimer(hWnd, TIMER_PLAYER);
    KillTimer(hWnd, TIMER_CLOCK);

    isCountingDown = true;
    countdownValue = 3;
    SetTimer(hWnd, TIMER_COUNTDOWN, 1000, NULL);

    InvalidateRect(hWnd, NULL, FALSE);
}

vector<Point> getShortestPath(Point start, Point goal) {
    vector<vector<Point> > parent(mazeH, vector<Point>(mazeW, Point(-1, -1)));
    vector<vector<bool> > visited(mazeH, vector<bool>(mazeW, false));
    queue<Point> q;

    q.push(start);
    visited[start.y][start.x] = true;

    int dx[] = {0, 0, 1, -1};
    int dy[] = {1, -1, 0, 0};

    bool found = false;
    while (!q.empty()) {
        Point curr = q.front();
        q.pop();

        if (curr == goal) {
            found = true;
            break;
        }

        for (int i = 0; i < 4; ++i) {
            int nx = curr.x + dx[i];
            int ny = curr.y + dy[i];

            if (nx >= 0 && nx < mazeW && ny >= 0 && ny < mazeH) {
                if (maze[ny][nx] != 1 && !visited[ny][nx]) {
                    visited[ny][nx] = true;
                    parent[ny][nx] = curr;
                    q.push(Point(nx, ny));
                }
            }
        }
    }

    vector<Point> path;
    if (found) {
        Point curr = goal;
        while (!(curr == start)) {
            path.push_back(curr);
            curr = parent[curr.y][curr.x];
        }
        reverse(path.begin(), path.end());
    }
    return path;
}

Point getNextAIMove() {
    DWORD now = GetTickCount();
    if (now < aiWaitUntil) {
        return p2Pos;
    }

    int dx[] = {0, 0, 1, -1};
    int dy[] = {1, -1, 0, 0};
    vector<Point> validMoves;

    for (int i = 0; i < 4; ++i) {
        int nx = p2Pos.x + dx[i];
        int ny = p2Pos.y + dy[i];
        if (nx >= 0 && nx < mazeW && ny >= 0 && ny < mazeH && maze[ny][nx] != 1) {
            validMoves.push_back(Point(nx, ny));
        }
    }

    if (validMoves.empty()) return p2Pos;

    if (canChangeDirection(p2Pos, p2Dir)) {
        double thinkingFactor = 1.0;
        if (aiLevel == 5) thinkingFactor = 1.0;
        else if (aiLevel < 5) thinkingFactor = 1.0 + (5 - aiLevel) * 0.3;
        else thinkingFactor = (10.0 - aiLevel) / 5.0;

        DWORD delayTime = (DWORD)(humanAvgReactionMs * thinkingFactor);
        if (delayTime > 0 && aiWaitUntil == 0) {
            aiWaitUntil = now + delayTime;
            return p2Pos;
        }
    }
    aiWaitUntil = 0;

    vector<Point> path = getShortestPath(p2Pos, goalPos);

    double currentMissChance = humanErrorRate * 100.0;
    if (aiLevel < 5) {
        currentMissChance += (5 - aiLevel) * 12.0;
    } else if (aiLevel > 5) {
        currentMissChance -= (aiLevel - 5) * (currentMissChance / 5.0);
    }
    if (currentMissChance < 0) currentMissChance = 0;

    if (!path.empty() && (rand() % 100 >= (int)currentMissChance)) {
        return path[0];
    }

    vector<Point> forwardMoves;
    for (size_t i = 0; i < validMoves.size(); ++i) {
        if (!(validMoves[i] == aiLastPos)) forwardMoves.push_back(validMoves[i]);
    }

    if (!forwardMoves.empty()) return forwardMoves[rand() % forwardMoves.size()];
    return validMoves[rand() % validMoves.size()];
}

void drawSingleMaze(HDC hdc, int offsetX, int offsetY, Point playerPos, COLORREF playerColor, int steps, const char* title) {
    int mazeWidthPx = mazeW * CELL_SIZE;
    int mazeHeightPx = mazeH * CELL_SIZE;

    HBRUSH cardBg = CreateSolidBrush(RGB(35, 41, 54));
    HPEN cardBorder = CreatePen(PS_SOLID, 1, RGB(55, 65, 85));
    SelectObject(hdc, cardBg);
    SelectObject(hdc, cardBorder);
    RoundRect(hdc, offsetX - 8, offsetY - 26, offsetX + mazeWidthPx + 8, offsetY + mazeHeightPx + 24, 12, 12);
    DeleteObject(cardBg);
    DeleteObject(cardBorder);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(220, 225, 235));
    TextOut(hdc, offsetX, offsetY - 22, title, strlen(title));

    char stepBuf[32];
    sprintf(stepBuf, "歩数: %d", steps);
    SetTextColor(hdc, RGB(140, 150, 170));
    TextOut(hdc, offsetX + mazeWidthPx - 65, offsetY - 22, stepBuf, strlen(stepBuf));

    for (int y = 0; y < mazeH; ++y) {
        for (int x = 0; x < mazeW; ++x) {
            HBRUSH hBrush;
            if (maze[y][x] == 1) {
                hBrush = CreateSolidBrush(RGB(20, 24, 32));
            } else if (maze[y][x] == 2) {
                hBrush = CreateSolidBrush(RGB(255, 193, 7));
            } else {
                hBrush = CreateSolidBrush(RGB(48, 56, 70));
            }

            RECT rect = {
                offsetX + x * CELL_SIZE,
                offsetY + y * CELL_SIZE,
                offsetX + (x + 1) * CELL_SIZE - 1,
                offsetY + (y + 1) * CELL_SIZE - 1
            };
            FillRect(hdc, &rect, hBrush);
            DeleteObject(hBrush);
        }
    }

    HBRUSH pBrush = CreateSolidBrush(playerColor);
    HPEN pPen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
    SelectObject(hdc, pBrush);
    SelectObject(hdc, pPen);

    int px1 = offsetX + playerPos.x * CELL_SIZE + 2;
    int py1 = offsetY + playerPos.y * CELL_SIZE + 2;
    int px2 = offsetX + (playerPos.x + 1) * CELL_SIZE - 2;
    int py2 = offsetY + (playerPos.y + 1) * CELL_SIZE - 2;

    RoundRect(hdc, px1, py1, px2, py2, 6, 6);

    DeleteObject(pBrush);
    DeleteObject(pPen);
}

void OnPaint(HWND hWnd) {
    PAINTSTRUCT ps;
    HDC hdcWindow = BeginPaint(hWnd, &ps);

    RECT winRect;
    GetClientRect(hWnd, &winRect);
    int width = winRect.right - winRect.left;
    int height = winRect.bottom - winRect.top;

    HDC hdcMem = CreateCompatibleDC(hdcWindow);
    HBITMAP hbmMem = CreateCompatibleBitmap(hdcWindow, width, height);
    HBITMAP hbmOld = (HBITMAP)SelectObject(hdcMem, hbmMem);

    HBRUSH bgBrush = CreateSolidBrush(RGB(24, 28, 36));
    FillRect(hdcMem, &winRect, bgBrush);
    DeleteObject(bgBrush);

    SetBkMode(hdcMem, TRANSPARENT);

    HFONT hFontMain = CreateFont(14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                 SHIFTJIS_CHARSET, OUT_DEFAULT_PRECIS,
                                 CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                 DEFAULT_PITCH | FF_DONTCARE, "ＭＳ ゴシック");
    HFONT hFontOld = (HFONT)SelectObject(hdcMem, hFontMain);

    HBRUSH headerBg = CreateSolidBrush(RGB(35, 41, 54));
    RECT headerRect = { MARGIN, 8, winRect.right - MARGIN, TOP_PANEL - 10 };
    FillRect(hdcMem, &headerRect, headerBg);
    DeleteObject(headerBg);

    SetTextColor(hdcMem, RGB(200, 210, 225));
    TextOut(hdcMem, MARGIN + 10, 18, "モード:", 7);
    TextOut(hdcMem, MARGIN + 145, 18, "AIレベル:", 9);
    TextOut(hdcMem, MARGIN + 265, 18, "迷路難易度:", 11);

    DWORD currentSec = (isCountingDown || gameOver) ? (elapsedTime / 1000) : ((GetTickCount() - startTime) / 1000);
    DWORD currentMs = (isCountingDown || gameOver) ? ((elapsedTime % 1000) / 100) : (((GetTickCount() - startTime) % 1000) / 100);

    char infoBuf[256];
    sprintf(infoBuf, "タイム: %lu.%lus   ", currentSec, currentMs);
    SetTextColor(hdcMem, RGB(0, 210, 255));
    TextOut(hdcMem, MARGIN + 10, 62, infoBuf, strlen(infoBuf));

    int mazePixelWidth = mazeW * CELL_SIZE;
    int leftOffsetX = MARGIN + 8;
    int rightOffsetX = MARGIN * 2 + mazePixelWidth + 8;
    int mazeOffsetY = TOP_PANEL + 26;

    drawSingleMaze(hdcMem, leftOffsetX, mazeOffsetY, p1Pos, RGB(0, 168, 255), p1Steps, "PLAYER 1 (左: WASD)");

    COLORREF rightColor = (currentMode == MODE_VS_AI) ? RGB(255, 71, 87) : RGB(46, 213, 115);
    const char* rightTitle = (currentMode == MODE_VS_AI) ? "AI BOT (右)" : "PLAYER 2 (右: 矢印)";
    drawSingleMaze(hdcMem, rightOffsetX, mazeOffsetY, p2Pos, rightColor, p2Steps, rightTitle);

    if (isCountingDown) {
        HFONT hFontCount = CreateFont(52, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                      SHIFTJIS_CHARSET, OUT_DEFAULT_PRECIS,
                                      CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                      DEFAULT_PITCH | FF_DONTCARE, "Arial");
        SelectObject(hdcMem, hFontCount);

        int dialogW = 260;
        int dialogH = 100;
        int dialogX = (winRect.right - dialogW) / 2;
        int dialogY = mazeOffsetY + (mazeH * CELL_SIZE) / 2 - dialogH / 2;

        HBRUSH dlgBg = CreateSolidBrush(RGB(20, 24, 32));
        HPEN dlgBorder = CreatePen(PS_SOLID, 2, RGB(0, 210, 255));
        SelectObject(hdcMem, dlgBg);
        SelectObject(hdcMem, dlgBorder);

        RoundRect(hdcMem, dialogX, dialogY, dialogX + dialogW, dialogY + dialogH, 16, 16);

        char countStr[32];
        if (countdownValue > 0) {
            sprintf(countStr, "%d", countdownValue);
            SetTextColor(hdcMem, RGB(255, 211, 42));
        } else {
            sprintf(countStr, "READY!");
            SetTextColor(hdcMem, RGB(46, 213, 115));
        }

        RECT textRect = { dialogX, dialogY + 18, dialogX + dialogW, dialogY + 80 };
        DrawText(hdcMem, countStr, -1, &textRect, DT_CENTER);

        DeleteObject(hFontCount);
        DeleteObject(dlgBg);
        DeleteObject(dlgBorder);
    }

    if (gameOver) {
        HFONT hFontWin = CreateFont(26, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                    SHIFTJIS_CHARSET, OUT_DEFAULT_PRECIS,
                                    CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                    DEFAULT_PITCH | FF_DONTCARE, "ＭＳ ゴシック");
        SelectObject(hdcMem, hFontWin);

        int dialogW = 380;
        int dialogH = 70;
        int dialogX = (winRect.right - dialogW) / 2;
        int dialogY = mazeOffsetY + (mazeH * CELL_SIZE) / 2 - dialogH / 2;

        HBRUSH dlgBg = CreateSolidBrush(RGB(20, 24, 32));
        HPEN dlgBorder = CreatePen(PS_SOLID, 2, (winner == 1) ? RGB(0, 168, 255) : RGB(255, 71, 87));
        SelectObject(hdcMem, dlgBg);
        SelectObject(hdcMem, dlgBorder);

        RoundRect(hdcMem, dialogX, dialogY, dialogX + dialogW, dialogY + dialogH, 16, 16);

        char resultStr[128];
        if (winner == 1) {
            sprintf(resultStr, "PLAYER 1 の勝利！");
            SetTextColor(hdcMem, RGB(0, 168, 255));
        } else {
            if (currentMode == MODE_VS_AI) {
                sprintf(resultStr, "AI (Lv.%d) の勝利！", aiLevel);
            } else {
                sprintf(resultStr, "PLAYER 2 の勝利！");
            }
            SetTextColor(hdcMem, RGB(255, 71, 87));
        }

        RECT textRect = { dialogX, dialogY + 12, dialogX + dialogW, dialogY + 40 };
        DrawText(hdcMem, resultStr, -1, &textRect, DT_CENTER);

        HFONT hFontSub = CreateFont(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    SHIFTJIS_CHARSET, OUT_DEFAULT_PRECIS,
                                    CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                    DEFAULT_PITCH | FF_DONTCARE, "ＭＳ ゴシック");
        SelectObject(hdcMem, hFontSub);
        SetTextColor(hdcMem, RGB(180, 190, 205));

        RECT subTextRect = { dialogX, dialogY + 42, dialogX + dialogW, dialogY + 65 };
        DrawText(hdcMem, "おつかれさまでした！\nもういっかいあそぶ？", -1, &subTextRect, DT_CENTER);

        DeleteObject(hFontSub);
        DeleteObject(hFontWin);
        DeleteObject(dlgBg);
        DeleteObject(dlgBorder);
    }

    SelectObject(hdcMem, hFontOld);
    DeleteObject(hFontMain);

    BitBlt(hdcWindow, 0, 0, width, height, hdcMem, 0, 0, SRCCOPY);

    SelectObject(hdcMem, hbmOld);
    DeleteObject(hbmMem);
    DeleteDC(hdcMem);

    EndPaint(hWnd, &ps);
}

void processPlayerStep(HWND hWnd) {
    if (gameOver || isCountingDown) return;

    bool moved = false;

    if (p1IsMoving) {
        Point targetP1(p1Pos.x + p1Dir.x, p1Pos.y + p1Dir.y);

        if (maze[targetP1.y][targetP1.x] != 1) {
            p1Pos = targetP1;
            p1Steps++;
            moved = true;

            if (canChangeDirection(p1Pos, p1Dir)) {
                p1IsMoving = false;
                p1Dir = Point(0, 0);

                isP1Stopped = true;
                stopStartTime = GetTickCount();
            }
        } else {
            p1IsMoving = false;
            p1Dir = Point(0, 0);

            isP1Stopped = true;
            stopStartTime = GetTickCount();
        }
    }

    if (p1Pos == goalPos) {
        gameOver = true;
        winner = 1;
        elapsedTime = GetTickCount() - startTime;
        saveCurrentPlayerStyleHex();

        KillTimer(hWnd, TIMER_AI);
        KillTimer(hWnd, TIMER_PLAYER);
        KillTimer(hWnd, TIMER_CLOCK);
    }

    if (currentMode == MODE_VS_HUMAN && p2IsMoving) {
        Point targetP2(p2Pos.x + p2Dir.x, p2Pos.y + p2Dir.y);

        if (maze[targetP2.y][targetP2.x] != 1) {
            p2Pos = targetP2;
            p2Steps++;
            moved = true;

            if (canChangeDirection(p2Pos, p2Dir)) {
                p2IsMoving = false;
                p2Dir = Point(0, 0);
            }
        } else {
            p2IsMoving = false;
            p2Dir = Point(0, 0);
        }

        if (p2Pos == goalPos) {
            gameOver = true;
            winner = 2;
            elapsedTime = GetTickCount() - startTime;
            saveCurrentPlayerStyleHex();

            KillTimer(hWnd, TIMER_AI);
            KillTimer(hWnd, TIMER_PLAYER);
            KillTimer(hWnd, TIMER_CLOCK);
        }
    }

    if (moved) {
        InvalidateRect(hWnd, NULL, FALSE);
    }
}

void handleKeyPress(WPARAM key) {
    if (gameOver || isCountingDown) return;

    Point newDir(0, 0);
    if (key == 'W' || key == 'w') newDir = Point(0, -1);
    else if (key == 'S' || key == 's') newDir = Point(0, 1);
    else if (key == 'A' || key == 'a') newDir = Point(-1, 0);
    else if (key == 'D' || key == 'd') newDir = Point(1, 0);

    if (newDir.x != 0 || newDir.y != 0) {
        Point target(p1Pos.x + newDir.x, p1Pos.y + newDir.y);

        if (maze[target.y][target.x] != 1) {
            if (isP1Stopped) {
                DWORD now = GetTickCount();
                double reactionMs = (double)(now - stopStartTime);
                currentReactionTimeMs += reactionMs;
                currentIntersectionCount++;
                isP1Stopped = false;
            }

            p1Dir = newDir;
            p1IsMoving = true;
        } else {
            currentWrongTurnsCount++;
        }
    }

    if (currentMode == MODE_VS_HUMAN) {
        Point newDirP2(0, 0);
        if (key == VK_UP) newDirP2 = Point(0, -1);
        else if (key == VK_DOWN) newDirP2 = Point(0, 1);
        else if (key == VK_LEFT) newDirP2 = Point(-1, 0);
        else if (key == VK_RIGHT) newDirP2 = Point(1, 0);

        if (newDirP2.x != 0 || newDirP2.y != 0) {
            Point targetP2(p2Pos.x + newDirP2.x, p2Pos.y + newDirP2.y);
            if (maze[targetP2.y][targetP2.x] != 1) {
                p2Dir = newDirP2;
                p2IsMoving = true;
            }
        }
    }
}

void createUIControls(HWND hWnd, HINSTANCE hInstance) {
    hComboMode = CreateWindow("COMBOBOX", NULL, CBS_DROPDOWNLIST | WS_CHILD | WS_VISIBLE | WS_VSCROLL,
                              MARGIN + 55, 14, 80, 120, hWnd, (HMENU)IDC_COMBO_MODE, hInstance, NULL);
    SendMessage(hComboMode, CB_ADDSTRING, 0, (LPARAM)"VS AI");
    SendMessage(hComboMode, CB_ADDSTRING, 0, (LPARAM)"VS HUMAN");
    SendMessage(hComboMode, CB_SETCURSEL, 0, 0);

    hComboAI = CreateWindow("COMBOBOX", NULL, CBS_DROPDOWNLIST | WS_CHILD | WS_VISIBLE | WS_VSCROLL,
                            MARGIN + 215, 14, 45, 200, hWnd, (HMENU)IDC_COMBO_AI, hInstance, NULL);
    for (int i = 1; i <= 10; ++i) {
        char buf[16];
        sprintf(buf, "%d", i);
        SendMessage(hComboAI, CB_ADDSTRING, 0, (LPARAM)buf);
    }
    SendMessage(hComboAI, CB_SETCURSEL, 4, 0);

    hComboMaze = CreateWindow("COMBOBOX", NULL, CBS_DROPDOWNLIST | WS_CHILD | WS_VISIBLE | WS_VSCROLL,
                              MARGIN + 350, 14, 45, 200, hWnd, (HMENU)IDC_COMBO_MAZE, hInstance, NULL);
    for (int i = 1; i <= 10; ++i) {
        char buf[16];
        sprintf(buf, "%d", i);
        SendMessage(hComboMaze, CB_ADDSTRING, 0, (LPARAM)buf);
    }
    SendMessage(hComboMaze, CB_SETCURSEL, 4, 0);

    hBtnStart = CreateWindow("BUTTON", "ゲーム開始", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
                             MARGIN + 410, 13, 90, 25, hWnd, (HMENU)IDC_BTN_START, hInstance, NULL);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        createUIControls(hWnd, ((LPCREATESTRUCT)lParam)->hInstance);
        resetGame(hWnd);
        break;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_BTN_START) {
            resetGame(hWnd);
            SetFocus(hWnd);
        }
        break;

    case WM_ERASEBKGND:
        return 1;

    case WM_KEYDOWN:
        handleKeyPress(wParam);
        break;

    case WM_TIMER:
        if (wParam == TIMER_COUNTDOWN) {
            countdownValue--;
            if (countdownValue < 0) {
                KillTimer(hWnd, TIMER_COUNTDOWN);
                isCountingDown = false;
                startTime = GetTickCount();
                stopStartTime = startTime;

                SetTimer(hWnd, TIMER_AI, MOVE_SPEED_DELAY, NULL);
                SetTimer(hWnd, TIMER_PLAYER, MOVE_SPEED_DELAY, NULL);
                SetTimer(hWnd, TIMER_CLOCK, 100, NULL);
            }
            InvalidateRect(hWnd, NULL, FALSE);
        }
        else if (wParam == TIMER_AI && currentMode == MODE_VS_AI && !gameOver && !isCountingDown) {
            Point nextAI = getNextAIMove();
            if (!(nextAI == p2Pos)) {
                aiLastPos = p2Pos;
                p2Pos = nextAI;
                p2Steps++;
            }

            if (p2Pos == goalPos) {
                gameOver = true;
                winner = 2;
                elapsedTime = GetTickCount() - startTime;
                saveCurrentPlayerStyleHex();

                KillTimer(hWnd, TIMER_AI);
                KillTimer(hWnd, TIMER_PLAYER);
                KillTimer(hWnd, TIMER_CLOCK);
            }
            InvalidateRect(hWnd, NULL, FALSE);
        }
        else if (wParam == TIMER_PLAYER && !gameOver && !isCountingDown) {
            processPlayerStep(hWnd);
        }
        else if (wParam == TIMER_CLOCK && !gameOver && !isCountingDown) {
            InvalidateRect(hWnd, NULL, FALSE);
        }
        break;

    case WM_PAINT:
        OnPaint(hWnd);
        break;

    case WM_DESTROY:
        KillTimer(hWnd, TIMER_AI);
        KillTimer(hWnd, TIMER_PLAYER);
        KillTimer(hWnd, TIMER_CLOCK);
        KillTimer(hWnd, TIMER_COUNTDOWN);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    srand((unsigned int)time(NULL));

    const char* className = "BalancedMazeRaceJP";
    WNDCLASS wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = NULL;
    wc.lpszClassName = className;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);

    RegisterClass(&wc);

    HWND hWnd = CreateWindow(
        className, "対戦迷路ゲーム",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        NULL, NULL, hInstance, NULL
    );

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}
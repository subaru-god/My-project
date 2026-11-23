#include <windows.h>
#include <vector>
#include <queue>
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <cstdio>
#include <cmath>

using namespace std;

// ==========================================
// コントロールID ＆ 定数定義
// ==========================================
const UINT IDC_COMBO_MODE  = 101;
const UINT IDC_COMBO_AI    = 102;
const UINT IDC_COMBO_MAZE  = 103;
const UINT IDC_CHK_FOG     = 104;
const UINT IDC_BTN_START   = 105;

const int CELL_SIZE = 18;   // 1マスのサイズ(px)
const int MARGIN = 20;      // 外枠余白
const int TOP_PANEL = 100;  // ヘッダーエリア高さ

const UINT_PTR TIMER_AI        = 1;
const UINT_PTR TIMER_PLAYER    = 2;
const UINT_PTR TIMER_CLOCK     = 3;
const UINT_PTR TIMER_COUNTDOWN = 4;

const int MOVE_SPEED_DELAY = 80; // 1マスあたりの移動速度(ms)
const char* LOG_FILE_NAME = "player_style.hex";

// ==========================================
// 構造体・列挙型定義
// ==========================================
enum GameMode {
    MODE_VS_AI = 0,
    MODE_VS_HUMAN = 1
};

struct Point {
    int x;
    int y;

    Point(int _x = 0, int _y = 0) : x(_x), y(_y) {}

    bool operator==(const Point& other) const {
        return x == other.x && y == other.y;
    }
    bool operator!=(const Point& other) const {
        return !(*this == other);
    }
};

// 拡張バイナリログデータ (32バイト固定)
#pragma pack(push, 1)
struct GameRecordBinary {
    DWORD totalSteps;
    DWORD clearTimeMs;
    DWORD intersectionCount;
    DWORD wrongTurnsCount;
    double totalReactionTimeMs;
    double pathEfficiencyRatio; // 最短歩数 / 実際の歩数 (効率性)

    GameRecordBinary() 
        : totalSteps(0), clearTimeMs(0), intersectionCount(0), wrongTurnsCount(0), 
          totalReactionTimeMs(0.0), pathEfficiencyRatio(1.0) {}
};
#pragma pack(pop)

// ==========================================
// プレイヤーデータ＆ログ管理クラス
// ==========================================
class PlayerStyleTracker {
public:
    double avgReactionMs;
    double errorRate;
    double avgEfficiency; // 最短経路に対する移動効率

    PlayerStyleTracker() : avgReactionMs(350.0), errorRate(0.12), avgEfficiency(0.85) {}

    void loadStyles(const char* filename) {
        FILE* fp = fopen(filename, "r");
        if (!fp) return;

        DWORD totalIntersections = 0;
        DWORD totalWrongTurns = 0;
        double totalReactionTime = 0.0;
        double totalEfficiency = 0.0;
        int recordCount = 0;

        char hexBuf[256];
        while (fgets(hexBuf, sizeof(hexBuf), fp)) {
            GameRecordBinary rec;
            BYTE* pRec = (BYTE*)&rec;
            int len = (int)strlen(hexBuf);

            for (size_t i = 0; i < sizeof(GameRecordBinary) && (i * 2 + 1) < (size_t)len; ++i) {
                unsigned int val = 0;
                sscanf(hexBuf + i * 2, "%02x", &val);
                pRec[i] = (BYTE)val;
            }

            totalIntersections += rec.intersectionCount;
            totalWrongTurns += rec.wrongTurnsCount;
            totalReactionTime += rec.totalReactionTimeMs;
            totalEfficiency += rec.pathEfficiencyRatio;
            recordCount++;
        }
        fclose(fp);

        if (totalIntersections > 0) {
            avgReactionMs = totalReactionTime / (double)totalIntersections;
            errorRate = (double)totalWrongTurns / (double)totalIntersections;
            if (errorRate > 0.5) errorRate = 0.5;
        }
        if (recordCount > 0) {
            avgEfficiency = totalEfficiency / (double)recordCount;
            if (avgEfficiency < 0.4) avgEfficiency = 0.4;
            if (avgEfficiency > 1.0) avgEfficiency = 1.0;
        }
    }

    void saveCurrentStyle(const char* filename, const GameRecordBinary& rec) const {
        FILE* fp = fopen(filename, "a");
        if (!fp) return;

        const BYTE* pRec = (const BYTE*)&rec;
        for (size_t i = 0; i < sizeof(GameRecordBinary); ++i) {
            fprintf(fp, "%02X", pRec[i]);
        }
        fprintf(fp, "\n");
        fclose(fp);
    }
};

// ==========================================
// 迷路データ＆アルゴリズム管理クラス
// ==========================================
class Maze {
public:
    int width;
    int height;
    vector<vector<int> > grid;
    Point goalPos;

    Maze() : width(21), height(15), goalPos(1, 1) {}

    void updateSizeByLevel(int level) {
        width = 11 + (level - 1) * 2 + 2;
        if (width % 2 == 0) width++;

        height = 9 + (level - 1) * 2;
        if (height % 2 == 0) height++;

        goalPos = Point(width - 2, height - 2);
    }

    void generate(int level) {
        updateSizeByLevel(level);
        grid.assign(height, vector<int>(width, 1));

        vector<Point> stack;
        grid[1][1] = 0;
        stack.push_back(Point(1, 1));

        int dx[] = { 0, 0, 2, -2 };
        int dy[] = { 2, -2, 0, 0 };

        while (!stack.empty()) {
            size_t idx = (rand() % 100 < 70) ? stack.size() - 1 : rand() % stack.size();
            Point current = stack[idx];

            vector<int> neighbors;
            for (int i = 0; i < 4; ++i) {
                int nx = current.x + dx[i];
                int ny = current.y + dy[i];
                if (nx > 0 && nx < width - 1 && ny > 0 && ny < height - 1) {
                    if (grid[ny][nx] == 1) {
                        neighbors.push_back(i);
                    }
                }
            }

            if (!neighbors.empty()) {
                int dir = neighbors[rand() % neighbors.size()];
                int nx = current.x + dx[dir];
                int ny = current.y + dy[dir];

                grid[current.y + dy[dir] / 2][current.x + dx[dir] / 2] = 0;
                grid[ny][nx] = 0;
                stack.push_back(Point(nx, ny));
            } else {
                stack.erase(stack.begin() + idx);
            }
        }

        int loopCount = 1 + (level / 4);
        for (int i = 0; i < loopCount; ++i) {
            int rx = 1 + (rand() % (width - 2));
            int ry = 1 + (rand() % (height - 2));

            if (grid[ry][rx] == 1) {
                if (grid[ry - 1][rx] == 0 && grid[ry + 1][rx] == 0) {
                    grid[ry][rx] = 0;
                } else if (grid[ry][rx - 1] == 0 && grid[ry][rx + 1] == 0) {
                    grid[ry][rx] = 0;
                }
            }
        }

        grid[goalPos.y][goalPos.x] = 2;
    }

    bool isWall(int x, int y) const {
        if (x < 0 || x >= width || y < 0 || y >= height) return true;
        return grid[y][x] == 1;
    }

    bool canChangeDirection(Point pos, Point currentDir) const {
        if (currentDir.x == 0 && currentDir.y == 0) return true;

        Point leftDir(-currentDir.y, currentDir.x);
        Point rightDir(currentDir.y, -currentDir.x);

        bool leftOpen = !isWall(pos.x + leftDir.x, pos.y + leftDir.y);
        bool rightOpen = !isWall(pos.x + rightDir.x, pos.y + rightDir.y);
        bool frontBlocked = isWall(pos.x + currentDir.x, pos.y + currentDir.y);

        return (leftOpen || rightOpen || frontBlocked);
    }

    vector<Point> getShortestPath(Point start, Point target) const {
        vector<vector<Point> > parent(height, vector<Point>(width, Point(-1, -1)));
        vector<vector<bool> > visited(height, vector<bool>(width, false));
        queue<Point> q;

        q.push(start);
        visited[start.y][start.x] = true;

        int dx[] = { 0, 0, 1, -1 };
        int dy[] = { 1, -1, 0, 0 };

        bool found = false;
        while (!q.empty()) {
            Point curr = q.front();
            q.pop();

            if (curr == target) {
                found = true;
                break;
            }

            for (int i = 0; i < 4; ++i) {
                int nx = curr.x + dx[i];
                int ny = curr.y + dy[i];

                if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
                    if (grid[ny][nx] != 1 && !visited[ny][nx]) {
                        visited[ny][nx] = true;
                        parent[ny][nx] = curr;
                        q.push(Point(nx, ny));
                    }
                }
            }
        }

        vector<Point> path;
        if (found) {
            Point curr = target;
            while (curr != start) {
                path.push_back(curr);
                curr = parent[curr.y][curr.x];
            }
            reverse(path.begin(), path.end());
        }
        return path;
    }
};

// ==========================================
// グローバル状態
// ==========================================
Maze g_maze;
PlayerStyleTracker g_tracker;

Point g_p1Pos(1, 1);
Point g_p2Pos(1, 1);
Point g_aiLastPos(1, 1);

Point g_p1Dir(0, 0);
bool g_p1IsMoving = false;

Point g_p2Dir(0, 0);
bool g_p2IsMoving = false;

int g_p1Steps = 0;
int g_p2Steps = 0;

DWORD g_startTime = 0;
DWORD g_elapsedTime = 0;

GameMode g_currentMode = MODE_VS_AI;
int g_aiLevel = 5;
int g_mazeLevel = 5;
bool g_enableFog = false; // 視界制限フラグ
bool g_gameOver = false;
int g_winner = 0;

bool g_isCountingDown = false;
int g_countdownValue = 3;

DWORD g_stopStartTime = 0;
bool g_isP1Stopped = true;
int g_currentIntersectionCount = 0;
double g_currentReactionTimeMs = 0.0;
int g_currentWrongTurnsCount = 0;

DWORD g_aiWaitUntil = 0;

// UI ハンドル
HWND g_hComboMode = NULL;
HWND g_hComboAI   = NULL;
HWND g_hComboMaze = NULL;
HWND g_hChkFog    = NULL;
HWND g_hBtnStart  = NULL;

// ==========================================
// AI ロジック (Lv.5 = プレイヤー同等モデル)
// ==========================================
Point getNextAIMove() {
    DWORD now = GetTickCount();
    if (now < g_aiWaitUntil) {
        return g_p2Pos;
    }

    int dx[] = { 0, 0, 1, -1 };
    int dy[] = { 1, -1, 0, 0 };
    vector<Point> validMoves;

    for (int i = 0; i < 4; ++i) {
        int nx = g_p2Pos.x + dx[i];
        int ny = g_p2Pos.y + dy[i];
        if (!g_maze.isWall(nx, ny)) {
            validMoves.push_back(Point(nx, ny));
        }
    }

    if (validMoves.empty()) return g_p2Pos;

    // 分岐点での思考ディレイの再現
    if (g_maze.canChangeDirection(g_p2Pos, g_p2Dir)) {
        double thinkingFactor = 1.0;
        if (g_aiLevel == 5) {
            thinkingFactor = 1.0; // プレイヤーの思考時間そのまま
        } else if (g_aiLevel < 5) {
            thinkingFactor = 1.0 + (5 - g_aiLevel) * 0.35; // 遅くなる
        } else {
            thinkingFactor = (10.0 - g_aiLevel) / 5.0; // 高速化
        }

        DWORD delayTime = (DWORD)(g_tracker.avgReactionMs * thinkingFactor);
        if (delayTime > 0 && g_aiWaitUntil == 0) {
            g_aiWaitUntil = now + delayTime;
            return g_p2Pos;
        }
    }
    g_aiWaitUntil = 0;

    vector<Point> path = g_maze.getShortestPath(g_p2Pos, g_maze.goalPos);

    // エラー率（曲がり角での判断ミス・遠回り）の調整
    double missChance = g_tracker.errorRate * 100.0;
    if (g_aiLevel < 5) {
        missChance += (5 - g_aiLevel) * 12.0;
    } else if (g_aiLevel > 5) {
        missChance -= (g_aiLevel - 5) * (missChance / 5.0);
    }

    // 移動効率（無駄な回り道の再現）
    if (g_aiLevel == 5 && (rand() % 100 > (int)(g_tracker.avgEfficiency * 100.0))) {
        missChance += 15.0; 
    }

    if (missChance < 0) missChance = 0;

    // 最適解を選択
    if (!path.empty() && (rand() % 100 >= (int)missChance)) {
        return path[0];
    }

    // 迷った場合の移動（後退回避）
    vector<Point> forwardMoves;
    for (size_t i = 0; i < validMoves.size(); ++i) {
        if (validMoves[i] != g_aiLastPos) forwardMoves.push_back(validMoves[i]);
    }

    if (!forwardMoves.empty()) return forwardMoves[rand() % forwardMoves.size()];
    return validMoves[rand() % validMoves.size()];
}

// ==========================================
// ゲーム制御関数
// ==========================================
void saveRecord() {
    GameRecordBinary rec;
    rec.totalSteps = (DWORD)g_p1Steps;
    rec.clearTimeMs = (DWORD)g_elapsedTime;
    rec.intersectionCount = (DWORD)g_currentIntersectionCount;
    rec.wrongTurnsCount = (DWORD)g_currentWrongTurnsCount;
    rec.totalReactionTimeMs = g_currentReactionTimeMs;

    // 最短経路ステップ数の算出
    vector<Point> minPath = g_maze.getShortestPath(Point(1, 1), g_maze.goalPos);
    if (g_p1Steps > 0 && !minPath.empty()) {
        rec.pathEfficiencyRatio = (double)minPath.size() / (double)g_p1Steps;
    } else {
        rec.pathEfficiencyRatio = 1.0;
    }

    g_tracker.saveCurrentStyle(LOG_FILE_NAME, rec);
}

void resizeWindowToFit(HWND hWnd) {
    int mazePixelWidth = g_maze.width * CELL_SIZE;
    int winWidth = MARGIN * 3 + mazePixelWidth * 2 + 32;
    if (winWidth < 620) winWidth = 620;
    int winHeight = TOP_PANEL + g_maze.height * CELL_SIZE + MARGIN + 70;

    SetWindowPos(hWnd, NULL, 0, 0, winWidth, winHeight, SWP_NOMOVE | SWP_NOZORDER);
}

void resetGame(HWND hWnd) {
    if (g_hComboMode) {
        int modeIdx = (int)SendMessage(g_hComboMode, CB_GETCURSEL, 0, 0);
        g_currentMode = (modeIdx == 1) ? MODE_VS_HUMAN : MODE_VS_AI;
    }
    if (g_hComboAI) {
        g_aiLevel = (int)SendMessage(g_hComboAI, CB_GETCURSEL, 0, 0) + 1;
    }
    if (g_hComboMaze) {
        g_mazeLevel = (int)SendMessage(g_hComboMaze, CB_GETCURSEL, 0, 0) + 1;
    }
    if (g_hChkFog) {
        g_enableFog = (SendMessage(g_hChkFog, BM_GETCHECK, 0, 0) == BST_CHECKED);
    }

    g_tracker.loadStyles(LOG_FILE_NAME);
    g_maze.generate(g_mazeLevel);
    resizeWindowToFit(hWnd);

    g_p1Pos = Point(1, 1);
    g_p2Pos = Point(1, 1);
    g_p1Dir = Point(0, 0);
    g_p2Dir = Point(0, 0);
    g_p1IsMoving = false;
    g_p2IsMoving = false;

    g_aiLastPos = Point(1, 1);
    g_p1Steps = 0;
    g_p2Steps = 0;
    g_elapsedTime = 0;
    g_gameOver = false;
    g_winner = 0;

    g_currentIntersectionCount = 0;
    g_currentReactionTimeMs = 0.0;
    g_currentWrongTurnsCount = 0;

    g_stopStartTime = GetTickCount();
    g_isP1Stopped = true;
    g_aiWaitUntil = 0;

    KillTimer(hWnd, TIMER_AI);
    KillTimer(hWnd, TIMER_PLAYER);
    KillTimer(hWnd, TIMER_CLOCK);

    g_isCountingDown = true;
    g_countdownValue = 3;
    SetTimer(hWnd, TIMER_COUNTDOWN, 1000, NULL);

    InvalidateRect(hWnd, NULL, FALSE);
}

// ==========================================
// 描画ロジック (視界制限対応)
// ==========================================
void drawSingleMaze(HDC hdc, int offsetX, int offsetY, Point playerPos, COLORREF playerColor, int steps, const char* title) {
    int mazeWidthPx = g_maze.width * CELL_SIZE;
    int mazeHeightPx = g_maze.height * CELL_SIZE;

    HBRUSH cardBg = CreateSolidBrush(RGB(35, 41, 54));
    HPEN cardBorder = CreatePen(PS_SOLID, 1, RGB(55, 65, 85));
    SelectObject(hdc, cardBg);
    SelectObject(hdc, cardBorder);
    RoundRect(hdc, offsetX - 8, offsetY - 26, offsetX + mazeWidthPx + 8, offsetY + mazeHeightPx + 24, 12, 12);
    DeleteObject(cardBg);
    DeleteObject(cardBorder);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(220, 225, 235));
    TextOut(hdc, offsetX, offsetY - 22, title, (int)strlen(title));

    char stepBuf[32];
    sprintf(stepBuf, "歩数: %d", steps);
    SetTextColor(hdc, RGB(140, 150, 170));
    TextOut(hdc, offsetX + mazeWidthPx - 65, offsetY - 22, stepBuf, (int)strlen(stepBuf));

    for (int y = 0; y < g_maze.height; ++y) {
        for (int x = 0; x < g_maze.width; ++x) {
            // 視界制限判定: 半径2マス圏内（距離 2.5 未満）かどうか
            bool inSight = true;
            if (g_enableFog) {
                double dist = sqrt(pow((double)(x - playerPos.x), 2.0) + pow((double)(y - playerPos.y), 2.0));
                if (dist > 2.5) {
                    inSight = false;
                }
            }

            HBRUSH hBrush;
            if (!inSight) {
                hBrush = CreateSolidBrush(RGB(15, 17, 22)); // 暗闇
            } else if (g_maze.grid[y][x] == 1) {
                hBrush = CreateSolidBrush(RGB(20, 24, 32)); // 壁
            } else if (g_maze.grid[y][x] == 2) {
                hBrush = CreateSolidBrush(RGB(255, 193, 7)); // ゴール
            } else {
                hBrush = CreateSolidBrush(RGB(48, 56, 70)); // 通路
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

    // プレイヤー描画
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

    DWORD nowMs = GetTickCount();
    DWORD currentSec = (g_isCountingDown || g_gameOver) ? (g_elapsedTime / 1000) : ((nowMs - g_startTime) / 1000);
    DWORD currentMs = (g_isCountingDown || g_gameOver) ? ((g_elapsedTime % 1000) / 100) : (((nowMs - g_startTime) % 1000) / 100);

    char infoBuf[256];
    sprintf(infoBuf, "タイム: %lu.%lus   (AI同等率: %.0f%%)", currentSec, currentMs, g_tracker.avgEfficiency * 100.0);
    SetTextColor(hdcMem, RGB(0, 210, 255));
    TextOut(hdcMem, MARGIN + 10, 62, infoBuf, (int)strlen(infoBuf));

    int mazePixelWidth = g_maze.width * CELL_SIZE;
    int leftOffsetX = MARGIN + 8;
    int rightOffsetX = MARGIN * 2 + mazePixelWidth + 8;
    int mazeOffsetY = TOP_PANEL + 26;

    drawSingleMaze(hdcMem, leftOffsetX, mazeOffsetY, g_p1Pos, RGB(0, 168, 255), g_p1Steps, "PLAYER 1 (左: WASD)");

    COLORREF rightColor = (g_currentMode == MODE_VS_AI) ? RGB(255, 71, 87) : RGB(46, 213, 115);
    const char* rightTitle = (g_currentMode == MODE_VS_AI) ? "AI BOT (右)" : "PLAYER 2 (右: 矢印)";
    drawSingleMaze(hdcMem, rightOffsetX, mazeOffsetY, g_p2Pos, rightColor, g_p2Steps, rightTitle);

    if (g_isCountingDown) {
        HFONT hFontCount = CreateFont(52, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                      SHIFTJIS_CHARSET, OUT_DEFAULT_PRECIS,
                                      CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                      DEFAULT_PITCH | FF_DONTCARE, "Arial");
        SelectObject(hdcMem, hFontCount);

        int dialogW = 260;
        int dialogH = 100;
        int dialogX = (winRect.right - dialogW) / 2;
        int dialogY = mazeOffsetY + (g_maze.height * CELL_SIZE) / 2 - dialogH / 2;

        HBRUSH dlgBg = CreateSolidBrush(RGB(20, 24, 32));
        HPEN dlgBorder = CreatePen(PS_SOLID, 2, RGB(0, 210, 255));
        SelectObject(hdcMem, dlgBg);
        SelectObject(hdcMem, dlgBorder);

        RoundRect(hdcMem, dialogX, dialogY, dialogX + dialogW, dialogY + dialogH, 16, 16);

        char countStr[32];
        if (g_countdownValue > 0) {
            sprintf(countStr, "%d", g_countdownValue);
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

    if (g_gameOver) {
        HFONT hFontWin = CreateFont(26, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                    SHIFTJIS_CHARSET, OUT_DEFAULT_PRECIS,
                                    CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                    DEFAULT_PITCH | FF_DONTCARE, "ＭＳ ゴシック");
        SelectObject(hdcMem, hFontWin);

        int dialogW = 380;
        int dialogH = 70;
        int dialogX = (winRect.right - dialogW) / 2;
        int dialogY = mazeOffsetY + (g_maze.height * CELL_SIZE) / 2 - dialogH / 2;

        HBRUSH dlgBg = CreateSolidBrush(RGB(20, 24, 32));
        HPEN dlgBorder = CreatePen(PS_SOLID, 2, (g_winner == 1) ? RGB(0, 168, 255) : RGB(255, 71, 87));
        SelectObject(hdcMem, dlgBg);
        SelectObject(hdcMem, dlgBorder);

        RoundRect(hdcMem, dialogX, dialogY, dialogX + dialogW, dialogY + dialogH, 16, 16);

        char resultStr[128];
        if (g_winner == 1) {
            sprintf(resultStr, "PLAYER 1 の勝利！");
            SetTextColor(hdcMem, RGB(0, 168, 255));
        } else {
            if (g_currentMode == MODE_VS_AI) {
                sprintf(resultStr, "AI (Lv.%d) の勝利！", g_aiLevel);
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
        DrawText(hdcMem, "おつかれさでした！\nもういっかいあそぶ？", -1, &subTextRect, DT_CENTER);

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

// ==========================================
// イベント更新・キー入力
// ==========================================
void processPlayerStep(HWND hWnd) {
    if (g_gameOver || g_isCountingDown) return;

    bool moved = false;

    if (g_p1IsMoving) {
        Point targetP1(g_p1Pos.x + g_p1Dir.x, g_p1Pos.y + g_p1Dir.y);

        if (!g_maze.isWall(targetP1.x, targetP1.y)) {
            g_p1Pos = targetP1;
            g_p1Steps++;
            moved = true;

            if (g_maze.canChangeDirection(g_p1Pos, g_p1Dir)) {
                g_p1IsMoving = false;
                g_p1Dir = Point(0, 0);

                g_isP1Stopped = true;
                g_stopStartTime = GetTickCount();
            }
        } else {
            g_p1IsMoving = false;
            g_p1Dir = Point(0, 0);

            g_isP1Stopped = true;
            g_stopStartTime = GetTickCount();
        }
    }

    if (g_p1Pos == g_maze.goalPos) {
        g_gameOver = true;
        g_winner = 1;
        g_elapsedTime = GetTickCount() - g_startTime;
        saveRecord();

        KillTimer(hWnd, TIMER_AI);
        KillTimer(hWnd, TIMER_PLAYER);
        KillTimer(hWnd, TIMER_CLOCK);
    }

    if (g_currentMode == MODE_VS_HUMAN && g_p2IsMoving) {
        Point targetP2(g_p2Pos.x + g_p2Dir.x, g_p2Pos.y + g_p2Dir.y);

        if (!g_maze.isWall(targetP2.x, targetP2.y)) {
            g_p2Pos = targetP2;
            g_p2Steps++;
            moved = true;

            if (g_maze.canChangeDirection(g_p2Pos, g_p2Dir)) {
                g_p2IsMoving = false;
                g_p2Dir = Point(0, 0);
            }
        } else {
            g_p2IsMoving = false;
            g_p2Dir = Point(0, 0);
        }

        if (g_p2Pos == g_maze.goalPos) {
            g_gameOver = true;
            g_winner = 2;
            g_elapsedTime = GetTickCount() - g_startTime;
            saveRecord();

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
    if (g_gameOver || g_isCountingDown) return;

    Point newDir(0, 0);
    if (key == 'W' || key == 'w') newDir = Point(0, -1);
    else if (key == 'S' || key == 's') newDir = Point(0, 1);
    else if (key == 'A' || key == 'a') newDir = Point(-1, 0);
    else if (key == 'D' || key == 'd') newDir = Point(1, 0);

    if (newDir.x != 0 || newDir.y != 0) {
        Point target(g_p1Pos.x + newDir.x, g_p1Pos.y + newDir.y);

        if (!g_maze.isWall(target.x, target.y)) {
            if (g_isP1Stopped) {
                DWORD now = GetTickCount();
                double reactionMs = (double)(now - g_stopStartTime);
                g_currentReactionTimeMs += reactionMs;
                g_currentIntersectionCount++;
                g_isP1Stopped = false;
            }

            g_p1Dir = newDir;
            g_p1IsMoving = true;
        } else {
            g_currentWrongTurnsCount++;
        }
    }

    if (g_currentMode == MODE_VS_HUMAN) {
        Point newDirP2(0, 0);
        if (key == VK_UP) newDirP2 = Point(0, -1);
        else if (key == VK_DOWN) newDirP2 = Point(0, 1);
        else if (key == VK_LEFT) newDirP2 = Point(-1, 0);
        else if (key == VK_RIGHT) newDirP2 = Point(1, 0);

        if (newDirP2.x != 0 || newDirP2.y != 0) {
            Point targetP2(g_p2Pos.x + newDirP2.x, g_p2Pos.y + newDirP2.y);
            if (!g_maze.isWall(targetP2.x, targetP2.y)) {
                g_p2Dir = newDirP2;
                g_p2IsMoving = true;
            }
        }
    }
}

// ==========================================
// Win32 UI作成 ＆ ウィンドウプロシージャ
// ==========================================
void createUIControls(HWND hWnd, HINSTANCE hInstance) {
    g_hComboMode = CreateWindow("COMBOBOX", NULL, CBS_DROPDOWNLIST | WS_CHILD | WS_VISIBLE | WS_VSCROLL,
                                MARGIN + 55, 14, 80, 120, hWnd, (HMENU)IDC_COMBO_MODE, hInstance, NULL);
    SendMessage(g_hComboMode, CB_ADDSTRING, 0, (LPARAM)"VS AI");
    SendMessage(g_hComboMode, CB_ADDSTRING, 0, (LPARAM)"VS HUMAN");
    SendMessage(g_hComboMode, CB_SETCURSEL, 0, 0);

    g_hComboAI = CreateWindow("COMBOBOX", NULL, CBS_DROPDOWNLIST | WS_CHILD | WS_VISIBLE | WS_VSCROLL,
                              MARGIN + 215, 14, 45, 200, hWnd, (HMENU)IDC_COMBO_AI, hInstance, NULL);
    for (int i = 1; i <= 10; ++i) {
        char buf[16];
        sprintf(buf, "%d", i);
        SendMessage(g_hComboAI, CB_ADDSTRING, 0, (LPARAM)buf);
    }
    SendMessage(g_hComboAI, CB_SETCURSEL, 4, 0); // 初期値 Lv.5

    g_hComboMaze = CreateWindow("COMBOBOX", NULL, CBS_DROPDOWNLIST | WS_CHILD | WS_VISIBLE | WS_VSCROLL,
                                MARGIN + 350, 14, 45, 200, hWnd, (HMENU)IDC_COMBO_MAZE, hInstance, NULL);
    for (int i = 1; i <= 10; ++i) {
        char buf[16];
        sprintf(buf, "%d", i);
        SendMessage(g_hComboMaze, CB_ADDSTRING, 0, (LPARAM)buf);
    }
    SendMessage(g_hComboMaze, CB_SETCURSEL, 4, 0);

    g_hChkFog = CreateWindow("BUTTON", "視界制限", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
                             MARGIN + 405, 14, 85, 22, hWnd, (HMENU)IDC_CHK_FOG, hInstance, NULL);

    g_hBtnStart = CreateWindow("BUTTON", "ゲーム開始", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
                               MARGIN + 495, 13, 90, 25, hWnd, (HMENU)IDC_BTN_START, hInstance, NULL);
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
            g_countdownValue--;
            if (g_countdownValue < 0) {
                KillTimer(hWnd, TIMER_COUNTDOWN);
                g_isCountingDown = false;
                g_startTime = GetTickCount();
                g_stopStartTime = g_startTime;

                SetTimer(hWnd, TIMER_AI, MOVE_SPEED_DELAY, NULL);
                SetTimer(hWnd, TIMER_PLAYER, MOVE_SPEED_DELAY, NULL);
                SetTimer(hWnd, TIMER_CLOCK, 100, NULL);
            }
            InvalidateRect(hWnd, NULL, FALSE);
        }
        else if (wParam == TIMER_AI && g_currentMode == MODE_VS_AI && !g_gameOver && !g_isCountingDown) {
            Point nextAI = getNextAIMove();
            if (nextAI != g_p2Pos) {
                g_aiLastPos = g_p2Pos;
                g_p2Pos = nextAI;
                g_p2Steps++;
            }

            if (g_p2Pos == g_maze.goalPos) {
                g_gameOver = true;
                g_winner = 2;
                g_elapsedTime = GetTickCount() - g_startTime;
                saveRecord();

                KillTimer(hWnd, TIMER_AI);
                KillTimer(hWnd, TIMER_PLAYER);
                KillTimer(hWnd, TIMER_CLOCK);
            }
            InvalidateRect(hWnd, NULL, FALSE);
        }
        else if (wParam == TIMER_PLAYER && !g_gameOver && !g_isCountingDown) {
            processPlayerStep(hWnd);
        }
        else if (wParam == TIMER_CLOCK && !g_gameOver && !g_isCountingDown) {
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

// ==========================================
// エントリポイント
// ==========================================
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
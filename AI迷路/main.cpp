#include <windows.h>
#include <vector>
#include <string>
#include <random>
#include <chrono>

#define ID_BTN_MODE_AI_AI 101
#define ID_BTN_MODE_P_P   102
#define ID_BTN_MODE_AI_P  103
#define ID_BTN_START      104
#define ID_COMBO_DIFF_L   105
#define ID_COMBO_DIFF_R   106
#define ID_COMBO_MAZE     107

enum GameMode { MODE_AI_AI, MODE_P_P, MODE_AI_P };
enum CellType { CELL_WALL = 1, CELL_PATH = 0, CELL_START = 2, CELL_GOAL = 3 };

struct Point {
    int x, y;
    bool operator==(const Point& o) const { return x == o.x && y == o.y; }
};

struct Player {
    double x, y;
    int goalX, goalY;
    int score;
    bool isAI;
    int aiLevel;
    std::vector<Point> path;
    int pathIndex;
    DWORD lastMoveTick;
    bool finished;
};

HWND g_hWnd = NULL;
HWND g_hBtnAIAI = NULL, g_hBtnPP = NULL, g_hBtnAIP = NULL, g_hBtnStart = NULL;
HWND g_hCmbDiffL = NULL, g_hCmbDiffR = NULL, g_hCmbMaze = NULL;

GameMode g_gameMode = MODE_AI_AI;
bool g_gameRunning = false;
int g_mazeSizeIdx = 1; // 0: Small (15x15), 1: Normal (25x25), 2: Large (35x35)
int g_mazeW = 25;
int g_mazeH = 25;

std::vector<std::vector<int>> g_leftMaze;
std::vector<std::vector<int>> g_rightMaze;

Player g_p1;
Player g_p2;

std::mt19937 g_rng((unsigned)chrono::system_clock::now().time_since_epoch().count());

void GenerateMaze(int w, int h, std::vector<std::vector<int>>& maze) {
    if (w % 2 == 0) w--;
    if (h % 2 == 0) h--;
    maze.assign(h, std::vector<int>(w, CELL_WALL));

    auto isValid = [w, h](int x, int y) {
        return x > 0 && x < w - 1 && y > 0 && y < h - 1;
    };

    int startX = 1, startY = 1;
    maze[startY][startX] = CELL_PATH;

    std::vector<Point> stack;
    stack.push_back({startX, startY});

    int dx[] = {0, 0, 2, -2};
    int dy[] = {2, -2, 0, 0};

    while (!stack.empty()) {
        Point cur = stack.back();
        std::vector<int> dirs = {0, 1, 2, 3};
        std::shuffle(dirs.begin(), dirs.end(), g_rng);

        bool moved = false;
        for (int d : dirs) {
            int nx = cur.x + dx[d];
            int ny = cur.y + dy[d];
            if (isValid(nx, ny) && maze[ny][nx] == CELL_WALL) {
                maze[cur.y + dy[d] / 2][cur.x + dx[d] / 2] = CELL_PATH;
                maze[ny][nx] = CELL_PATH;
                stack.push_back({nx, ny});
                moved = true;
                break;
            }
        }
        if (!moved) {
            stack.pop_back();
        }
    }

    maze[1][1] = CELL_START;
    maze[h - 2][w - 2] = CELL_GOAL;
}

void InitGame() {
    int sizeVal = 25;
    if (g_mazeSizeIdx == 0) sizeVal = 15;
    else if (g_mazeSizeIdx == 1) sizeVal = 25;
    else if (g_mazeSizeIdx == 2) sizeVal = 35;

    g_mazeW = sizeVal;
    g_mazeH = sizeVal;

    GenerateMaze(g_mazeW, g_mazeH, g_leftMaze);
    g_rightMaze = g_leftMaze;

    g_p1.x = 1.0; g_p1.y = 1.0;
    g_p1.goalX = g_mazeW - 2; g_p1.goalY = g_mazeH - 2;
    g_p1.score = 0;
    g_p1.isAI = (g_gameMode == MODE_AI_AI || g_gameMode == MODE_AI_P);
    g_p1.aiLevel = (int)SendMessage(g_hCmbDiffL, CB_GETCURSEL, 0, 0) + 1;
    g_p1.path.clear();
    g_p1.pathIndex = 0;
    g_p1.lastMoveTick = 0;
    g_p1.finished = false;

    g_p2.x = 1.0; g_p2.y = 1.0;
    g_p2.goalX = g_mazeW - 2; g_p2.goalY = g_mazeH - 2;
    g_p2.score = 0;
    g_p2.isAI = (g_gameMode == MODE_AI_AI);
    g_p2.aiLevel = (int)SendMessage(g_hCmbDiffR, CB_GETCURSEL, 0, 0) + 1;
    g_p2.path.clear();
    g_p2.pathIndex = 0;
    g_p2.lastMoveTick = 0;
    g_p2.finished = false;
}

std::vector<Point> SolveBFS(const std::vector<std::vector<int>>& maze, Point start, Point goal) {
    int h = maze.size();
    int w = maze[0].size();
    std::vector<std::vector<Point>> parent(h, std::vector<Point>(w, {-1, -1}));
    std::vector<std::vector<bool>> visited(h, std::vector<bool>(w, false));
    std::vector<Point> q;

    q.push_back(start);
    visited[start.y][start.x] = true;

    int head = 0;
    bool found = false;
    int dx[] = {0, 0, 1, -1};
    int dy[] = {1, -1, 0, 0};

    while (head < (int)q.size()) {
        Point cur = q[head++];
        if (cur == goal) {
            found = true;
            break;
        }
        for (int i = 0; i < 4; i++) {
            int nx = cur.x + dx[i];
            int ny = cur.y + dy[i];
            if (nx >= 0 && nx < w && ny >= 0 && ny < h) {
                if (maze[ny][nx] != CELL_WALL && !visited[ny][nx]) {
                    visited[ny][nx] = true;
                    parent[ny][nx] = cur;
                    q.push_back({nx, ny});
                }
            }
        }
    }

    std::vector<Point> path;
    if (found) {
        Point curr = goal;
        while (!(curr == start)) {
            path.push_back(curr);
            curr = parent[curr.y][curr.x];
        }
        path.push_back(start);
        std::vector<Point> rev(path.rbegin(), path.rend());
        return rev;
    }
    return path;
}

void UpdateAI(Player& p, const std::vector<std::vector<int>>& maze) {
    if (p.finished) return;

    int cx = (int)(p.x + 0.5);
    int cy = (int)(p.y + 0.5);

    if (cx == p.goalX && cy == p.goalY) {
        p.finished = true;
        return;
    }

    if (p.path.empty() || p.pathIndex >= (int)p.path.size()) {
        Point start = {cx, cy};
        Point goal = {p.goalX, p.goalY};
        p.path = SolveBFS(maze, start, goal);
        p.pathIndex = 0;
    }

    if (!p.path.empty() && p.pathIndex < (int)p.path.size()) {
        Point next = p.path[p.pathIndex];
        double targetX = next.x;
        double targetY = next.y;

        double speed = 0.05 + (p.aiLevel * 0.025);
        if (p.aiLevel >= 10) speed = 0.5;

        if (p.x < targetX) p.x += speed;
        else if (p.x > targetX) p.x -= speed;
        
        if (p.y < targetY) p.y += speed;
        else if (p.y > targetY) p.y -= speed;

        if (abs(p.x - targetX) < 0.01 && abs(p.y - targetY) < 0.01) {
            p.x = targetX;
            p.y = targetY;
            p.pathIndex++;
        }
    }
}

bool CanMove(double nx, double ny, const std::vector<std::vector<int>>& maze) {
    double radius = 0.35;
    int points[4][2] = {
        {(int)(nx - radius), (int)(ny - radius)},
        {(int)(nx + radius), (int)(ny - radius)},
        {(int)(nx - radius), (int)(ny + radius)},
        {(int)(nx + radius), (int)(ny + radius)}
    };

    for (int i = 0; i < 4; i++) {
        int px = points[i][0];
        int py = points[i][1];
        if (px < 0 || px >= g_mazeW || py < 0 || py >= g_mazeH) return false;
        if (maze[py][px] == CELL_WALL) return false;
    }
    return true;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        g_hBtnAIAI = CreateWindow("BUTTON", "AI vs AI", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON, 10, 10, 90, 25, hWnd, (HMENU)ID_BTN_MODE_AI_AI, ((LPCREATESTRUCT)lParam)->hInstance, NULL);
        g_hBtnPP   = CreateWindow("BUTTON", "P vs P",   WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON, 110, 10, 70, 25, hWnd, (HMENU)ID_BTN_MODE_P_P,   ((LPCREATESTRUCT)lParam)->hInstance, NULL);
        g_hBtnAIP  = CreateWindow("BUTTON", "AI vs P",  WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON, 190, 10, 70, 25, hWnd, (HMENU)ID_BTN_MODE_AI_P,  ((LPCREATESTRUCT)lParam)->hInstance, NULL);
        SendMessage(g_hBtnAIAI, BM_SETCHECK, BST_CHECKED, 0);

        CreateWindow("STATIC", "L-Diff:", WS_CHILD | WS_VISIBLE, 270, 13, 45, 20, hWnd, NULL, NULL, NULL);
        g_hCmbDiffL = CreateWindow("COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 320, 10, 50, 150, hWnd, (HMENU)ID_COMBO_DIFF_L, NULL, NULL);
        
        CreateWindow("STATIC", "R-Diff:", WS_CHILD | WS_VISIBLE, 380, 13, 45, 20, hWnd, NULL, NULL, NULL);
        g_hCmbDiffR = CreateWindow("COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 430, 10, 50, 150, hWnd, (HMENU)ID_COMBO_DIFF_R, NULL, NULL);

        CreateWindow("STATIC", "Size:", WS_CHILD | WS_VISIBLE, 490, 13, 35, 20, hWnd, NULL, NULL, NULL);
        g_hCmbMaze  = CreateWindow("COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 530, 10, 70, 150, hWnd, (HMENU)ID_COMBO_MAZE, NULL, NULL);

        g_hBtnStart = CreateWindow("BUTTON", "START", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 610, 10, 80, 25, hWnd, (HMENU)ID_BTN_START, ((LPCREATESTRUCT)lParam)->hInstance, NULL);

        for (int i = 1; i <= 10; i++) {
            char buf[16];
            wsprintf(buf, "Lv.%d", i);
            SendMessage(g_hCmbDiffL, CB_ADDSTRING, 0, (LPARAM)buf);
            SendMessage(g_hCmbDiffR, CB_ADDSTRING, 0, (LPARAM)buf);
        }
        SendMessage(g_hCmbDiffL, CB_SETCURSEL, 6, 0); // Default Lv.7
        SendMessage(g_hCmbDiffR, CB_SETCURSEL, 6, 0);

        SendMessage(g_hCmbMaze, CB_ADDSTRING, 0, (LPARAM)"Small");
        SendMessage(g_hCmbMaze, CB_ADDSTRING, 0, (LPARAM)"Normal");
        SendMessage(g_hCmbMaze, CB_ADDSTRING, 0, (LPARAM)"Large");
        SendMessage(g_hCmbMaze, CB_SETCURSEL, 1, 0);

        SetTimer(hWnd, 1, 16, NULL);
        InitGame();
        break;

    case WM_COMMAND:
        if (LOWORD(wParam) == ID_BTN_MODE_AI_AI) g_gameMode = MODE_AI_AI;
        if (LOWORD(wParam) == ID_BTN_MODE_P_P)   g_gameMode = MODE_P_P;
        if (LOWORD(wParam) == ID_BTN_MODE_AI_P)  g_gameMode = MODE_AI_P;
        if (LOWORD(wParam) == ID_COMBO_MAZE) {
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                g_mazeSizeIdx = (int)SendMessage(g_hCmbMaze, CB_GETCURSEL, 0, 0);
            }
        }
        if (LOWORD(wParam) == ID_BTN_START) {
            InitGame();
            g_gameRunning = true;
        }
        break;

    case WM_TIMER:
        if (g_gameRunning) {
            if (g_p1.isAI) UpdateAI(g_p1, g_leftMaze);
            if (g_p2.isAI) UpdateAI(g_p2, g_rightMaze);

            if (!g_p1.isAI && !g_p1.finished) {
                double speed = 0.1;
                double nx = g_p1.x, ny = g_p1.y;
                if (GetAsyncKeyState(VK_LEFT) & 0x8000) nx -= speed;
                if (GetAsyncKeyState(VK_RIGHT) & 0x8000) nx += speed;
                if (GetAsyncKeyState(VK_UP) & 0x8000) ny -= speed;
                if (GetAsyncKeyState(VK_DOWN) & 0x8000) ny += speed;

                if (CanMove(nx, g_p1.y, g_leftMaze)) g_p1.x = nx;
                if (CanMove(g_p1.x, ny, g_leftMaze)) g_p1.y = ny;

                if ((int)(g_p1.x + 0.5) == g_p1.goalX && (int)(g_p1.y + 0.5) == g_p1.goalY) {
                    g_p1.finished = true;
                }
            }

            if (!g_p2.isAI && !g_p2.finished) {
                double speed = 0.1;
                double nx = g_p2.x, ny = g_p2.y;
                if (GetAsyncKeyState('A') & 0x8000) nx -= speed;
                if (GetAsyncKeyState('D') & 0x8000) nx += speed;
                if (GetAsyncKeyState('W') & 0x8000) ny -= speed;
                if (GetAsyncKeyState('S') & 0x8000) ny += speed;

                if (CanMove(nx, g_p2.y, g_rightMaze)) g_p2.x = nx;
                if (CanMove(g_p2.x, ny, g_rightMaze)) g_p2.y = ny;

                if ((int)(g_p2.x + 0.5) == g_p2.goalX && (int)(g_p2.y + 0.5) == g_p2.goalY) {
                    g_p2.finished = true;
                }
            }
        }
        InvalidateRect(hWnd, NULL, FALSE);
        break;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        RECT clientRect;
        GetClientRect(hWnd, &clientRect);
        int width = clientRect.right;
        int height = clientRect.bottom - 45;

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBitmap = CreateCompatibleBitmap(hdc, clientRect.right, clientRect.bottom);
        HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

        FillRect(memDC, &clientRect, (HBRUSH)(COLOR_WINDOW + 1));

        int halfW = width / 2;
        int fieldH = height;

        int cellW = halfW / g_mazeW;
        int cellH = fieldH / g_mazeH;
        int cellSize = (cellW < cellH) ? cellW : cellH;
        if (cellSize < 4) cellSize = 4;

        auto drawMaze = [&](const std::vector<std::vector<int>>& maze, int offsetX, int offsetY, const Player& p) {
            for (int y = 0; y < g_mazeH; y++) {
                for (int x = 0; x < g_mazeW; x++) {
                    RECT rc = { offsetX + x * cellSize, offsetY + y * cellSize, offsetX + (x + 1) * cellSize, offsetY + (y + 1) * cellSize };
                    HBRUSH brush;
                    if (maze[y][x] == CELL_WALL) brush = CreateSolidBrush(RGB(50, 50, 50));
                    else if (maze[y][x] == CELL_GOAL) brush = CreateSolidBrush(RGB(255, 100, 100));
                    else brush = CreateSolidBrush(RGB(240, 240, 240));
                    FillRect(memDC, &rc, brush);
                    DeleteObject(brush);
                }
            }

            RECT prc = {
                offsetX + (int)(p.x * cellSize) + 1,
                offsetY + (int)(p.y * cellSize) + 1,
                offsetX + (int)((p.x + 1) * cellSize) - 1,
                offsetY + (int)((p.y + 1) * cellSize) - 1
            };
            HBRUSH pBrush = CreateSolidBrush(p.isAI ? RGB(0, 120, 255) : RGB(255, 120, 0));
            FillRect(memDC, &prc, pBrush);
            DeleteObject(pBrush);
        };

        int lOffsetX = (halfW - g_mazeW * cellSize) / 2;
        int lOffsetY = 45 + (fieldH - g_mazeH * cellSize) / 2;
        drawMaze(g_leftMaze, lOffsetX, lOffsetY, g_p1);

        int rOffsetX = halfW + (halfW - g_mazeW * cellSize) / 2;
        int rOffsetY = 45 + (fieldH - g_mazeH * cellSize) / 2;
        drawMaze(g_rightMaze, rOffsetX, rOffsetY, g_p2);

        HPEN hPen = CreatePen(PS_SOLID, 2, RGB(200, 200, 200));
        SelectObject(memDC, hPen);
        MoveToEx(memDC, halfW, 45, NULL);
        LineTo(memDC, halfW, clientRect.bottom);
        DeleteObject(hPen);

        BitBlt(hdc, 0, 0, clientRect.right, clientRect.bottom, memDC, 0, 0, SRCCOPY);

        SelectObject(memDC, oldBitmap);
        DeleteObject(memBitmap);
        DeleteDC(memDC);

        EndPaint(hWnd, &ps);
        break;
    }

    case WM_SIZE:
        InvalidateRect(hWnd, NULL, TRUE);
        break;

    case WM_DESTROY:
        KillTimer(hWnd, 1);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASSEX wcex;
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = NULL;
    wcex.lpszClassName = "MazeBattleClass";
    wcex.hIconSm = LoadIcon(NULL, IDI_APPLICATION);

    RegisterClassEx(&wcex);

    HWND hWnd = CreateWindow("MazeBattleClass", "Maze Battle AI vs Human", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600, NULL, NULL, hInstance, NULL);

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}

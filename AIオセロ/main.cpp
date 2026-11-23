#include <windows.h>
#include <commctrl.h> 
#include <vector>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <string>
#include <stdio.h>
#include <math.h>
#include <cstring>

using namespace std;

typedef BOOL (WINAPI *INITCOMMONCONTROLSEXPROC)(const INITCOMMONCONTROLSEX*);

const int BOARD_SIZE = 8;
int g_cellSize = 50; 
const int EMPTY = 0;
const int BLACK = 1;
const int WHITE = -1;

const int dr[] = {-1, -1, -1,  0, 0,  1, 1, 1};
const int dc[] = {-1,  0,  1, -1, 1, -1, 0, 1};

double g_weightTable[BOARD_SIZE][BOARD_SIZE];

struct Move {
    int r, c;
};

class Othello {
public:
    int board[BOARD_SIZE][BOARD_SIZE];
    int turn;

    Othello();
    void reset();
    bool isValidMove(int r, int c, bool flip = false);
    vector<Move> getValidMoves();
};

Othello::Othello() { 
    reset(); 
}

void Othello::reset() {
    for(int i=0; i<BOARD_SIZE; i++) {
        for(int j=0; j<BOARD_SIZE; j++) board[i][j] = EMPTY;
    }
    board[3][3] = WHITE; board[3][4] = BLACK;
    board[4][3] = BLACK; board[4][4] = WHITE;
    turn = BLACK;
}

bool Othello::isValidMove(int r, int c, bool flip) {
    if (r < 0 || r >= BOARD_SIZE || c < 0 || c >= BOARD_SIZE) return false;
    if (board[r][c] != EMPTY) return false;
    bool valid = false;

    for (int i = 0; i < 8; i++) {
        int nr = r + dr[i];
        int nc = c + dc[i];
        int count = 0;

        while (nr >= 0 && nr < BOARD_SIZE && nc >= 0 && nc < BOARD_SIZE && board[nr][nc] == -turn) {
            nr += dr[i];
            nc += dc[i];
            count++;
        }

        if (count > 0 && nr >= 0 && nr < BOARD_SIZE && nc >= 0 && nc < BOARD_SIZE && board[nr][nc] == turn) {
            valid = true;
            if (flip) {
                int fr = r + dr[i];
                int fc = c + dc[i];
                while (fr != nr || fc != nc) {
                    board[fr][fc] = turn;
                    fr += dr[i];
                    fc += dc[i];
                }
            }
        }
    }
    if (valid && flip) {
        board[r][c] = turn;
    }
    return valid;
}

vector<Move> g_gameValidMoves; 

vector<Move> Othello::getValidMoves() {
    vector<Move> moves;
    for (int i = 0; i < BOARD_SIZE; i++) {
        for (int j = 0; j < BOARD_SIZE; j++) {
            if (isValidMove(i, j)) {
                Move m; m.r = i; m.c = j;
                moves.push_back(m);
            }
        }
    }
    return moves;
}

Othello g_game;
int g_mode = 1; 
int g_passCount = 0;
bool g_gameOver = false;
int g_totalGames = 10;    
int g_currentGame = 0;
int g_totalDataCount = 0; 
bool g_isPassing = false; 

const char* const DATA_FILE_PATH = "othello_dataset.dat";
int g_aiLevel = 5; 

char g_inputBuffer[16] = "10";
bool g_inputDone = false;

const unsigned char MOVE_PASS = 0xFF;

RECT g_btnRectPvP;
RECT g_btnRectPvE;
RECT g_btnRectEvE;

bool g_learningCancelled = false;
streampos g_gameStartPosition = 0; 

bool g_supportMode = false;
int g_supportTarget = BLACK;

struct FlipAnimation {
    int r;
    int c;
    int fromColor;
    int toColor;
};

bool g_isAnimating = false;
int g_animationStep = 0;
const int ANIMATION_STEPS = 3; 
vector<FlipAnimation> g_flipAnimations;

HWND g_hThinkingProgress = NULL;
bool g_isThinking = false;

// --- Python プロセス間通信（パイプ）関連ハンドル ---
HANDLE g_hChildStdInWrite = NULL;
HANDLE g_hChildStdOutRead = NULL;
HANDLE g_hPythonProcess = NULL;

void InitPythonProcess() {
    HANDLE hChildStdInRead, hChildStdOutWrite;
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    CreatePipe(&g_hChildStdOutRead, &hChildStdOutWrite, &sa, 0);
    SetHandleInformation(g_hChildStdOutRead, HANDLE_FLAG_INHERIT, 0);

    CreatePipe(&hChildStdInRead, &g_hChildStdInWrite, &sa, 0);
    SetHandleInformation(g_hChildStdInWrite, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(STARTUPINFO));
    si.cb = sizeof(STARTUPINFO);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    si.hStdOutput = hChildStdOutWrite;
    si.hStdInput = hChildStdInRead;
    si.dwFlags |= STARTF_USESTDHANDLES;

    ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));

    char cmd[] = "python main.py";
    BOOL success = CreateProcess(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);

    if (success) {
        g_hPythonProcess = pi.hProcess;
        CloseHandle(pi.hThread);
    }
    CloseHandle(hChildStdOutWrite);
    CloseHandle(hChildStdInRead);
}

void ClosePythonProcess() {
    if (g_hChildStdInWrite) CloseHandle(g_hChildStdInWrite);
    if (g_hChildStdOutRead) CloseHandle(g_hChildStdOutRead);
    if (g_hPythonProcess) {
        TerminateProcess(g_hPythonProcess, 0);
        CloseHandle(g_hPythonProcess);
    }
}

Move QueryPythonAI(const Othello& state, int level) {
    Move m = {-1, -1};
    if (!g_hChildStdInWrite || !g_hChildStdOutRead) return m;

    char jsonBuf[1024];
    string boardStr = "[";
    for(int i=0; i<BOARD_SIZE; i++) {
        boardStr += "[";
        for(int j=0; j<BOARD_SIZE; j++) {
            char num[8];
            sprintf(num, "%d", state.board[i][j]);
            boardStr += num;
            if(j < BOARD_SIZE-1) boardStr += ",";
        }
        boardStr += "]";
        if(i < BOARD_SIZE-1) boardStr += ",";
    }
    boardStr += "]";

    sprintf(jsonBuf, "{\"turn\":%d,\"level\":%d,\"board\":%s}\n", state.turn, level, boardStr.c_str());

    DWORD bytesWritten;
    WriteFile(g_hChildStdInWrite, jsonBuf, (DWORD)strlen(jsonBuf), &bytesWritten, NULL);

    char readBuf[512];
    DWORD bytesRead;
    string response = "";
    while (true) {
        if (ReadFile(g_hChildStdOutRead, readBuf, 1, &bytesRead, NULL)) {
            if (bytesRead == 0 || readBuf[0] == '\n') break;
            response += readBuf[0];
        } else {
            break;
        }
    }

    int r = -1, c = -1;
    char* pr = strstr((char*)response.c_str(), "\"r\":");
    char* pc = strstr((char*)response.c_str(), "\"c\":");
    if (pr && pc) {
        r = atoi(pr + 4);
        c = atoi(pc + 4);
    }
    m.r = r; m.c = c;
    return m;
}

void saveStepToDataset(Move nextMove, bool isPass);
void saveGameEndMarker();
void estimateWinProbabilitiesForState(const Othello& state, int& blackPct, int& whitePct);
void runFastLearning(HWND hwnd);
void triggerAIMove(HWND hwnd);
void truncateUnfinishedGame();
vector<FlipAnimation> getFlipAnimationsForMove(int r, int c, int player);
FlipAnimation* findFlipAnimation(int r, int c);

Move selectAIMoveBasedOnLevel(const vector<Move>& moves) {
    if (moves.empty()) {
        Move m = {0, 0}; return m;
    }

    g_isThinking = true;
    if (g_hThinkingProgress) {
        SendMessage(g_hThinkingProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
        SendMessage(g_hThinkingProgress, PBM_SETPOS, 50, 0);
        ShowWindow(g_hThinkingProgress, SW_SHOW);
    }

    Move bestMove = QueryPythonAI(g_game, g_aiLevel);

    if (g_hThinkingProgress) {
        SendMessage(g_hThinkingProgress, PBM_SETPOS, 100, 0);
        ShowWindow(g_hThinkingProgress, SW_HIDE);
    }
    g_isThinking = false;

    if (bestMove.r >= 0 && bestMove.c >= 0 && g_game.isValidMove(bestMove.r, bestMove.c)) {
        return bestMove;
    }
    return moves[rand() % moves.size()];
}

void saveStepToDataset(Move nextMove, bool isPass) {
    ofstream dbFile(DATA_FILE_PATH, ios::app | ios::binary);
    if (!dbFile) return;

    if (isPass) {
        unsigned char code = MOVE_PASS;
        dbFile.write((char*)&code, 1);
    } else {
        unsigned char code = (nextMove.r << 4) | (nextMove.c & 0x0F);
        dbFile.write((char*)&code, 1);
        g_totalDataCount++;
    }
    dbFile.close();
}

void saveGameEndMarker() {
    ofstream dbFile(DATA_FILE_PATH, ios::app | ios::binary);
    if (!dbFile) return;
    unsigned char marker = 0xFE; 
    dbFile.write((char*)&marker, 1);
    dbFile.close();
}

void recordCurrentFilePosition() {
    ifstream dbFile(DATA_FILE_PATH, ios::binary | ios::ate);
    if (!dbFile) {
        g_gameStartPosition = 0;
    } else {
        g_gameStartPosition = dbFile.tellg();
        dbFile.close();
    }
}

void truncateUnfinishedGame() {
    if (g_gameStartPosition <= 0) return;

    HANDLE hFile = CreateFile(DATA_FILE_PATH, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        LONG lowPart = (LONG)(g_gameStartPosition & 0xFFFFFFFF);
        LONG highPart = 0; 
        SetFilePointer(hFile, lowPart, &highPart, FILE_BEGIN);
        SetEndOfFile(hFile);
        CloseHandle(hFile);
    }
}

vector<FlipAnimation> getFlipAnimationsForMove(int r, int c, int player) {
    vector<FlipAnimation> animations;
    if (r < 0 || r >= BOARD_SIZE || c < 0 || c >= BOARD_SIZE) return animations;
    if (g_game.board[r][c] != EMPTY) return animations;

    for (int i = 0; i < 8; i++) {
        int nr = r + dr[i];
        int nc = c + dc[i];
        vector<Move> line;

        while (nr >= 0 && nr < BOARD_SIZE && nc >= 0 && nc < BOARD_SIZE && g_game.board[nr][nc] == -player) {
            Move m; m.r = nr; m.c = nc;
            line.push_back(m);
            nr += dr[i];
            nc += dc[i];
        }

        if (!line.empty() && nr >= 0 && nr < BOARD_SIZE && nc >= 0 && nc < BOARD_SIZE && g_game.board[nr][nc] == player) {
            for (size_t j = 0; j < line.size(); j++) {
                FlipAnimation anim;
                anim.r = line[j].r;
                anim.c = line[j].c;
                anim.fromColor = -player;
                anim.toColor = player;
                animations.push_back(anim);
            }
        }
    }

    if (!animations.empty()) {
        FlipAnimation stoneAnim;
        stoneAnim.r = r;
        stoneAnim.c = c;
        stoneAnim.fromColor = EMPTY;
        stoneAnim.toColor = player;
        animations.push_back(stoneAnim);
    }

    return animations;
}

FlipAnimation* findFlipAnimation(int r, int c) {
    for (size_t i = 0; i < g_flipAnimations.size(); i++) {
        if (g_flipAnimations[i].r == r && g_flipAnimations[i].c == c) {
            return &g_flipAnimations[i];
        }
    }
    return NULL;
}

bool processSingleMove() {
    if (g_gameOver) return false;

    vector<Move> moves = g_game.getValidMoves();
    if (moves.empty()) {
        g_passCount++;
        Move dummyMove; dummyMove.r = 0; dummyMove.c = 0;
        saveStepToDataset(dummyMove, true); 
        if (g_passCount >= 2) {
            g_gameOver = true;
            g_isPassing = false;
            saveGameEndMarker(); 
            return false;
        }
        g_isPassing = true;
        g_game.turn = -g_game.turn;
        return true; 
    }

    g_passCount = 0;
    g_isPassing = false;

    Move aiMove = selectAIMoveBasedOnLevel(moves);
    saveStepToDataset(aiMove, false); 
    g_game.isValidMove(aiMove.r, aiMove.c, true);
    g_game.turn = -g_game.turn;

    return true;
}

LRESULT CALLBACK ProgressWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    static HWND hProgress, hStatus;
    switch (msg) {
        case WM_CREATE:
            hStatus = CreateWindow("STATIC", "自動学習中...", WS_CHILD | WS_VISIBLE, 20, 15, 300, 20, hwnd, NULL, NULL, NULL);
            hProgress = CreateWindow(PROGRESS_CLASS, NULL, WS_CHILD | WS_VISIBLE | PBS_SMOOTH, 20, 40, 300, 25, hwnd, NULL, NULL, NULL);
            CreateWindow("BUTTON", "キャンセル", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 130, 80, 100, 30, hwnd, (HMENU)IDCANCEL, NULL, NULL);
            
            SendMessage(hProgress, PBM_SETRANGE, 0, MAKELPARAM(0, g_totalGames));
            SendMessage(hProgress, PBM_SETSTEP, 1, 0);
            break;
        case WM_USER + 100: { 
            int cur = (int)wp;
            char buf[128];
            sprintf(buf, "高速自動学習中... ( %d / %d 試合完了 )", cur, g_totalGames);
            SetWindowText(hStatus, buf);
            SendMessage(hProgress, PBM_SETPOS, cur, 0);
            break;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDCANCEL) {
                g_learningCancelled = true;
                DestroyWindow(hwnd);
            }
            break;
        default:
            return DefWindowProc(hwnd, msg, wp, lp);
    }
    return 0;
}

void runFastLearning(HWND hwnd) {
    int oldMode = g_mode;
    g_mode = 2; 
    g_currentGame = 0;
    g_learningCancelled = false;

    HINSTANCE hInst = GetModuleHandle(NULL);
    WNDCLASSEX wcl = {0};
    wcl.cbSize = sizeof(WNDCLASSEX);
    wcl.lpfnWndProc = ProgressWndProc;
    wcl.hInstance = hInst;
    wcl.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcl.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wcl.lpszClassName = "ProgressBoxWindow";
    RegisterClassEx(&wcl);

    HWND hwndProg = CreateWindowEx(WS_EX_DLGMODALFRAME, "ProgressBoxWindow", "学習プログレス", 
                                   WS_POPUPWINDOW | WS_CAPTION, CW_USEDEFAULT, CW_USEDEFAULT, 350, 160, 
                                   hwnd, NULL, hInst, NULL);
    
    EnableWindow(hwnd, FALSE);
    
    RECT rcOwner, rcDlg;
    GetWindowRect(hwnd, &rcOwner);
    GetWindowRect(hwndProg, &rcDlg);
    int x = rcOwner.left + ((rcOwner.right - rcOwner.left) - (rcDlg.right - rcDlg.left)) / 2;
    int y = rcOwner.top + ((rcOwner.bottom - rcOwner.top) - (rcDlg.bottom - rcDlg.top)) / 2;
    SetWindowPos(hwndProg, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    ShowWindow(hwndProg, SW_SHOW);
    UpdateWindow(hwndProg);

    while (g_currentGame < g_totalGames && !g_learningCancelled) {
        recordCurrentFilePosition();

        g_game.reset();
        g_gameOver = false;
        g_passCount = 0;
        g_isPassing = false;

        while (!g_gameOver && !g_learningCancelled) {
            processSingleMove();

            MSG msg;
            while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }

        if (!g_learningCancelled) {
            g_currentGame++;
            SendMessage(hwndProg, WM_USER + 100, g_currentGame, 0);
        }
    }

    EnableWindow(hwnd, TRUE);

    if (g_learningCancelled) {
        truncateUnfinishedGame();
        DestroyWindow(hwndProg);
        
        g_mode = oldMode;
        g_game.reset();
        g_gameOver = false;
        g_passCount = 0;
        g_isPassing = false;
        
        InvalidateRect(hwnd, NULL, TRUE);
        MessageBox(hwnd, "学習が途中で停止されました。\n中断した試合データは安全に削除されました。", "中断", MB_OK | MB_ICONEXCLAMATION);
        return;
    }

    DestroyWindow(hwndProg);
    g_mode = oldMode;
    g_game.reset();
    g_gameOver = false;
    g_passCount = 0;
    g_isPassing = false;
    
    InvalidateRect(hwnd, NULL, TRUE);
    MessageBox(hwnd, "すべての自動高速学習対戦が完了しました！\n学習結果をAIに反映しました。", "完了", MB_OK | MB_ICONINFORMATION);
}

void triggerAIMove(HWND hwnd) {
    if (g_gameOver || g_isAnimating) return;

    if (g_mode == 2 || (g_mode == 1 && g_game.turn == WHITE)) {
        vector<FlipAnimation> animations;
        if (!g_gameOver) {
            vector<Move> moves = g_game.getValidMoves();
            if (!moves.empty()) {
                Move aiMove = selectAIMoveBasedOnLevel(moves);
                animations = getFlipAnimationsForMove(aiMove.r, aiMove.c, g_game.turn);
                if (!animations.empty()) {
                    saveStepToDataset(aiMove, false);
                    g_flipAnimations = animations;
                    g_isAnimating = true;
                    g_animationStep = 0;
                    g_passCount = 0;
                    g_isPassing = false;
                    g_game.isValidMove(aiMove.r, aiMove.c, true);
                    g_game.turn = -g_game.turn;
                    InvalidateRect(hwnd, NULL, FALSE);
                    UpdateWindow(hwnd);
                    return;
                }
            }
        }

        processSingleMove();
        InvalidateRect(hwnd, NULL, FALSE);
        UpdateWindow(hwnd);

        if (!g_gameOver) {
            vector<Move> nextMoves = g_game.getValidMoves();
            if (nextMoves.empty()) {
                g_passCount++;
                Move dummyMove; dummyMove.r = 0; dummyMove.c = 0;
                saveStepToDataset(dummyMove, true);
                if (g_passCount >= 2) {
                    g_gameOver = true;
                    saveGameEndMarker();
                } else {
                    g_isPassing = true;
                    g_game.turn = -g_game.turn;
                }
                InvalidateRect(hwnd, NULL, FALSE);
                UpdateWindow(hwnd);
            }
        }
    }
}

int ShowLevelSelectBox(HWND hwndParent) {
    int res = MessageBox(hwndParent, "AIの初期レベルを最高（LV 10）にしますか？\n\n【はい】: レベル 10 (最適化最強モード)\n【いいえ】: レベル 5 (バランスモード)\n【キャンセル】: レベル 1 (ランダム)", "AIレベルの初期設定", MB_YESNOCANCEL | MB_ICONQUESTION);
    if (res == IDYES) return 10;
    if (res == IDNO) return 5;
    return 1;
}

LRESULT CALLBACK InputWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    static HWND hEdit, hBtn;
    switch (msg) {
        case WM_CREATE:
            CreateWindow("STATIC", "自動学習を行う試合数を入力してください（半角数字）:", 
                         WS_CHILD | WS_VISIBLE, 15, 15, 320, 20, hwnd, NULL, NULL, NULL);
            hEdit = CreateWindow("EDIT", g_inputBuffer, 
                                 WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 15, 45, 120, 24, hwnd, NULL, NULL, NULL);
            hBtn = CreateWindow("BUTTON", "確定", 
                                WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 150, 44, 80, 26, hwnd, (HMENU)IDOK, NULL, NULL);
            SetFocus(hEdit);
            break;
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK) {
                GetWindowText(hEdit, g_inputBuffer, sizeof(g_inputBuffer));
                g_inputDone = true;
                DestroyWindow(hwnd);
            }
            break;
        case WM_CLOSE:
            g_inputDone = true;
            DestroyWindow(hwnd);
            break;
        default:
            return DefWindowProc(hwnd, msg, wp, lp);
    }
    return 0;
}

int ShowInputBox(HWND hwndParent) {
    g_inputDone = false;
    HINSTANCE hInst = GetModuleHandle(NULL);
    
    WNDCLASSEX wcl = {0};
    wcl.cbSize = sizeof(WNDCLASSEX);
    wcl.lpfnWndProc = InputWndProc;
    wcl.hInstance = hInst;
    wcl.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcl.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wcl.lpszClassName = "InputBoxWindow";
    RegisterClassEx(&wcl);

    HWND hwndDlg = CreateWindowEx(WS_EX_DLGMODALFRAME, "InputBoxWindow", "自動学習数の指定", 
                                  WS_POPUPWINDOW | WS_CAPTION, CW_USEDEFAULT, CW_USEDEFAULT, 360, 130, 
                                  hwndParent, NULL, hInst, NULL);
    
    EnableWindow(hwndParent, FALSE);
    
    RECT rcOwner, rcDlg;
    GetWindowRect(hwndParent, &rcOwner);
    GetWindowRect(hwndDlg, &rcDlg);
    int x = rcOwner.left + ((rcOwner.right - rcOwner.left) - (rcDlg.right - rcDlg.left)) / 2;
    int y = rcOwner.top + ((rcOwner.bottom - rcOwner.top) - (rcDlg.bottom - rcDlg.top)) / 2;
    SetWindowPos(hwndDlg, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

    ShowWindow(hwndDlg, SW_SHOW);
    UpdateWindow(hwndDlg);

    MSG msg;
    while (!g_inputDone && GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    EnableWindow(hwndParent, TRUE);
    SetActiveWindow(hwndParent);

    int games = atoi(g_inputBuffer);
    if (games <= 0) games = 10;
    return games;
}

void estimateWinProbabilitiesForState(const Othello& state, int& blackPct, int& whitePct) {
    int blackCount = 0, whiteCount = 0;
    for (int i = 0; i < BOARD_SIZE; i++) {
        for (int j = 0; j < BOARD_SIZE; j++) {
            if (state.board[i][j] == BLACK) blackCount++;
            if (state.board[i][j] == WHITE) whiteCount++;
        }
    }
    int total = blackCount + whiteCount;
    if (total == 0) { blackPct = 50; whitePct = 50; return; }
    blackPct = (blackCount * 100) / total;
    whitePct = 100 - blackPct;
}

void estimateWinProbabilities(int& blackPct, int& whitePct) {
    estimateWinProbabilitiesForState(g_game, blackPct, whitePct);
}

void DrawBoard(HDC hdc, HWND hwnd) {
    RECT clientRect;
    GetClientRect(hwnd, &clientRect);
    int winW = clientRect.right - clientRect.left;
    int winH = clientRect.bottom - clientRect.top;

    int boardPadding = 10;
    int availableHeight = winH - (boardPadding * 2) - 25; 
    if (availableHeight < 100) availableHeight = 100;
    
    g_cellSize = availableHeight / BOARD_SIZE;
    int boardDisplaySize = g_cellSize * BOARD_SIZE;

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP hMemBitmap = CreateCompatibleBitmap(hdc, winW, winH);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(memDC, hMemBitmap);

    HBRUSH hBgBrush = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
    FillRect(memDC, &clientRect, hBgBrush);
    DeleteObject(hBgBrush);

    RECT infoPanel = { boardDisplaySize + 20, 10, winW - 10, winH - 10 };
    if (infoPanel.right > infoPanel.left) {
        HBRUSH hPanelBrush = CreateSolidBrush(RGB(250, 250, 250));
        FillRect(memDC, &infoPanel, hPanelBrush);
        HPEN hPanelPen = CreatePen(PS_SOLID, 1, RGB(200, 200, 200));
        HPEN hSavedPen = (HPEN)SelectObject(memDC, hPanelPen);
        Rectangle(memDC, infoPanel.left, infoPanel.top, infoPanel.right, infoPanel.bottom);
        SelectObject(memDC, hSavedPen);
        DeleteObject(hPanelPen);
        DeleteObject(hPanelBrush);
    }

    HBRUSH hBoardBrush = CreateSolidBrush(RGB(34, 139, 34));
    HBRUSH hOldBrush = (HBRUSH)SelectObject(memDC, hBoardBrush);
    Rectangle(memDC, boardPadding, boardPadding, boardPadding + boardDisplaySize, boardPadding + boardDisplaySize);

    HPEN hBoardPen = CreatePen(PS_SOLID, 2, RGB(20, 80, 20));
    HPEN hBoardOldPen = (HPEN)SelectObject(memDC, hBoardPen);
    for (int i = 0; i <= BOARD_SIZE; i++) {
        MoveToEx(memDC, boardPadding + i * g_cellSize, boardPadding, NULL); 
        LineTo(memDC, boardPadding + i * g_cellSize, boardPadding + boardDisplaySize);
        MoveToEx(memDC, boardPadding, boardPadding + i * g_cellSize, NULL); 
        LineTo(memDC, boardPadding + boardDisplaySize, boardPadding + i * g_cellSize);
    }
    SelectObject(memDC, hBoardOldPen);
    DeleteObject(hBoardPen);

    HBRUSH hBlackBrush = CreateSolidBrush(RGB(0, 0, 0));
    HBRUSH hWhiteBrush = CreateSolidBrush(RGB(255, 255, 255));
    HPEN hNullPen = CreatePen(PS_NULL, 0, RGB(0, 0, 0));
    HPEN hOldPen = (HPEN)SelectObject(memDC, hNullPen);

    for (int i = 0; i < BOARD_SIZE; i++) {
        for (int j = 0; j < BOARD_SIZE; j++) {
            FlipAnimation* anim = g_isAnimating ? findFlipAnimation(i, j) : NULL;
            int currentColor = g_game.board[i][j];
            int drawColor = currentColor;
            double widthScale = 1.0;
            double heightScale = 1.0;

            if (anim) {
                double progress = (double)g_animationStep / (double)ANIMATION_STEPS;
                double cosval = fabs(cos(progress * 3.14159265));
                widthScale = 0.15 + 0.85 * cosval;
                heightScale = 1.0;

                if (anim->fromColor == EMPTY) {
                    drawColor = anim->toColor;
                    widthScale = 0.15 + 0.85 * progress;
                } else {
                    if (progress < 0.5) {
                        drawColor = anim->fromColor;
                    } else {
                        drawColor = anim->toColor;
                    }
                }
            }

            if (drawColor == BLACK) {
                SelectObject(memDC, hBlackBrush);
            } else if (drawColor == WHITE) {
                SelectObject(memDC, hWhiteBrush);
            } else {
                continue;
            }

            int margin = (g_cellSize >= 20) ? 4 : 1;
            int pieceWidth = (int)((g_cellSize - (margin * 2)) * widthScale);
            int pieceHeight = (int)((g_cellSize - (margin * 2)) * heightScale);
            int left = boardPadding + j * g_cellSize + margin + ((g_cellSize - (margin * 2)) - pieceWidth) / 2;
            int top = boardPadding + i * g_cellSize + margin + ((g_cellSize - (margin * 2)) - pieceHeight) / 2;
            int right = left + pieceWidth;
            int bottom = top + pieceHeight;
            Ellipse(memDC, left, top, right, bottom);
        }
    }

    if (!g_gameOver && ((g_mode == 0) || (g_mode == 1 && g_game.turn == BLACK))) {
        g_gameValidMoves = g_game.getValidMoves();
        HBRUSH hGuideBrush = CreateSolidBrush(RGB(100, 220, 100));
        SelectObject(memDC, hGuideBrush);
        int radius = g_cellSize / 3;
        if (radius < 2) radius = 2;
        for (size_t i = 0; i < g_gameValidMoves.size(); i++) {
            int cx = boardPadding + g_gameValidMoves[i].c * g_cellSize + g_cellSize / 2;
            int cy = boardPadding + g_gameValidMoves[i].r * g_cellSize + g_cellSize / 2;
            Ellipse(memDC, cx - radius, cy - radius, cx + radius, cy + radius);
        }
        DeleteObject(hGuideBrush);

        if (g_supportMode && !g_gameValidMoves.empty()) {
            HBRUSH hSupportBrush = CreateSolidBrush(RGB(240, 200, 80));
            SelectObject(memDC, hSupportBrush);
            
            Move bestMove = QueryPythonAI(g_game, g_aiLevel);
            if (bestMove.r >= 0) {
                int sRadius = g_cellSize / 2 - 2;
                if (sRadius < 2) sRadius = 2;
                int cx = boardPadding + bestMove.c * g_cellSize + g_cellSize / 2;
                int cy = boardPadding + bestMove.r * g_cellSize + g_cellSize / 2;
                Ellipse(memDC, cx - sRadius, cy - sRadius, cx + sRadius, cy + sRadius);
            }
            DeleteObject(hSupportBrush);
        }
    }

    SelectObject(memDC, hOldBrush);
    SelectObject(memDC, hOldPen);
    DeleteObject(hBoardBrush);
    DeleteObject(hBlackBrush);
    DeleteObject(hWhiteBrush);
    DeleteObject(hNullPen);

    int blackCount = 0, whiteCount = 0;
    for (int i = 0; i < BOARD_SIZE; i++) {
        for (int j = 0; j < BOARD_SIZE; j++) {
            if (g_game.board[i][j] == BLACK) blackCount++;
            if (g_game.board[i][j] == WHITE) whiteCount++;
        }
    }

    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, RGB(60, 60, 60));
    for (int i = 0; i < BOARD_SIZE; i++) {
        char txt[2] = { (char)('A' + i), '\0' };
        TextOut(memDC, boardPadding + i * g_cellSize + (g_cellSize / 2) - 4, boardPadding + boardDisplaySize + 2, txt, 1);
    }
    for (int i = 0; i < BOARD_SIZE; i++) {
        char txt[2] = { (char)('1' + i), '\0' };
        TextOut(memDC, boardPadding + boardDisplaySize + 4, boardPadding + i * g_cellSize + (g_cellSize / 2) - 6, txt, 1);
    }

    if (infoPanel.right > infoPanel.left + 40) {
        SetBkMode(memDC, OPAQUE);
        SetBkColor(memDC, RGB(250, 250, 250));
        char buf[256];
        int infoX = infoPanel.left + 16;
        int infoY = infoPanel.top + 16;
        int lineHeight = 26;

        SetTextColor(memDC, RGB(0, 0, 0));
        sprintf(buf, "黒の石数: %d        ", blackCount); 
        TextOut(memDC, infoX, infoY, buf, (int)strlen(buf));
        sprintf(buf, "白の石数: %d        ", whiteCount);
        TextOut(memDC, infoX, infoY + lineHeight * 1, buf, (int)strlen(buf));

        int blackProb = 50, whiteProb = 50;
        estimateWinProbabilities(blackProb, whiteProb);
        sprintf(buf, "勝率予測: 黒 %d%% / 白 %d%%   ", blackProb, whiteProb);
        TextOut(memDC, infoX, infoY + lineHeight * 2, buf, (int)strlen(buf));

        sprintf(buf, "AIレベル: %d / 10     ", g_aiLevel);
        TextOut(memDC, infoX, infoY + lineHeight * 3, buf, (int)strlen(buf));

        sprintf(buf, "サポート: %s %s     ", (g_supportMode ? "ON " : "OFF"), (g_supportTarget == BLACK ? "黒" : "白"));
        TextOut(memDC, infoX, infoY + lineHeight * 4, buf, (int)strlen(buf));

        if (g_isThinking) {
            SetTextColor(memDC, RGB(220, 50, 50));
            sprintf(buf, "AI思考中... (Python連動)");
            TextOut(memDC, infoX, infoY + lineHeight * 5, buf, (int)strlen(buf));
        } else {
            sprintf(buf, "                               ");
            TextOut(memDC, infoX, infoY + lineHeight * 5, buf, (int)strlen(buf));
        }

        int btnW = 80;
        int btnH = 26;
        int btnY = infoY + lineHeight * 7;
        
        g_btnRectPvP.left = infoX; g_btnRectPvP.top = btnY; g_btnRectPvP.right = infoX + btnW; g_btnRectPvP.bottom = btnY + btnH;
        g_btnRectPvE.left = infoX + btnW + 10; g_btnRectPvE.top = btnY; g_btnRectPvE.right = infoX + btnW * 2 + 10; g_btnRectPvE.bottom = btnY + btnH;
        g_btnRectEvE.left = infoX + btnW * 2 + 20; g_btnRectEvE.top = btnY; g_btnRectEvE.right = infoX + btnW * 3 + 20; g_btnRectEvE.bottom = btnY + btnH;

        RECT buttonRects[3];
        buttonRects[0] = g_btnRectPvP; buttonRects[1] = g_btnRectPvE; buttonRects[2] = g_btnRectEvE;
        const char* buttonLabels[3] = { "人対人", "人対AI", "AI対AI" };

        SetBkMode(memDC, TRANSPARENT);
        for (int i = 0; i < 3; i++) {
            HBRUSH hBtnBrush = CreateSolidBrush((g_mode == i ? RGB(60, 130, 220) : RGB(220, 220, 220)));
            HPEN hBtnBorder = CreatePen(PS_SOLID, 1, RGB(150, 150, 150));
            HPEN hOldBorder = (HPEN)SelectObject(memDC, hBtnBorder);
            HBRUSH hOldBtnBrush = (HBRUSH)SelectObject(memDC, hBtnBrush);
            RoundRect(memDC, buttonRects[i].left, buttonRects[i].top, buttonRects[i].right, buttonRects[i].bottom, 8, 8);
            SelectObject(memDC, hOldBtnBrush);
            SelectObject(memDC, hOldBorder);
            DeleteObject(hBtnBrush);
            DeleteObject(hBtnBorder);

            SetTextColor(memDC, (g_mode == i ? RGB(255, 255, 255) : RGB(0, 0, 0)));
            TextOut(memDC, buttonRects[i].left + 14, buttonRects[i].top + 5, buttonLabels[i], (int)strlen(buttonLabels[i]));
        }

        SetBkMode(memDC, OPAQUE);
        SetTextColor(memDC, RGB(120, 120, 120));
        int helpY = winH - 160;
        if (helpY > btnY + btnH + 20) {
            sprintf(buf, "[1]-[0]: AI強度変更          "); TextOut(memDC, infoX, helpY, buf, (int)strlen(buf));
            sprintf(buf, "[R]: リセット (学習継続)     "); TextOut(memDC, infoX, helpY + 24, buf, (int)strlen(buf));
            sprintf(buf, "[S]: サポート ON/OFF         "); TextOut(memDC, infoX, helpY + 48, buf, (int)strlen(buf));
            sprintf(buf, "[T]: サポート対象切替        "); TextOut(memDC, infoX, helpY + 72, buf, (int)strlen(buf));
            sprintf(buf, "左クリックで石を配置します  "); TextOut(memDC, infoX, helpY + 96, buf, (int)strlen(buf));
        }

        if (g_hThinkingProgress) {
            MoveWindow(g_hThinkingProgress, infoX, infoY + lineHeight * 6, (infoPanel.right - infoX - 20 < 150) ? 150 : infoPanel.right - infoX - 20, 16, TRUE);
        }
    }

    BitBlt(hdc, 0, 0, winW, winH, memDC, 0, 0, SRCCOPY);

    SelectObject(memDC, hOldBitmap);
    DeleteObject(hMemBitmap);
    DeleteDC(memDC);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            InitPythonProcess();
            g_aiLevel = ShowLevelSelectBox(hwnd);

            g_hThinkingProgress = CreateWindow(PROGRESS_CLASS, NULL, WS_CHILD | PBS_SMOOTH, 0, 0, 0, 0, hwnd, NULL, GetModuleHandle(NULL), NULL);

            if (MessageBox(hwnd, "裏で一括して「AI vs AI 自動高速学習」を実行しますか？", "高速学習の確認", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                g_totalGames = ShowInputBox(hwnd); 
                runFastLearning(hwnd);
            }
            g_mode = 1;
            SetTimer(hwnd, 1, 50, NULL); 
            break;

        case WM_SIZE:
            InvalidateRect(hwnd, NULL, FALSE);
            break;

        case WM_TIMER:
            if (g_isAnimating) {
                g_animationStep++;
                if (g_animationStep > ANIMATION_STEPS) {
                    g_isAnimating = false;
                    g_animationStep = 0;
                    g_flipAnimations.clear();
                }
                InvalidateRect(hwnd, NULL, FALSE);
                break;
            }
            if (!g_gameOver) {
                if (g_mode == 2 || (g_mode == 1 && g_game.turn == WHITE)) {
                    triggerAIMove(hwnd);
                }
            }
            break;

        case WM_KEYDOWN:
            if (wp >= '1' && wp <= '9') {
                g_aiLevel = (int)(wp - '0');
                InvalidateRect(hwnd, NULL, TRUE);
            } else if (wp == '0') {
                g_aiLevel = 10;
                InvalidateRect(hwnd, NULL, TRUE);
            } else if (wp == 'R' || wp == 'r') {
                g_game.reset();
                g_gameOver = false;
                g_passCount = 0;
                g_isPassing = false;
                InvalidateRect(hwnd, NULL, TRUE);
            } else if (wp == 'S' || wp == 's') {
                g_supportMode = !g_supportMode;
                InvalidateRect(hwnd, NULL, TRUE); 
            } else if (wp == 'T' || wp == 't') {
                g_supportTarget = (g_supportTarget == BLACK ? WHITE : BLACK);
                InvalidateRect(hwnd, NULL, TRUE); 
            }
            break;

        case WM_LBUTTONDOWN: {
            int x = LOWORD(lp); int y = HIWORD(lp);

            if (x >= g_btnRectPvP.left && x <= g_btnRectPvP.right && y >= g_btnRectPvP.top && y <= g_btnRectPvP.bottom) {
                g_mode = 0; InvalidateRect(hwnd, NULL, TRUE); break;
            }
            if (x >= g_btnRectPvE.left && x <= g_btnRectPvE.right && y >= g_btnRectPvE.top && y <= g_btnRectPvE.bottom) {
                g_mode = 1; InvalidateRect(hwnd, NULL, TRUE); break;
            }
            if (x >= g_btnRectEvE.left && x <= g_btnRectEvE.right && y >= g_btnRectEvE.top && y <= g_btnRectEvE.bottom) {
                g_mode = 2; InvalidateRect(hwnd, NULL, TRUE); break;
            }

            if (!g_gameOver && !g_isAnimating) {
                int boardPadding = 10;
                int c = (x - boardPadding) / g_cellSize; 
                int r = (y - boardPadding) / g_cellSize;

                if (g_mode == 0 || (g_mode == 1 && g_game.turn == BLACK)) {
                    vector<FlipAnimation> animations = getFlipAnimationsForMove(r, c, g_game.turn);
                    if (!animations.empty() && g_game.isValidMove(r, c, true)) {
                        Move humanMove; humanMove.r = r; humanMove.c = c;
                        saveStepToDataset(humanMove, false);
                        g_flipAnimations = animations;
                        g_isAnimating = true;
                        g_animationStep = 0;
                        g_passCount = 0; g_isPassing = false;
                        g_game.turn = -g_game.turn;
                        
                        InvalidateRect(hwnd, NULL, FALSE);
                        UpdateWindow(hwnd);

                        if (g_mode == 0 && !g_gameOver) {
                            vector<Move> nextMoves = g_game.getValidMoves();
                            if (nextMoves.empty()) {
                                g_passCount++;
                                Move dummyMove; dummyMove.r = 0; dummyMove.c = 0;
                                saveStepToDataset(dummyMove, true);
                                if (g_passCount >= 2) {
                                    g_gameOver = true;
                                    saveGameEndMarker();
                                } else {
                                    g_isPassing = true;
                                    g_game.turn = -g_game.turn;
                                }
                                InvalidateRect(hwnd, NULL, FALSE);
                            }
                        }
                    }
                }
            }
            break;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
            DrawBoard(hdc, hwnd); EndPaint(hwnd, &ps);
            break;
        }

        case WM_ERASEBKGND:
            return 1; 

        case WM_DESTROY:
            KillTimer(hwnd, 1);
            ClosePythonProcess();
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProc(hwnd, msg, wp, lp);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow) {
    (void)hPrev; (void)lpCmd; srand((unsigned int)time(NULL));

    HMODULE hComCtl = LoadLibrary("comctl32.dll");
    if (hComCtl) {
        INITCOMMONCONTROLSEXPROC pInitCommonControlsEx = 
            (INITCOMMONCONTROLSEXPROC)GetProcAddress(hComCtl, "InitCommonControlsEx");
        if (pInitCommonControlsEx) {
            INITCOMMONCONTROLSEX icex;
            icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
            icex.dwICC = ICC_PROGRESS_CLASS;
            pInitCommonControlsEx(&icex);
        }
    }

    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, hInst, NULL, LoadCursor(NULL, IDC_ARROW), NULL, NULL, "OthelloAI_v5", NULL };
    RegisterClassEx(&wc);
    
    HWND hwnd = CreateWindow("OthelloAI_v5", "マルチモード対応・Python連動型高性能オセロAI", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 760, 440, NULL, NULL, hInst, NULL);
    ShowWindow(hwnd, nShow); UpdateWindow(hwnd);
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    
    UnregisterClass("OthelloAI_v5", wc.hInstance);
    
    if (hComCtl) {
        FreeLibrary(hComCtl);
    }
    return (int)msg.wParam;
}
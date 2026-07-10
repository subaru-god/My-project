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
int g_cellSize = 50; // ウィンドウサイズに応じて動的に変化
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

// ボタン位置（WndProc内、DrawBoard内でリサイズに合わせて動的に計算）
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

// AI思考の進捗表示用グローバル変数
HWND g_hThinkingProgress = NULL;
bool g_isThinking = false;

void convertOldLogsToBinary();
void trainAIFromDataset();
void saveStepToDataset(Move nextMove, bool isPass);
void saveGameEndMarker();
void estimateWinProbabilitiesForState(const Othello& state, int& blackPct, int& whitePct);
void runFastLearning(HWND hwnd);
void triggerAIMove(HWND hwnd);
void truncateUnfinishedGame();
vector<FlipAnimation> getFlipAnimationsForMove(int r, int c, int player);
FlipAnimation* findFlipAnimation(int r, int c);

const int STATIC_WEIGHT[8][8] = {
    { 120, -40,  20,   5,   5,  20, -40, 120},
    {-40, -70,  -5,  -5,  -5,  -5, -70, -40},
    {  20,  -5,  15,   3,   3,  15,  -5,  20},
    {   5,  -5,   3,   3,   3,   3,  -5,   5},
    {   5,  -5,   3,   3,   3,   3,  -5,   5},
    {  20,  -5,  15,   3,   3,  15,  -5,  20},
    {-40, -70,  -5,  -5,  -5,  -5, -70, -40},
    { 120, -40,  20,   5,   5,  20, -40, 120}
};

int evaluateBoard(const Othello& state, int aiPlayer, int emptyCount) {
    int myScore = 0;
    int oppScore = 0;
    int myStones = 0;
    int oppStones = 0;

    for (int i = 0; i < BOARD_SIZE; i++) {
        for (int j = 0; j < BOARD_SIZE; j++) {
            if (state.board[i][j] == EMPTY) continue;
            
            int w = STATIC_WEIGHT[i][j] + (int)(g_weightTable[i][j] * 0.05);
            
            if ((i == 1 && j == 1 && state.board[0][0] != EMPTY) ||
                (i == 1 && j == 6 && state.board[0][7] != EMPTY) ||
                (i == 6 && j == 1 && state.board[7][0] != EMPTY) ||
                (i == 6 && j == 6 && state.board[7][7] != EMPTY)) {
                w += 60; 
            }

            if (state.board[i][j] == aiPlayer) {
                myScore += w;
                myStones++;
            } else {
                oppScore += w;
                oppStones++;
            }
        }
    }

    if (emptyCount <= 12) {
        return (myStones - oppStones) * 10000;
    }

    Othello tempState = state;
    tempState.turn = aiPlayer;
    int myMoves = (int)tempState.getValidMoves().size();
    tempState.turn = -aiPlayer;
    int oppMoves = (int)tempState.getValidMoves().size();

    int stoneCountPenalty = 0;
    if (emptyCount > 28) { 
        stoneCountPenalty = -(myStones * 12);
    }

    return (myScore - oppScore) + (myMoves - oppMoves) * 30 + stoneCountPenalty;
}

struct RatedMove {
    Move m;
    int rate;
};

int alphaBetaSearch(Othello& state, int depth, int alpha, int beta, int aiPlayer, bool isMax, int emptyCount) {
    if (depth == 0) {
        return evaluateBoard(state, aiPlayer, emptyCount);
    }

    vector<Move> moves = state.getValidMoves();
    if (moves.empty()) {
        Othello nextState = state;
        nextState.turn = -nextState.turn;
        vector<Move> oppMoves = nextState.getValidMoves();
        if (oppMoves.empty()) {
            int myCount = 0, oppCount = 0;
            for (int i = 0; i < BOARD_SIZE; i++) {
                for (int j = 0; j < BOARD_SIZE; j++) {
                    if (state.board[i][j] == aiPlayer) myCount++;
                    else if (state.board[i][j] == -aiPlayer) oppCount++;
                }
            }
            if (myCount > oppCount) return 1000000 + (myCount - oppCount);
            if (myCount < oppCount) return -1000000 - (oppCount - myCount);
            return 0;
        }
        return alphaBetaSearch(nextState, depth - 1, alpha, beta, aiPlayer, !isMax, emptyCount);
    }

    vector<RatedMove> ratedMoves;
    for (size_t i = 0; i < moves.size(); i++) {
        RatedMove rm;
        rm.m = moves[i];
        rm.rate = STATIC_WEIGHT[moves[i].r][moves[i].c];
        if ((moves[i].r == 1 && moves[i].c == 1) || (moves[i].r == 1 && moves[i].c == 6) ||
            (moves[i].r == 6 && moves[i].c == 1) || (moves[i].r == 6 && moves[i].c == 6)) {
            rm.rate -= 50;
        }
        ratedMoves.push_back(rm);
    }

    for (size_t i = 0; i < ratedMoves.size(); i++) {
        for (size_t j = i + 1; j < ratedMoves.size(); j++) {
            if ((isMax && ratedMoves[i].rate < ratedMoves[j].rate) || (!isMax && ratedMoves[i].rate > ratedMoves[j].rate)) {
                RatedMove tmp = ratedMoves[i];
                ratedMoves[i] = ratedMoves[j];
                ratedMoves[j] = tmp;
            }
        }
    }

    if (isMax) {
        int maxEval = -9999999;
        for (size_t i = 0; i < ratedMoves.size(); i++) {
            Othello nextState = state;
            nextState.isValidMove(ratedMoves[i].m.r, ratedMoves[i].m.c, true);
            nextState.turn = -nextState.turn;
            
            int eval = alphaBetaSearch(nextState, depth - 1, alpha, beta, aiPlayer, false, emptyCount - 1);
            if (eval > maxEval) maxEval = eval;
            if (eval > alpha) alpha = eval;
            if (beta <= alpha) break; 
        }
        return maxEval;
    } else {
        int minEval = 9999999;
        for (size_t i = 0; i < ratedMoves.size(); i++) {
            Othello nextState = state;
            nextState.isValidMove(ratedMoves[i].m.r, ratedMoves[i].m.c, true);
            nextState.turn = -nextState.turn;

            int eval = alphaBetaSearch(nextState, depth - 1, alpha, beta, aiPlayer, true, emptyCount - 1);
            if (eval < minEval) minEval = eval;
            if (eval < beta) beta = eval;
            if (beta <= alpha) break; 
        }
        return minEval;
    }
}

Move selectAIMoveBasedOnLevel(const vector<Move>& moves) {
    if (moves.empty()) {
        Move m = {0, 0}; return m;
    }

    int rate = (g_aiLevel - 1) * 11;
    if (g_aiLevel < 5 && (rand() % 100) >= rate) {
        return moves[rand() % moves.size()];
    }

    int maxDepth = 1;
    if (g_aiLevel == 5) maxDepth = 3;
    else if (g_aiLevel == 6) maxDepth = 4;
    else if (g_aiLevel == 7) maxDepth = 5;
    else if (g_aiLevel == 8) maxDepth = 6;
    else if (g_aiLevel == 9) maxDepth = 7;
    else if (g_aiLevel >= 10) maxDepth = 8; 

    int emptyCount = 0;
    for (int i = 0; i < BOARD_SIZE; i++) {
        for (int j = 0; j < BOARD_SIZE; j++) {
            if (g_game.board[i][j] == EMPTY) emptyCount++;
        }
    }

    if (g_aiLevel >= 10 && emptyCount <= 12) {
        maxDepth = emptyCount;
    }

    int aiPlayer = g_game.turn;
    size_t bestIdx = 0;

    g_isThinking = true;
    if (g_hThinkingProgress) {
        SendMessage(g_hThinkingProgress, PBM_SETRANGE, 0, MAKELPARAM(0, moves.size()));
        SendMessage(g_hThinkingProgress, PBM_SETPOS, 0, 0);
        ShowWindow(g_hThinkingProgress, SW_SHOW);
    }

    for (int currentDepth = 2; currentDepth <= maxDepth; currentDepth += 2) {
        int bestScore = -99999999;
        for (size_t i = 0; i < moves.size(); i++) {
            Othello nextState = g_game;
            nextState.isValidMove(moves[i].r, moves[i].c, true);
            nextState.turn = -nextState.turn;

            int score = alphaBetaSearch(nextState, currentDepth - 1, -99999999, 99999999, aiPlayer, false, emptyCount - 1);

            int r = moves[i].r;
            int c = moves[i].c;
            if ((r == 1 && c == 1 && g_game.board[0][0] == EMPTY) ||
                (r == 1 && c == 6 && g_game.board[0][7] == EMPTY) ||
                (r == 6 && c == 1 && g_game.board[7][0] == EMPTY) ||
                (r == 6 && c == 6 && g_game.board[7][7] == EMPTY)) {
                score -= 40000;
            }

            if (score > bestScore) {
                bestScore = score;
                bestIdx = i;
            }

            // 最深層のループ時に、プログレスバーの進行状況を進める
            if (currentDepth >= maxDepth - 1 && g_hThinkingProgress) {
                SendMessage(g_hThinkingProgress, PBM_SETPOS, i + 1, 0);
                // Windowsメッセージを強制処理してプログレスバーの描画を反映
                MSG msg;
                while (PeekMessage(&msg, g_hThinkingProgress, 0, 0, PM_REMOVE)) {
                    DispatchMessage(&msg);
                }
            }
        }
    }

    if (g_hThinkingProgress) {
        ShowWindow(g_hThinkingProgress, SW_HIDE);
    }
    g_isThinking = false;

    return moves[bestIdx];
}

void convertOldLogsToBinary() {
    WIN32_FIND_DATA findData;
    HANDLE hFind = FindFirstFile("log\\*.log", &findData);
    
    if (hFind != INVALID_HANDLE_VALUE) {
        bool hasConvertedAny = false;
        do {
            string fullPath = "log\\" + string(findData.cFileName);
            ifstream logFile(fullPath.c_str());
            if (logFile) {
                string line;
                hasConvertedAny = true;
                while (getline(logFile, line)) {
                    if (line.empty()) continue;
                    
                    int vals[67]; 
                    int idx = 0;
                    string token;
                    string tempLine = line;
                    
                    while (!tempLine.empty() && idx < 67) {
                        size_t nextPos = tempLine.find(',');
                        if (nextPos == string::npos) {
                            vals[idx++] = atoi(tempLine.c_str());
                            break;
                        }
                        token = tempLine.substr(0, nextPos);
                        vals[idx++] = atoi(token.c_str());
                        tempLine.erase(0, nextPos + 1);
                    }
                    
                    if(idx >= 66) {
                        int r = vals[64];
                        int c = vals[65];
                        Move m; m.r = r; m.c = c;
                        saveStepToDataset(m, false);
                    }
                }
                logFile.close();
                string bakPath = fullPath + ".bak";
                MoveFile(fullPath.c_str(), bakPath.c_str());
            }
        } while (FindNextFile(hFind, &findData));
        FindClose(hFind);
        if (hasConvertedAny) {
            saveGameEndMarker();
        }
    }
}

void trainAIFromDataset() {
    convertOldLogsToBinary();

    for(int i=0; i<BOARD_SIZE; i++) {
        for(int j=0; j<BOARD_SIZE; j++) g_weightTable[i][j] = 0.0;
    }
    
    g_totalDataCount = 0;
    ifstream dbFile(DATA_FILE_PATH, ios::binary);
    if (!dbFile) return;

    dbFile.seekg(0, ios::end);
    streampos fileSize = dbFile.tellg();
    dbFile.seekg(0, ios::beg);

    if (fileSize <= 0) {
        dbFile.close();
        return;
    }

    vector<unsigned char> data((size_t)fileSize);
    dbFile.read((char*)&data[0], fileSize);
    dbFile.close();

    Othello simGame;
    size_t idx = 0;

    while (idx < data.size()) {
        simGame.reset();

        while (idx < data.size()) {
            unsigned char code = data[idx++];
            
            if (code == 0xFE) { 
                break; 
            }

            if (code == MOVE_PASS) {
                simGame.turn = -simGame.turn;
                continue;
            }

            int r = code >> 4;   
            int c = code & 0x0F; 

            if (r >= 0 && r < BOARD_SIZE && c >= 0 && c < BOARD_SIZE) {
                g_weightTable[r][c] += 1.0;
                g_totalDataCount++;

                simGame.isValidMove(r, c, true);
                simGame.turn = -simGame.turn;
            }
        }
    }

    g_weightTable[0][0] += 50; g_weightTable[0][7] += 50;
    g_weightTable[7][0] += 50; g_weightTable[7][7] += 50;
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
        trainAIFromDataset(); 
        
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
    trainAIFromDataset();
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

Move selectAIMoveForEstimate(Othello& state) {
    vector<Move> moves = state.getValidMoves();
    if (moves.empty()) {
        Move emptyMove; emptyMove.r = -1; emptyMove.c = -1;
        return emptyMove;
    }
    return moves[rand() % moves.size()];
}

void estimateWinProbabilitiesForState(const Othello& state, int& blackPct, int& whitePct) {
    int blackWins = 0;
    int whiteWins = 0;
    int draws = 0;
    const int simulations = 100; 

    for (int sim = 0; sim < simulations; sim++) {
        Othello temp = state;
        int passCount = 0;
        while (true) {
            vector<Move> moves = temp.getValidMoves();
            if (moves.empty()) {
                passCount++;
                if (passCount >= 2) break;
                temp.turn = -temp.turn;
                continue;
            }
            passCount = 0;
            Move m = selectAIMoveForEstimate(temp);
            if (m.r >= 0) {
                temp.isValidMove(m.r, m.c, true);
            }
            temp.turn = -temp.turn;
        }

        int blackCount = 0, whiteCount = 0;
        for (int i = 0; i < BOARD_SIZE; i++) {
            for (int j = 0; j < BOARD_SIZE; j++) {
                if (temp.board[i][j] == BLACK) blackCount++;
                if (temp.board[i][j] == WHITE) whiteCount++;
            }
        }
        if (blackCount > whiteCount) blackWins++;
        else if (whiteCount > blackCount) whiteWins++;
        else draws++;
    }

    blackPct = (int)((double)blackWins + (double)draws * 0.5) * 100 / simulations;
    whitePct = 100 - blackPct; 
}

void estimateWinProbabilities(int& blackPct, int& whitePct) {
    if (g_gameOver) {
        int blackCount = 0, whiteCount = 0;
        for (int i = 0; i < BOARD_SIZE; i++) {
            for (int j = 0; j < BOARD_SIZE; j++) {
                if (g_game.board[i][j] == BLACK) blackCount++;
                if (g_game.board[i][j] == WHITE) whiteCount++;
            }
        }
        if (blackCount > whiteCount) { blackPct = 100; whitePct = 0; }
        else if (whiteCount > blackCount) { blackPct = 0; whitePct = 100; }
        else { blackPct = 50; whitePct = 50; }
        return;
    }

    estimateWinProbabilitiesForState(g_game, blackPct, whitePct);
}

void DrawBoard(HDC hdc, HWND hwnd) {
    RECT clientRect;
    GetClientRect(hwnd, &clientRect);
    int winW = clientRect.right - clientRect.left;
    int winH = clientRect.bottom - clientRect.top;

    // 【最優先リサイズ】オセロ盤（譜面）のサイズをウィンドウの短辺（高さか幅）を基準に動的決定
    int boardPadding = 10;
    int availableHeight = winH - (boardPadding * 2) - 25; // 下部に多少の余白
    if (availableHeight < 100) availableHeight = 100;
    
    // 盤面が最優先で正方形になるようセルサイズを決定
    g_cellSize = availableHeight / BOARD_SIZE;
    int boardDisplaySize = g_cellSize * BOARD_SIZE;

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP hMemBitmap = CreateCompatibleBitmap(hdc, winW, winH);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(memDC, hMemBitmap);

    HBRUSH hBgBrush = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
    FillRect(memDC, &clientRect, hBgBrush);
    DeleteObject(hBgBrush);

    // 情報パネルを盤面の右隣に追従させる
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

    // オセロ盤背景描画
    HBRUSH hBoardBrush = CreateSolidBrush(RGB(34, 139, 34));
    HBRUSH hOldBrush = (HBRUSH)SelectObject(memDC, hBoardBrush);
    Rectangle(memDC, boardPadding, boardPadding, boardPadding + boardDisplaySize, boardPadding + boardDisplaySize);

    // 罫線描画
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

    // 石の描画
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

    // ガイド用ドットの描画
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
            int bestScore = -1000;
            Move bestMove = { -1, -1 };
            for (size_t i = 0; i < g_gameValidMoves.size(); i++) {
                Othello testState = g_game;
                testState.turn = g_supportTarget;
                testState.isValidMove(g_gameValidMoves[i].r, g_gameValidMoves[i].c, true);
                int blackPct, whitePct;
                estimateWinProbabilitiesForState(testState, blackPct, whitePct);
                int score = (g_supportTarget == BLACK ? blackPct : whitePct);
                if (score > bestScore) {
                    bestScore = score;
                    bestMove = g_gameValidMoves[i];
                }
            }
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

    // 座標（A~H, 1~8）の動的描画
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

    // 情報テキスト描画
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

        // AI思考中のみテキスト表示（下に本物のプログレスバーが配置されます）
        if (g_isThinking) {
            SetTextColor(memDC, RGB(220, 50, 50));
            sprintf(buf, "AI思考中... (完了度)");
            TextOut(memDC, infoX, infoY + lineHeight * 5, buf, (int)strlen(buf));
        } else {
            sprintf(buf, "                               ");
            TextOut(memDC, infoX, infoY + lineHeight * 5, buf, (int)strlen(buf));
        }

        // モード変更ボタンの動的配置計算
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

        // 操作ヘルプメッセージの動的配置
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

        // 思考中プログレスバーの位置を情報パネルの大きさに合わせて同期調整
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
            trainAIFromDataset(); 
            g_aiLevel = ShowLevelSelectBox(hwnd);

            // インライン思考用プログレスバーの生成（初期は非表示）
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
                // 最優先リサイズ後の座標系に合わせてクリック判定
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
    
    HWND hwnd = CreateWindow("OthelloAI_v5", "マルチモード対応・高速学習オセロシステム", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 760, 440, NULL, NULL, hInst, NULL);
    ShowWindow(hwnd, nShow); UpdateWindow(hwnd);
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    
    UnregisterClass("OthelloAI_v5", wc.hInstance);
    
    if (hComCtl) {
        FreeLibrary(hComCtl);
    }
    return (int)msg.wParam;
}
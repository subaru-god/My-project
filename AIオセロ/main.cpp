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
const int CELL_SIZE = 50;
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

const RECT BTN_RECT_PVP = { 430, 170, 520, 200 };
const RECT BTN_RECT_PVE = { 530, 170, 620, 200 };
const RECT BTN_RECT_EVE = { 630, 170, 720, 200 };

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

// --- 最強AIのための評価関数とアルファベータ探索ルーチン ---

// 固定の位置評価テーブル（静的譜面パターン評価のベース）
const int STATIC_WEIGHT[8][8] = {
    { 100, -40,  20,   5,   5,  20, -40, 100},
    {-40, -70,  -5,  -5,  -5,  -5, -70, -40},
    {  20,  -5,  15,   3,   3,  15,  -5,  20},
    {   5,  -5,   3,   3,   3,   3,  -5,   5},
    {   5,  -5,   3,   3,   3,   3,  -5,   5},
    {  20,  -5,  15,   3,   3,  15,  -5,  20},
    {-40, -70,  -5,  -5,  -5,  -5, -70, -40},
    { 100, -40,  20,   5,   5,  20, -40, 100}
};

// 盤面全体の評価関数 (譜面パターン＋着手可能数＋確定石)
int evaluateBoard(const Othello& state, int aiPlayer) {
    int myScore = 0;
    int oppScore = 0;
    int emptyCount = 0;
    int myStones = 0;
    int oppStones = 0;

    // 1. 石数と位置評価、データセットから学習した重みの加算
    for (int i = 0; i < BOARD_SIZE; i++) {
        for (int j = 0; j < BOARD_SIZE; j++) {
            if (state.board[i][j] == EMPTY) {
                emptyCount++;
                continue;
            }
            
            // 基本位置評価 + 自己学習データセットの重みを反映
            int w = STATIC_WEIGHT[i][j] + (int)(g_weightTable[i][j] * 0.1);
            
            // 角の隣(X打ち・C打ち)の動的評価（角が取られているなら悪手ではない）
            if ((i == 1 && j == 1 && state.board[0][0] != EMPTY) ||
                (i == 1 && j == 6 && state.board[0][7] != EMPTY) ||
                (i == 6 && j == 1 && state.board[7][0] != EMPTY) ||
                (i == 6 && j == 6 && state.board[7][7] != EMPTY)) {
                w += 50; 
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

    // 終盤（空きマス16以下）なら純粋な石数の多さを最優先（完全勝利モード）
    if (emptyCount <= 16) {
        return (myStones - oppStones) * 10000;
    }

    // 2. 着手可能数（モビリティ評価：相手の行動を制限する）
    Othello tempState = state;
    tempState.turn = aiPlayer;
    int myMoves = (int)tempState.getValidMoves().size();
    tempState.turn = -aiPlayer;
    int oppMoves = (int)tempState.getValidMoves().size();

    // 3. 序盤・中盤の戦略的ペナルティ（石を多く取りすぎないようにする）
    int stoneCountPenalty = 0;
    if (emptyCount > 30) { // 序盤は石を少なく保つ方が強い
        stoneCountPenalty = -(myStones * 15);
    }

    // 複合評価を合算して返す
    return (myScore - oppScore) + (myMoves - oppMoves) * 25 + stoneCountPenalty;
}

// 高速化のための着手ソート用構造体
struct RatedMove {
    Move m;
    int rate;
};

// アルファベータ探索本体 (時間制限なし全探索対応)
int alphaBetaSearch(Othello& state, int depth, int alpha, int beta, int aiPlayer, bool isMax) {
    if (depth == 0) {
        return evaluateBoard(state, aiPlayer);
    }

    vector<Move> moves = state.getValidMoves();
    if (moves.empty()) {
        // パスの場合、相手に手番を回して深く読む
        Othello nextState = state;
        nextState.turn = -nextState.turn;
        vector<Move> oppMoves = nextState.getValidMoves();
        if (oppMoves.empty()) {
            // 両者パス（ゲーム終了）なら石数判定
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
        return alphaBetaSearch(nextState, depth - 1, alpha, beta, aiPlayer, !isMax);
    }

    // Move Ordering（評価の高い手を先に探索して刈り込み効率をアップ）
    vector<RatedMove> ratedMoves;
    for (size_t i = 0; i < moves.size(); i++) {
        RatedMove rm;
        rm.m = moves[i];
        rm.rate = STATIC_WEIGHT[moves[i].r][moves[i].c];
        ratedMoves.push_back(rm);
    }
    // 単純なバブルソート (BCC32対応)
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
            
            int eval = alphaBetaSearch(nextState, depth - 1, alpha, beta, aiPlayer, false);
            if (eval > maxEval) maxEval = eval;
            if (eval > alpha) alpha = eval;
            if (beta <= alpha) break; // βカット
        }
        return maxEval;
    } else {
        int minEval = 9999999;
        for (size_t i = 0; i < ratedMoves.size(); i++) {
            Othello nextState = state;
            nextState.isValidMove(ratedMoves[i].m.r, ratedMoves[i].m.c, true);
            nextState.turn = -nextState.turn;

            int eval = alphaBetaSearch(nextState, depth - 1, alpha, beta, aiPlayer, true);
            if (eval < minEval) minEval = eval;
            if (eval < beta) beta = eval;
            if (beta <= alpha) break; // αカット
        }
        return minEval;
    }
}

// 既存のAI思考処理を「最強アルファベータ探索」に完全リプレイス
Move selectAIMoveBasedOnLevel(const vector<Move>& moves) {
    if (moves.empty()) {
        Move m = {0, 0}; return m;
    }

    // レベル1?4は既存互換の確率的ランダム要素
    int rate = (g_aiLevel - 1) * 11;
    if (g_aiLevel < 5 && (rand() % 100) >= rate) {
        return moves[rand() % moves.size()];
    }

    // レベルに応じた探索深さの設定 (レベル10＝最強モード・時間無制限)
    int maxDepth = 1;
    if (g_aiLevel == 5) maxDepth = 3;
    else if (g_aiLevel == 6) maxDepth = 4;
    else if (g_aiLevel == 7) maxDepth = 5;
    else if (g_aiLevel == 8) maxDepth = 6;
    else if (g_aiLevel == 9) maxDepth = 8;
    else if (g_aiLevel >= 10) maxDepth = 10; // 10手先読み

    // 空きマスのカウント
    int emptyCount = 0;
    for (int i = 0; i < BOARD_SIZE; i++) {
        for (int j = 0; j < BOARD_SIZE; j++) {
            if (g_game.board[i][j] == EMPTY) emptyCount++;
        }
    }

    // 【終盤完全読み】空きマスが16以下なら、残り全てのマスを完全に読み切る
    if (g_aiLevel >= 10 && emptyCount <= 16) {
        maxDepth = emptyCount;
    }

    int aiPlayer = g_game.turn;
    int bestScore = -99999999;
    size_t bestIdx = 0;

    // 最善手をαβ探索
    for (size_t i = 0; i < moves.size(); i++) {
        Othello nextState = g_game;
        nextState.isValidMove(moves[i].r, moves[i].c, true);
        nextState.turn = -nextState.turn;

        // 時間制限なしで深さの限界までαβ探索を実行
        int score = alphaBetaSearch(nextState, maxDepth - 1, -99999999, 99999999, aiPlayer, false);

        // 特定の危険なパターン（角の隣など）への追加補正（安全策）
        int r = moves[i].r;
        int c = moves[i].c;
        if ((r == 1 && c == 1 && g_game.board[0][0] == EMPTY) ||
            (r == 1 && c == 6 && g_game.board[0][7] == EMPTY) ||
            (r == 6 && c == 1 && g_game.board[7][0] == EMPTY) ||
            (r == 6 && c == 6 && g_game.board[7][7] == EMPTY)) {
            score -= 50000;
        }

        if (score > bestScore) {
            bestScore = score;
            bestIdx = i;
        }
    }

    return moves[bestIdx];
}

// --- 以降、UIやデータセット処理などの既存ロジック（改変・削除なし） ---

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
    int res = MessageBox(hwndParent, "AIの初期レベルを最高（LV 10）にしますか？\n\n【はい】: レベル 10 (最強モード)\n【いいえ】: レベル 5 (バランスモード)\n【キャンセル】: レベル 1 (ランダム)", "AIレベルの初期設定", MB_YESNOCANCEL | MB_ICONQUESTION);
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
    const int simulations = 150; 

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

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP hMemBitmap = CreateCompatibleBitmap(hdc, winW, winH);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(memDC, hMemBitmap);

    HBRUSH hBgBrush = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
    FillRect(memDC, &clientRect, hBgBrush);
    DeleteObject(hBgBrush);

    RECT infoPanel = { BOARD_SIZE * CELL_SIZE + 12, 10, winW - 10, winH - 10 };
    HBRUSH hPanelBrush = CreateSolidBrush(RGB(250, 250, 250));
    FillRect(memDC, &infoPanel, hPanelBrush);
    HPEN hPanelPen = CreatePen(PS_SOLID, 1, RGB(200, 200, 200));
    HPEN hSavedPen = (HPEN)SelectObject(memDC, hPanelPen);
    Rectangle(memDC, infoPanel.left, infoPanel.top, infoPanel.right, infoPanel.bottom);
    SelectObject(memDC, hSavedPen);
    DeleteObject(hPanelPen);
    DeleteObject(hPanelBrush);

    HBRUSH hBoardBrush = CreateSolidBrush(RGB(34, 139, 34));
    HBRUSH hOldBrush = (HBRUSH)SelectObject(memDC, hBoardBrush);
    Rectangle(memDC, 0, 0, BOARD_SIZE * CELL_SIZE, BOARD_SIZE * CELL_SIZE);

    HPEN hBoardPen = CreatePen(PS_SOLID, 2, RGB(20, 80, 20));
    HPEN hBoardOldPen = (HPEN)SelectObject(memDC, hBoardPen);
    for (int i = 0; i <= BOARD_SIZE; i++) {
        MoveToEx(memDC, i * CELL_SIZE, 0, NULL); LineTo(memDC, i * CELL_SIZE, BOARD_SIZE * CELL_SIZE);
        MoveToEx(memDC, 0, i * CELL_SIZE, NULL); LineTo(memDC, BOARD_SIZE * CELL_SIZE, i * CELL_SIZE);
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

            int pieceWidth = (int)((CELL_SIZE - 8) * widthScale);
            int pieceHeight = (int)((CELL_SIZE - 8) * heightScale);
            int left = j * CELL_SIZE + 4 + ((CELL_SIZE - 8) - pieceWidth) / 2;
            int top = i * CELL_SIZE + 4 + ((CELL_SIZE - 8) - pieceHeight) / 2;
            int right = left + pieceWidth;
            int bottom = top + pieceHeight;
            Ellipse(memDC, left, top, right, bottom);
        }
    }

    if (!g_gameOver && ((g_mode == 0) || (g_mode == 1 && g_game.turn == BLACK))) {
        g_gameValidMoves = g_game.getValidMoves();
        HBRUSH hGuideBrush = CreateSolidBrush(RGB(100, 220, 100));
        SelectObject(memDC, hGuideBrush);
        for (size_t i = 0; i < g_gameValidMoves.size(); i++) {
            Ellipse(memDC, g_gameValidMoves[i].c * CELL_SIZE + 16, g_gameValidMoves[i].r * CELL_SIZE + 16, (g_gameValidMoves[i].c + 1) * CELL_SIZE - 16, (g_gameValidMoves[i].r + 1) * CELL_SIZE - 16);
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
                Ellipse(memDC, bestMove.c * CELL_SIZE + 10, bestMove.r * CELL_SIZE + 10, (bestMove.c + 1) * CELL_SIZE - 10, (bestMove.r + 1) * CELL_SIZE - 10);
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

    SetBkMode(memDC, OPAQUE);
    SetBkColor(memDC, RGB(250, 250, 250));
    char buf[256];

    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, RGB(245, 245, 245));
    for (int i = 0; i < BOARD_SIZE; i++) {
        char txt[2] = { (char)('A' + i), '\0' };
        TextOut(memDC, i * CELL_SIZE + 18, BOARD_SIZE * CELL_SIZE + 6, txt, 1);
    }
    for (int i = 0; i < BOARD_SIZE; i++) {
        char txt[2] = { (char)('1' + i), '\0' };
        TextOut(memDC, 6, i * CELL_SIZE + 16, txt, 1);
    }

    SetBkMode(memDC, OPAQUE);
    int infoX = BOARD_SIZE * CELL_SIZE + 28;
    int infoY = 26;
    int lineHeight = 32;

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

    RECT buttonRects[3];
    buttonRects[0] = BTN_RECT_PVP;
    buttonRects[1] = BTN_RECT_PVE;
    buttonRects[2] = BTN_RECT_EVE;
    const char* buttonLabels[3] = { "人対人", "人対AI", "AI対AI" };

    SetBkMode(memDC, TRANSPARENT);
    for (int i = 0; i < 3; i++) {
        HBRUSH hBtnBrush = CreateSolidBrush((g_mode == i ? RGB(60, 130, 220) : RGB(220, 220, 220)));
        HPEN hBtnBorder = CreatePen(PS_SOLID, 1, RGB(150, 150, 150));
        HPEN hOldBorder = (HPEN)SelectObject(memDC, hBtnBorder);
        HBRUSH hOldBtnBrush = (HBRUSH)SelectObject(memDC, hBtnBrush);
        RoundRect(memDC, buttonRects[i].left, buttonRects[i].top, buttonRects[i].right, buttonRects[i].bottom, 12, 12);
        SelectObject(memDC, hOldBtnBrush);
        SelectObject(memDC, hOldBorder);
        DeleteObject(hBtnBrush);
        DeleteObject(hBtnBorder);

        SetTextColor(memDC, (g_mode == i ? RGB(255, 255, 255) : RGB(0, 0, 0)));
        int textX = buttonRects[i].left + 8;
        int textY = buttonRects[i].top + 6;
        TextOut(memDC, textX, textY, buttonLabels[i], (int)strlen(buttonLabels[i]));
    }

    SetBkMode(memDC, OPAQUE);
    SetTextColor(memDC, RGB(120, 120, 120));
    
    sprintf(buf, "[1]-[0]: AI強度変更          ");
    TextOut(memDC, infoX, winH - 196, buf, (int)strlen(buf));
    sprintf(buf, "[R]: リセット (学習継続)     ");
    TextOut(memDC, infoX, winH - 168, buf, (int)strlen(buf));
    sprintf(buf, "[S]: サポート ON/OFF         ");
    TextOut(memDC, infoX, winH - 140, buf, (int)strlen(buf));
    sprintf(buf, "[T]: サポート対象切替        ");
    TextOut(memDC, infoX, winH - 112, buf, (int)strlen(buf));
    sprintf(buf, "左クリックで石を配置します  ");
    TextOut(memDC, infoX, winH - 84, buf, (int)strlen(buf));

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

            if (x >= BTN_RECT_PVP.left && x <= BTN_RECT_PVP.right && y >= BTN_RECT_PVP.top && y <= BTN_RECT_PVP.bottom) {
                g_mode = 0; InvalidateRect(hwnd, NULL, TRUE); break;
            }
            if (x >= BTN_RECT_PVE.left && x <= BTN_RECT_PVE.right && y >= BTN_RECT_PVE.top && y <= BTN_RECT_PVE.bottom) {
                g_mode = 1; InvalidateRect(hwnd, NULL, TRUE); break;
            }
            if (x >= BTN_RECT_EVE.left && x <= BTN_RECT_EVE.right && y >= BTN_RECT_EVE.top && y <= BTN_RECT_EVE.bottom) {
                g_mode = 2; InvalidateRect(hwnd, NULL, TRUE); break;
            }

            if (!g_gameOver && !g_isAnimating) {
                int c = x / CELL_SIZE; int r = y / CELL_SIZE;

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
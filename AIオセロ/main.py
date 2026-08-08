import sys
import json
import random

BOARD_SIZE = 8
EMPTY = 0
BLACK = 1
WHITE = -1

# 位置評価テーブル
STATIC_WEIGHT = [
    [ 120, -40,  20,   5,   5,  20, -40, 120],
    [-40, -70,  -5,  -5,  -5,  -5, -70, -40],
    [  20,  -5,  15,   3,   3,  15,  -5,  20],
    [   5,  -5,   3,   3,   3,   3,  -5,   5],
    [   5,  -5,   3,   3,   3,   3,  -5,   5],
    [  20,  -5,  15,   3,   3,  15,  -5,  20],
    [-40, -70,  -5,  -5,  -5,  -5, -70, -40],
    [ 120, -40,  20,   5,   5,  20, -40, 120]
]

DR = [-1, -1, -1,  0, 0,  1, 1, 1]
DC = [-1,  0,  1, -1, 1, -1, 0, 1]

def is_valid_move(board, r, c, player):
    if r < 0 or r >= BOARD_SIZE or c < 0 or c >= BOARD_SIZE:
        return False
    if board[r][c] != EMPTY:
        return False

    for i in range(8):
        nr, nc = r + DR[i], c + DC[i]
        count = 0
        while 0 <= nr < BOARD_SIZE and 0 <= nc < BOARD_SIZE and board[nr][nc] == -player:
            nr += DR[i]
            nc += DC[i]
            count += 1
        if count > 0 and 0 <= nr < BOARD_SIZE and 0 <= nc < BOARD_SIZE and board[nr][nc] == player:
            return True
    return False

def get_valid_moves(board, player):
    moves = []
    for r in range(BOARD_SIZE):
        for c in range(BOARD_SIZE):
            if is_valid_move(board, r, c, player):
                moves.append((r, c))
    return moves

def make_move(board, r, c, player):
    new_board = [row[:] for row in board]
    new_board[r][c] = player

    for i in range(8):
        nr, nc = r + DR[i], c + DC[i]
        flips = []
        while 0 <= nr < BOARD_SIZE and 0 <= nc < BOARD_SIZE and new_board[nr][nc] == -player:
            flips.append((nr, nc))
            nr += DR[i]
            nc += DC[i]
        if flips and 0 <= nr < BOARD_SIZE and 0 <= nc < BOARD_SIZE and new_board[nr][nc] == player:
            for fr, fc in flips:
                new_board[fr][fc] = player
    return new_board

def evaluate_board(board, ai_player):
    my_score = 0
    opp_score = 0
    my_stones = 0
    opp_stones = 0
    empty_count = 0

    for r in range(BOARD_SIZE):
        for c in range(BOARD_SIZE):
            val = board[r][c]
            if val == EMPTY:
                empty_count += 1
                continue

            w = STATIC_WEIGHT[r][c]
            # 角が取られている場合の危険マス(X置き)ペナルティ緩和
            if (r == 1 and c == 1 and board[0][0] != EMPTY) or \
               (r == 1 and c == 6 and board[0][7] != EMPTY) or \
               (r == 6 and c == 1 and board[7][0] != EMPTY) or \
               (r == 6 and c == 6 and board[7][7] != EMPTY):
                w += 60

            if val == ai_player:
                my_score += w
                my_stones += 1
            else:
                opp_score += w
                opp_stones += 1

    # 終盤戦（石数勝負）
    if empty_count <= 10:
        return (my_stones - opp_stones) * 10000

    # 着手可能数（Mobility）評価
    my_moves = len(get_valid_moves(board, ai_player))
    opp_moves = len(get_valid_moves(board, -ai_player))
    mobility_score = (my_moves - opp_moves) * 40

    # 序盤?中盤の石数多すぎ抑制
    stone_penalty = 0
    if empty_count > 30:
        stone_penalty = -(my_stones * 15)

    return (my_score - opp_score) + mobility_score + stone_penalty

def alpha_beta(board, depth, alpha, beta, player, ai_player):
    moves = get_valid_moves(board, player)

    if depth == 0 or (not moves and not get_valid_moves(board, -player)):
        return evaluate_board(board, ai_player)

    if not moves:
        return alpha_beta(board, depth - 1, alpha, beta, -player, ai_player)

    is_max = (player == ai_player)

    if is_max:
        max_eval = -9999999
        for r, c in moves:
            next_board = make_move(board, r, c, player)
            eval_val = alpha_beta(next_board, depth - 1, alpha, beta, -player, ai_player)
            max_eval = max(max_eval, eval_val)
            alpha = max(alpha, eval_val)
            if beta <= alpha:
                break
        return max_eval
    else:
        min_eval = 9999999
        for r, c in moves:
            next_board = make_move(board, r, c, player)
            eval_val = alpha_beta(next_board, depth - 1, alpha, beta, -player, ai_player)
            min_eval = min(min_eval, eval_val)
            beta = min(beta, eval_val)
            if beta <= alpha:
                break
        return min_eval

def select_best_move(board, player, level):
    moves = get_valid_moves(board, player)
    if not moves:
        return -1, -1

    # 低レベル時のランダム要素
    rate = (level - 1) * 11
    if level < 5 and random.randint(0, 99) >= rate:
        return random.choice(moves)

    # レベルに応じた深さ調整
    depth = 1
    if level == 5: depth = 3
    elif level == 6: depth = 4
    elif level == 7: depth = 5
    elif level == 8: depth = 6
    elif level == 9: depth = 7
    elif level >= 10: depth = 8

    empty_count = sum(row.count(EMPTY) for row in board)
    if level >= 10 and empty_count <= 12:
        depth = empty_count  # 終盤完全読み

    best_score = -99999999
    best_move = moves[0]

    for r, c in moves:
        next_board = make_move(board, r, c, player)
        score = alpha_beta(next_board, depth - 1, -99999999, 99999999, -player, player)

        # X置きのペナルティ調整
        if (r == 1 and c == 1 and board[0][0] == EMPTY) or \
           (r == 1 and c == 6 and board[0][7] == EMPTY) or \
           (r == 6 and c == 1 and board[7][0] == EMPTY) or \
           (r == 6 and c == 6 and board[7][7] == EMPTY):
            score -= 40000

        if score > best_score:
            best_score = score
            best_move = (r, c)

    return best_move

def main():
    while True:
        line = sys.stdin.readline()
        if not line:
            break
        try:
            data = json.loads(line)
            board = data["board"]
            player = data["turn"]
            level = data["level"]

            best_r, best_c = select_best_move(board, player, level)
            res = {"r": best_r, "c": best_c}
            print(json.dumps(res))
            sys.stdout.flush()
        except Exception as e:
            res = {"r": -1, "c": -1, "error": str(e)}
            print(json.dumps(res))
            sys.stdout.flush()

if __name__ == "__main__":
    main()
import torch
import random
import numpy as np

# 盤面サイズ
BOARD_SIZE = 8
dx = [0, 0, -1, 1, -1, 1, -1, 1]
dy = [-1, 1, 0, 0, -1, -1, 1, 1]

# オセロの簡易ゲーム環境クラス
class OthelloEnv:
    def __init__(self):
        self.reset()

    def reset(self):
        self.board = np.zeros((2, BOARD_SIZE, BOARD_SIZE), dtype=np.float32)
        # 初期配置 (1: 黒, 2: 白)
        self.board[1, 3, 3] = 1 # 黒
        self.board[0, 3, 4] = 1 # 白
        self.board[0, 4, 3] = 1 # 白
        self.board[1, 4, 4] = 1 # 黒
        # 内部表現: 0=空, 1=黒, 2=白
        self.grid = np.zeros((BOARD_SIZE, BOARD_SIZE), dtype=int)
        self.grid[3, 3] = 2; self.grid[3, 4] = 1
        self.grid[4, 3] = 1; self.grid[4, 4] = 2
        self.current_turn = 1 # 1: 黒, 2: 白

    def get_flips(self, x, y, player):
        if self.grid[y, x] != 0:
            return []
        opponent = 2 if player == 1 else 1
        flips = []
        for i in range(8):
            nx, ny = x + dx[i], y + dy[i]
            temp = []
            while 0 <= nx < BOARD_SIZE and 0 <= ny < BOARD_SIZE and self.grid[ny, nx] == opponent:
                temp.append((nx, ny))
                nx += dx[i]
                ny += dy[i]
            if 0 <= nx < BOARD_SIZE and 0 <= ny < BOARD_SIZE and self.grid[ny, nx] == player:
                flips.extend(temp)
        return flips

    def get_valid_moves(self, player):
        moves = []
        for y in range(BOARD_SIZE):
            for x in range(BOARD_SIZE):
                if len(self.get_flips(x, y, player)) > 0:
                    moves.append((x, y))
        return moves

    def step(self, move, player):
        x, y = move
        flips = self.get_flips(x, y, player)
        if not flips:
            return False
        self.grid[y, x] = player
        for fx, fy in flips:
            self.grid[fy, fx] = player
        self.current_turn = 2 if player == 1 else 1
        return True

    def is_game_over(self):
        return len(self.get_valid_moves(1)) == 0 and len(self.get_valid_moves(2)) == 0

    def get_score(self):
        black = np.sum(self.grid == 1)
        white = np.sum(self.grid == 2)
        return black, white

# --- AIのエージェント定義 ---

class RandomAgent:
    def select_move(self, env, player):
        moves = env.get_valid_moves(player)
        return random.choice(moves) if moves else None

class NeuralNetworkAgent:
    def __init__(self, model_path="othello_model.onnx"):
        # 簡単のため PyTorchモデルを直接読み込むか、ランダムな重みで代用
        from learn import OthelloNet
        self.model = OthelloNet()
        self.model.eval()

    def select_move(self, env, player):
        moves = env.get_valid_moves(player)
        if not moves:
            return None
        
        # 盤面をモデルの入力形式 (1, 2, 8, 8) に変換
        # チャンネル0: 自分の石, チャンネル1: 相手の石
        board_tensor = np.zeros((1, 2, BOARD_SIZE, BOARD_SIZE), dtype=np.float32)
        my_p = player
        op_p = 2 if player == 1 else 1
        board_tensor[0, 0] = (env.grid == my_p).astype(np.float32)
        board_tensor[0, 1] = (env.grid == op_p).astype(np.float32)
        
        with torch.no_grad():
            policy, value = self.model(torch.tensor(board_tensor))
            policy = policy[0].numpy()
            
        # 置ける場所の中でモデルの出力スコアが最も高いものを選ぶ
        best_move = moves[0]
        max_score = -float('inf')
        for x, y in moves:
            idx = y * BOARD_SIZE + x
            score = policy[idx]
            if score > max_score:
                max_score = score
                best_move = (x, y)
        return best_move

# --- 対局シミュレーション実行関数 ---
def run_tournament(num_games=50):
    env = OthelloEnv()
    ai_agent = NeuralNetworkAgent()
    random_agent = RandomAgent()

    ai_wins = 0
    random_wins = 0
    draws = 0
    disc_differences = []

    print(f"--- AI vs ランダムAI の対戦テスト開始 ({num_games}局) ---")

    for game in range(num_games):
        env.reset()
        # 交互に先手・後手を入れ替え
        ai_is_black = (game % 2 == 0)

        while not env.is_game_over():
            p = env.current_turn
            is_ai_turn = (p == 1 and ai_is_black) or (p == 2 and not ai_is_black)

            agent = ai_agent if is_ai_turn else random_agent
            move = agent.select_move(env, p)

            if move is not None:
                env.step(move, p)
            else:
                # パスの場合のターン切り替え
                env.current_turn = 2 if p == 1 else 1

            # 両者置けなくなったら終了
            if len(env.get_valid_moves(1)) == 0 and len(env.get_valid_moves(2)) == 0:
                break

        black_score, white_score = env.get_score()
        ai_score = black_score if ai_is_black else white_score
        rand_score = white_score if ai_is_black else black_score
        diff = ai_score - rand_score
        disc_differences.append(diff)

        if ai_score > rand_score:
            ai_wins += 1
        elif ai_score < rand_score:
            random_wins += 1
        else:
            draws += 1

    # --- 結果の集計と出力 ---
    win_rate = (ai_wins / num_games) * 100
    avg_diff = np.mean(disc_differences)

    print("\n===============================")
    print("?? AI進化・客観評価レポート")
    print("===============================")
    print(f"総対局数: {num_games} 局")
    print(f"AIの勝利数: {ai_wins} 勝")
    print(f"ランダムAIの勝利数: {random_wins} 敗")
    print(f"引き分け: {draws} 回")
    print(f"?? AI勝率: {win_rate:.1f}%")
    print(f"?? 平均石差 (AI視点): {avg_diff:+.1f} 枚")
    print("===============================")

if __name__ == "__main__":
    run_tournament(50)
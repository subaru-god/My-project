import argparse
import os
import random
import time
import numpy as np
import torch
import torch.nn as nn
import torch.optim as optim
from torch.utils.data import TensorDataset, DataLoader
from tqdm import tqdm

WEIGHTS_FILENAME = "othello_weights.pth"
ONNX_FILENAME = "othello_model.onnx"
BEST_WEIGHTS_FILENAME = "othello_best_weights.pth"

BOARD_SIZE = 8
DX = [0, 0, -1, 1, -1, 1, -1, 1]
DY = [-1, 1, 0, 0, -1, -1, 1, 1]

POSITION_WEIGHTS = np.array([
    [ 120, -20,  20,   5,   5,  20, -20, 120],
    [ -20, -40,  -5,  -5,  -5,  -5, -40, -20],
    [  20,  -5,  15,   3,   3,  15,  -5,  20],
    [   5,  -5,   3,   3,   3,   3,  -5,   5],
    [   5,  -5,   3,   3,   3,   3,  -5,   5],
    [  20,  -5,  15,   3,   3,  15,  -5,  20],
    [ -20, -40,  -5,  -5,  -5,  -5, -40, -20],
    [ 120, -20,  20,   5,   5,  20, -20, 120]
], dtype=np.float32)


class OthelloNet(nn.Module):
    def __init__(self):
        super(OthelloNet, self).__init__()
        self.conv1 = nn.Conv2d(2, 128, kernel_size=3, padding=1)
        self.conv2 = nn.Conv2d(128, 256, kernel_size=3, padding=1)
        self.conv3 = nn.Conv2d(256, 256, kernel_size=3, padding=1)

        self.value_fc1 = nn.Linear(256 * 8 * 8, 128)
        self.value_fc2 = nn.Linear(128, 32)
        self.value_fc3 = nn.Linear(32, 1)

        self.policy_fc1 = nn.Linear(256 * 8 * 8, 256)
        self.policy_fc2 = nn.Linear(256, 64)

    def forward(self, x):
        x = torch.relu(self.conv1(x))
        x = torch.relu(self.conv2(x))
        x = torch.relu(self.conv3(x))

        x = x.view(-1, 256 * 8 * 8)

        v = torch.relu(self.value_fc1(x))
        v = torch.relu(self.value_fc2(v))
        value = torch.tanh(self.value_fc3(v))

        p = torch.relu(self.policy_fc1(x))
        policy = self.policy_fc2(p)

        return policy, value


class FastOthelloEnv:
    def __init__(self):
        self.reset()

    def reset(self):
        self.grid = np.zeros((BOARD_SIZE, BOARD_SIZE), dtype=np.int8)
        self.grid[3, 3] = 2
        self.grid[3, 4] = 1
        self.grid[4, 3] = 1
        self.grid[4, 4] = 2
        self.current_turn = 1

    def clone(self):
        env = FastOthelloEnv.__new__(FastOthelloEnv)
        env.grid = self.grid.copy()
        env.current_turn = self.current_turn
        return env

    def get_flips(self, x, y, player):
        if self.grid[y, x] != 0:
            return []
        opponent = 3 - player
        flips = []
        grid = self.grid
        for i in range(8):
            dx_i = DX[i]
            dy_i = DY[i]
            nx = x + dx_i
            ny = y + dy_i
            temp = []
            while 0 <= nx < BOARD_SIZE and 0 <= ny < BOARD_SIZE and grid[ny, nx] == opponent:
                temp.append((nx, ny))
                nx += dx_i
                ny += dy_i
            if temp and 0 <= nx < BOARD_SIZE and 0 <= ny < BOARD_SIZE and grid[ny, nx] == player:
                flips.extend(temp)
        return flips

    def get_valid_moves(self, player):
        moves = []
        for y in range(BOARD_SIZE):
            for x in range(BOARD_SIZE):
                if self.grid[y, x] == 0 and self.get_flips(x, y, player):
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
        self.current_turn = 3 - player
        return True

    def get_state_tensor(self, player):
        opponent = 3 - player
        state = np.zeros((2, BOARD_SIZE, BOARD_SIZE), dtype=np.float32)
        state[0] = (self.grid == player)
        state[1] = (self.grid == opponent)
        return state

    def get_score(self):
        black = int(np.sum(self.grid == 1))
        white = int(np.sum(self.grid == 2))
        return black, white


def evaluate_heuristic(env, player):
    opponent = 3 - player
    my_mask = (env.grid == player)
    op_mask = (env.grid == opponent)
    pos_score = np.sum(POSITION_WEIGHTS[my_mask]) - np.sum(POSITION_WEIGHTS[op_mask])
    my_moves = len(env.get_valid_moves(player))
    op_moves = len(env.get_valid_moves(opponent))
    mobility_score = (my_moves - op_moves) * 8.0
    return pos_score + mobility_score


def select_move_with_lookahead(model, device, env, player, epsilon=0.15, use_2ply=True):
    valid_moves = env.get_valid_moves(player)
    if not valid_moves:
        return None

    if random.random() < epsilon:
        return random.choice(valid_moves)

    state = env.get_state_tensor(player)
    state_t = torch.from_numpy(state).unsqueeze(0).to(device)

    with torch.no_grad():
        policy_logits, _ = model(state_t)
        policy_probs = torch.softmax(policy_logits[0], dim=0).cpu().numpy()

    if not use_2ply:
        best_move = valid_moves[0]
        best_score = -1e9
        for x, y in valid_moves:
            s = policy_probs[y * BOARD_SIZE + x]
            if s > best_score:
                best_score = s
                best_move = (x, y)
        return best_move

    scored_candidates = []
    for x, y in valid_moves:
        p_score = policy_probs[y * BOARD_SIZE + x]
        pos_bonus = POSITION_WEIGHTS[y, x] / 120.0
        scored_candidates.append(((x, y), p_score + 0.25 * pos_bonus))

    scored_candidates.sort(key=lambda item: item[1], reverse=True)
    top_candidates = [mv for mv, _ in scored_candidates[:min(len(scored_candidates), 6)]]

    next_states = []
    next_envs = []
    for mv in top_candidates:
        sim_env = env.clone()
        sim_env.step(mv, player)
        next_envs.append(sim_env)
        next_states.append(sim_env.get_state_tensor(3 - player))

    batch_next = torch.from_numpy(np.array(next_states, dtype=np.float32)).to(device)
    with torch.no_grad():
        _, next_values = model(batch_next)
        next_values = next_values.squeeze(1).cpu().numpy()

    best_move = top_candidates[0]
    best_total = -1e9
    for idx, mv in enumerate(top_candidates):
        nn_val = -float(next_values[idx])
        heur_val = np.tanh(evaluate_heuristic(next_envs[idx], player) / 200.0)
        p_prior = float(policy_probs[mv[1] * BOARD_SIZE + mv[0]])
        total_score = 0.65 * nn_val + 0.25 * heur_val + 0.10 * p_prior
        if total_score > best_total:
            best_total = total_score
            best_move = mv

    return best_move


def augment_sample(state, action_idx, value):
    ay, ax = divmod(action_idx, BOARD_SIZE)
    action_board = np.zeros((BOARD_SIZE, BOARD_SIZE), dtype=np.float32)
    action_board[ay, ax] = 1.0

    augmented = []
    for k in range(4):
        s_rot = np.rot90(state, k, axes=(1, 2))
        a_rot = np.rot90(action_board, k)
        for flip in (False, True):
            if flip:
                s_aug = np.flip(s_rot, axis=2).copy()
                a_aug = np.fliplr(a_rot).copy()
            else:
                s_aug = s_rot.copy()
                a_aug = a_rot.copy()
            new_action_idx = int(np.argmax(a_aug))
            augmented.append((s_aug, new_action_idx, value))
    return augmented


def generate_self_play_games(model, device, num_games, epsilon, use_2ply=True):
    model.eval()
    states_list = []
    actions_list = []
    values_list = []

    black_wins = 0
    white_wins = 0
    draws = 0

    for _ in tqdm(range(num_games), desc="Self-Play Games", leave=False):
        env = FastOthelloEnv()
        history = []
        pass_count = 0

        while pass_count < 2:
            player = env.current_turn
            valid_moves = env.get_valid_moves(player)
            if not valid_moves:
                pass_count += 1
                env.current_turn = 3 - player
                continue

            pass_count = 0
            step_eps = max(epsilon, 0.30) if len(history) < 6 else epsilon
            move = select_move_with_lookahead(model, device, env, player, epsilon=step_eps, use_2ply=use_2ply)
            state = env.get_state_tensor(player)
            action_idx = move[1] * BOARD_SIZE + move[0]
            history.append((state, player, action_idx))
            env.step(move, player)

        black_score, white_score = env.get_score()
        if black_score > white_score:
            winner = 1
            black_wins += 1
        elif white_score > black_score:
            winner = 2
            white_wins += 1
        else:
            winner = 0
            draws += 1

        margin_bonus = (black_score - white_score) / 64.0 * 0.25

        for state, player, action_idx in history:
            if winner == 0:
                target_val = 0.0
            elif winner == player:
                target_val = min(1.0, 0.75 + abs(margin_bonus))
            else:
                target_val = max(-1.0, -0.75 - abs(margin_bonus))

            for aug_s, aug_a, aug_v in augment_sample(state, action_idx, target_val):
                states_list.append(aug_s)
                actions_list.append(aug_a)
                values_list.append(aug_v)

    stats = {
        "black_wins": black_wins,
        "white_wins": white_wins,
        "draws": draws,
        "samples": len(states_list),
    }
    return (
        np.array(states_list, dtype=np.float32),
        np.array(actions_list, dtype=np.int64),
        np.array(values_list, dtype=np.float32),
        stats,
    )


def evaluate_against_random(model, device, num_games=40):
    model.eval()
    wins = 0
    losses = 0
    draws = 0
    total_diff = 0

    for g in tqdm(range(num_games), desc="Evaluation vs Random", leave=False):
        env = FastOthelloEnv()
        ai_player = 1 if (g % 2 == 0) else 2
        pass_count = 0

        while pass_count < 2:
            p = env.current_turn
            valid = env.get_valid_moves(p)
            if not valid:
                pass_count += 1
                env.current_turn = 3 - p
                continue
            pass_count = 0

            if p == ai_player:
                move = select_move_with_lookahead(model, device, env, p, epsilon=0.0, use_2ply=False)
            else:
                move = random.choice(valid)
            env.step(move, p)

        b, w = env.get_score()
        ai_s = b if ai_player == 1 else w
        op_s = w if ai_player == 1 else b
        total_diff += (ai_s - op_s)
        if ai_s > op_s:
            wins += 1
        elif ai_s < op_s:
            losses += 1
        else:
            draws += 1

    win_rate = (wins / num_games) * 100.0
    avg_diff = total_diff / num_games
    return win_rate, avg_diff, wins, losses, draws


def export_onnx(model, onnx_path):
    model_cpu = OthelloNet()
    model_cpu.load_state_dict(model.state_dict())
    model_cpu.eval()

    dummy_input = torch.randn(1, 2, 8, 8)
    torch.onnx.export(
        model_cpu,
        dummy_input,
        onnx_path,
        input_names=["board_input"],
        output_names=["policy_output", "value_output"],
        dynamic_axes={
            "board_input": {0: "batch_size"},
            "policy_output": {0: "batch_size"},
            "value_output": {0: "batch_size"},
        },
        opset_version=14,
    )


def main():
    parser = argparse.ArgumentParser(description="Automated Large-Scale Othello Self-Play Reinforcement Trainer")
    parser.add_argument("--cycles", type=int, default=20, help="自動強化学習サイクル数 (デフォルト: 20)")
    parser.add_argument("--games", type=int, default=100, help="1サイクルあたりの自己対局数 (デフォルト: 100)")
    parser.add_argument("--epochs", type=int, default=5, help="1サイクルあたりの学習エポック数 (デフォルト: 5)")
    parser.add_argument("--batch-size", type=int, default=256, help="ミニバッチサイズ (デフォルト: 256)")
    parser.add_argument("--lr", type=float, default=0.0008, help="学習率 (デフォルト: 0.0008)")
    parser.add_argument("--buffer-size", type=int, default=200000, help="経験リプレイバッファ最大サンプル数 (デフォルト: 200000)")
    parser.add_argument("--eval-games", type=int, default=40, help="各サイクル後の評価対局数 (デフォルト: 40)")
    parser.add_argument("--no-lookahead", action="store_true", help="自己対局時の1手先読み探索を無効化して高速化")
    args = parser.parse_args()

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print(f"使用デバイス: {device}")

    model = OthelloNet().to(device)
    if os.path.exists(WEIGHTS_FILENAME):
        try:
            model.load_state_dict(torch.load(WEIGHTS_FILENAME, map_location=device))
            print(f"既存の学習済み重み ({WEIGHTS_FILENAME}) をロードしました。")
        except RuntimeError:
            print("ネットワーク構造不一致のため、新規重みから学習を開始します。")
    else:
        print("新規モデルから自動強化学習を開始します。")

    optimizer = optim.AdamW(model.parameters(), lr=args.lr, weight_decay=1e-4)
    scheduler = optim.lr_scheduler.CosineAnnealingLR(optimizer, T_max=max(1, args.cycles), eta_min=1e-5)
    criterion_policy = nn.CrossEntropyLoss()
    criterion_value = nn.MSELoss()

    replay_states = None
    replay_actions = None
    replay_values = None

    best_win_rate = -1.0
    best_avg_diff = -999.0
    total_games_played = 0
    start_time = time.time()

    # 外側のサイクルループにプログレスバーを導入
    for cycle in tqdm(range(1, args.cycles + 1), desc="Training Cycles"):
        progress = (cycle - 1) / max(1, args.cycles - 1)
        epsilon = max(0.05, 0.30 * (1.0 - 0.8 * progress))

        tqdm.write(f"\n=== サイクル {cycle}/{args.cycles} (自己対局: {args.games}局 / 探索率ε: {epsilon:.2f}) ===")
        new_states, new_actions, new_values, stats = generate_self_play_games(
            model,
            device,
            num_games=args.games,
            epsilon=epsilon,
            use_2ply=not args.no_lookahead,
        )
        total_games_played += args.games

        if replay_states is None:
            replay_states = new_states
            replay_actions = new_actions
            replay_values = new_values
        else:
            replay_states = np.concatenate([replay_states, new_states], axis=0)
            replay_actions = np.concatenate([replay_actions, new_actions], axis=0)
            replay_values = np.concatenate([replay_values, new_values], axis=0)
            if len(replay_states) > args.buffer_size:
                replay_states = replay_states[-args.buffer_size:]
                replay_actions = replay_actions[-args.buffer_size:]
                replay_values = replay_values[-args.buffer_size:]

        dataset = TensorDataset(
            torch.from_numpy(replay_states),
            torch.from_numpy(replay_actions),
            torch.from_numpy(replay_values).unsqueeze(1),
        )
        loader = DataLoader(dataset, batch_size=args.batch_size, shuffle=True)

        model.train()
        total_loss_sum = 0.0
        batch_count = 0

        # 学習のエポックおよびバッチの進捗を表示するプログレスバーを導入
        for epoch in range(args.epochs):
            epoch_loader = tqdm(loader, desc=f"Epoch {epoch+1}/{args.epochs}", leave=False)
            for bx, by_p, by_v in epoch_loader:
                bx = bx.to(device)
                by_p = by_p.to(device)
                by_v = by_v.to(device)

                optimizer.zero_grad()
                pred_p, pred_v = model(bx)
                loss_p = criterion_policy(pred_p, by_p)
                loss_v = criterion_value(pred_v, by_v)
                loss = loss_p + loss_v
                loss.backward()
                torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
                optimizer.step()

                total_loss_sum += loss.item()
                batch_count += 1
                
                # バッチごとのLossをバーに表示
                epoch_loader.set_postfix(loss=f"{loss.item():.4f}")

        scheduler.step()
        avg_loss = total_loss_sum / max(1, batch_count)

        torch.save(model.state_dict(), WEIGHTS_FILENAME)
        export_onnx(model, ONNX_FILENAME)

        win_rate, avg_diff, wins, losses, draws = evaluate_against_random(
            model, device, num_games=args.eval_games
        )

        is_best = (win_path := (win_rate > best_win_rate)) or (win_rate == best_win_rate and avg_diff > best_avg_diff)
        if is_best:
            best_win_rate = win_rate
            best_avg_diff = avg_diff
            torch.save(model.state_dict(), BEST_WEIGHTS_FILENAME)

        elapsed = time.time() - start_time
        tqdm.write(
            f"対局内訳(黒/白/分): {stats['black_wins']}/{stats['white_wins']}/{stats['draws']} | "
            f"バッファ: {len(replay_states)}件 | Loss: {avg_loss:.4f}"
        )
        tqdm.write(
            f"評価戦({args.eval_games}局): 勝率 {win_rate:.1f}% ({wins}勝{losses}敗{draws}分) | "
            f"平均石差: {avg_diff:+.1f} | 累計対局: {total_games_played}局 | 経過: {elapsed:.0f}秒"
            + (" [ベスト更新]" if is_best else "")
        )

    print("\n==========================================")
    print(f"自動大量強化学習が完了しました (累計 {total_games_played} 局)")
    print(f"最高勝率: {best_win_rate:.1f}% (平均石差: {best_avg_diff:+.1f})")
    print(f"最新モデル: {ONNX_FILENAME} / {WEIGHTS_FILENAME}")
    print(f"ベスト重み: {BEST_WEIGHTS_FILENAME}")
    print("==========================================")


if __name__ == "__main__":
    main()
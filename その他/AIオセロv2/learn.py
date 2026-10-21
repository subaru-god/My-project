import argparse
import os
import torch
import torch.nn as nn
import torch.optim as optim
from tqdm import tqdm

WEIGHTS_FILENAME = "othello_weights.pth"
ONNX_FILENAME = "othello_model.onnx"

# 1. パラメータを増やして拡張したオセロ用ニューラルネットワーク
class OthelloNet(nn.Module):
    def __init__(self):
        super(OthelloNet, self).__init__()
        # 畳み込み層を3層に増やし、チャネル数を拡大 (表現力を強化)
        self.conv1 = nn.Conv2d(2, 128, kernel_size=3, padding=1)
        self.conv2 = nn.Conv2d(128, 256, kernel_size=3, padding=1)
        self.conv3 = nn.Conv2d(256, 256, kernel_size=3, padding=1)
        
        # 評価値（Value Head）の層を深くする
        self.value_fc1 = nn.Linear(256 * 8 * 8, 128)
        self.value_fc2 = nn.Linear(128, 32)
        self.value_fc3 = nn.Linear(32, 1)
        
        # 手の確率（Policy Head）の層を深くする
        self.policy_fc1 = nn.Linear(256 * 8 * 8, 256)
        self.policy_fc2 = nn.Linear(256, 64)

    def forward(self, x):
        x = torch.relu(self.conv1(x))
        x = torch.relu(self.conv2(x))
        x = torch.relu(self.conv3(x))
        
        x = x.view(-1, 256 * 8 * 8)
        
        # Value（勝率: -1.0 ~ 1.0）
        v = torch.relu(self.value_fc1(x))
        v = torch.relu(self.value_fc2(v))
        value = torch.tanh(self.value_fc3(v))
        
        # Policy（各マスの指しやすさ）
        p = torch.relu(self.policy_fc1(x))
        policy = self.policy_fc2(p)
        
        return policy, value

# 2. 学習メイン処理
def main():
    parser = argparse.ArgumentParser(description="Othello AI Training Script")
    parser.add_argument("--epochs", type=int, default=10, help="学習のエポック数 (デフォルト: 10)")
    args = parser.parse_args()

    model = OthelloNet()

    # 構造が変わるため、初回は重みをリセットして新構造でスタートします
    if os.path.exists(WEIGHTS_FILENAME):
        try:
            model.load_state_dict(torch.load(WEIGHTS_FILENAME))
            print(f"?? 前回の学習済み重みをロードしました（※ネットワーク構造が拡張されました）。")
        except RuntimeError:
            print("?? ネットワークの構造変更に伴い、重みのサイズが一致しないため新規作成からスタートします。")
    else:
        print("?? 新しい拡張モデルの初期学習を開始します...")

    model.train()
    
    criterion_policy = nn.CrossEntropyLoss()
    criterion_value = nn.MSELoss()
    optimizer = optim.Adam(model.parameters(), lr=0.001)
    
    dummy_inputs = torch.randn(32, 2, 8, 8)
    dummy_policies = torch.randint(0, 64, (32,))
    dummy_values = torch.randn(32, 1).clamp(-1.0, 1.0)
    
    for epoch in tqdm(range(args.epochs), desc="Training Progress (Deep Model)"):
        optimizer.zero_grad()
        
        pred_policies, pred_values = model(dummy_inputs)
        
        loss_p = criterion_policy(pred_policies, dummy_policies)
        loss_v = criterion_value(pred_values, dummy_values)
        total_loss = loss_p + loss_v
        
        total_loss.backward()
        optimizer.step()

    # 保存処理
    torch.save(model.state_dict(), WEIGHTS_FILENAME)
    
    model.eval()
    dummy_input = torch.randn(1, 2, 8, 8)
    torch.onnx.export(
        model,
        dummy_input,
        ONNX_FILENAME,
        input_names=['board_input'],
        output_names=['policy_output', 'value_output'],
        dynamic_axes={
            'board_input': {0: 'batch_size'},
            'policy_output': {0: 'batch_size'},
            'value_output': {0: 'batch_size'}
        }
    )
    
    print(f"\n? パラメータ拡張版の学習が完了しました！")
    print(f"  - 新しいC++用モデル: {ONNX_FILENAME} を出力しました。")

if __name__ == "__main__":
    main()
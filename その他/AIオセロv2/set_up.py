# -*- coding: shift_jis -*-
import torch
import torch.nn as nn
import torch.nn.functional as F

# 1. オセロ用ニューラルネットワークの定義
class OthelloNet(nn.Module):
    def __init__(self):
        super(OthelloNet, self).__init__()
        # プレーンな盤面情報（自分の石、相手の石の2チャンネルを想定）
        # 入力形状: (バッチサイズ, 2, 8, 8)
        self.conv1 = nn.Conv2d(2, 64, kernel_size=3, padding=1)
        self.conv2 = nn.Conv2d(64, 128, kernel_size=3, padding=1)
        
        # 評価値（勝率）を出力するヘッド (Value Head)
        self.value_fc1 = nn.Linear(128 * 8 * 8, 64)
        self.value_fc2 = nn.Linear(64, 1)
        
        # 手の確率を出力するヘッド (Policy Head) - 8x8 = 64マス
        self.policy_fc = nn.Linear(128 * 8 * 8, 64)

    def forward(self, x):
        # 畳み込み層 ＋ ReLU活性化関数
        x = F.relu(self.conv1(x))
        x = F.relu(self.conv2(x))
        
        # フラット化
        x = x.view(-1, 128 * 8 * 8)
        
        # Value（勝率: -1.0 ~ 1.0）
        v = F.relu(self.value_fc1(x))
        value = torch.tanh(self.value_fc2(v))
        
        # Policy（各マスの指しやすさのスコア）
        policy = self.policy_fc(x)
        
        return policy, value

# 2. 動作確認とONNX出力のテスト
if __name__ == "__main__":
    # モデルのインスタンス化
    model = OthelloNet()
    model.eval() # 推論モードに設定

    # ダミーの入力データを作成 (バッチサイズ1, チャンネル2, 8x8盤面)
    # チャンネル0: 自分の石の位置, チャンネル1: 相手の石の位置
    dummy_input = torch.randn(1, 2, 8, 8)

    # 推論のテスト
    policy, value = model(dummy_input)
    print("Policy output shape:", policy.shape) # 期待値: torch.Size([1, 64])
    print("Value output shape: ", value.shape)  # 期待値: torch.Size([1, 1])

    # 3. C++（ONNX Runtime）で読み込めるようにONNX形式でエクスポート
    onnx_filename = "othello_model.onnx"
    torch.onnx.export(
        model,
        dummy_input,
        onnx_filename,
        input_names=['board_input'],
        output_names=['policy_output', 'value_output'],
        dynamic_axes={
            'board_input': {0: 'batch_size'},
            'policy_output': {0: 'batch_size'},
            'value_output': {0: 'batch_size'}
        }
    )
    print(f"Model successfully exported to {onnx_filename}")
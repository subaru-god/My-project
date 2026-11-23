#include <iostream>
#include <vector>
#include <thread>
#include <cmath>

// 1スレッドあたりがひたすら計算を行う関数
void heavy_work() {
    double val = 0.0;
        while (true) {
                // 負荷をかけるための重い数学的計算
                        val += std::sin(val) + std::cos(val);
                            }
                            }

                            int main() {
                                // 実行環境の最適なハードウェアスレッド数を取得
                                    unsigned int num_threads = std::thread::hardware_concurrency();
                                        if (num_threads == 0) num_threads = 2;

                                            std::cout << "Starting " << num_threads << " threads to load CPU..." << std::endl;

                                                // スレッドを格納するベクター
                                                    std::vector<std::thread> threads;
                                                        for (unsigned int i = 0; i < num_threads; ++i) {
                                                                threads.emplace_back(heavy_work);
                                                                    }

                                                                        // 全スレッドの終了を待機（無限ループのため実際はここで止まり続けます）
                                                                            for (auto& t : threads) {
                                                                                    t.join();
                                                                                        }

                                                                                            return 0;
                                                                                            }
                                                                                            
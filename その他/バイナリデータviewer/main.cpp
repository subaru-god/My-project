#include <windows.h>
#include <commdlg.h>
#include <stdio.h>

#define ID_EDIT_PATH    101
#define ID_BTN_BROWSE   102
#define ID_BTN_READ     103
#define ID_EDIT_OUTPUT  104

HWND hEditPath, hEditOutput;

// バイナリファイルを可読形式（16進数 ＋ ASCII）に変換する関数
void LoadAndConvertBinary(const char *filePath) {
    FILE *fp = fopen(filePath, "rb");
    if (!fp) {
        MessageBox(NULL, "ファイルを開けませんでした。", "エラー", MB_OK | MB_ICONERROR);
        return;
    }

    // ファイルサイズ取得
    fseek(fp, 0, SEEK_END);
    long fileSize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    // 読み込み上限（メモリ領域とパフォーマンス考慮：最大64KBまで表示）
    long maxBytes = (fileSize > 65536) ? 65536 : fileSize;
    unsigned char *buffer = (unsigned char *)malloc(maxBytes);
    if (!buffer) {
        fclose(fp);
        MessageBox(NULL, "メモリ確保に失敗しました。", "エラー", MB_OK | MB_ICONERROR);
        return;
    }

    fread(buffer, 1, maxBytes, fp);
    fclose(fp);

    // 出力テキストの生成 (1バイトあたり約4?5文字分のテキストが必要)
    size_t outSize = maxBytes * 5 + 1000;
    char *outputText = (char *)malloc(outSize);
    if (!outputText) {
        free(buffer);
        MessageBox(NULL, "メモリ確保に失敗しました。", "エラー", MB_OK | MB_ICONERROR);
        return;
    }

    outputText[0] = '\0';
    char line[128];
    
    // 16バイトずつ整形して出力用文字列に構築
    for (long i = 0; i < maxBytes; i += 16) {
        // アドレス表示
        sprintf(line, "%08X:  ", (unsigned int)i);
        strcat(outputText, line);

        // 16進数表示
        for (int j = 0; j < 16; j++) {
            if (i + j < maxBytes) {
                sprintf(line, "%02X ", buffer[i + j]);
            } else {
                sprintf(line, "   ");
            }
            strcat(outputText, line);
        }

        strcat(outputText, " |");

        // ASCII可読文字表示
        for (int j = 0; j < 16; j++) {
            if (i + j < maxBytes) {
                unsigned char c = buffer[i + j];
                // 表示可能文字（0x20?0x7E）以外は '.' に置換
                sprintf(line, "%c", (c >= 0x20 && c <= 0x7E) ? c : '.');
            } else {
                sprintf(line, " ");
            }
            strcat(outputText, line);
        }
        strcat(outputText, "|\r\n");
    }

    if (fileSize > maxBytes) {
        strcat(outputText, "\r\n... [ファイルが大きいため最初の 64KB のみ表示しています] ...");
    }

    SetWindowText(hEditOutput, outputText);

    free(buffer);
    free(outputText);
}

// エクスプローラー（ファイル選択ダイアログ）を開く関数
void SelectFileViaExplorer(HWND hwnd) {
    OPENFILENAME ofn;
    char szFile[MAX_PATH] = { 0 };

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "すべてのファイル (*.*)\0*.*\0バイナリファイル (*.bin;*.dat)\0*.bin;*.dat\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileName(&ofn)) {
        SetWindowText(hEditPath, szFile);
        LoadAndConvertBinary(szFile);
    }
}

// ウィンドウプロシージャ（イベント処理）
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            // パス入力欄（直接指定用）
            CreateWindow("STATIC", "ファイルパス:", WS_VISIBLE | WS_CHILD,
                       10, 15, 80, 20, hwnd, NULL, NULL, NULL);

            hEditPath = CreateWindow("EDIT", "", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
                                     90, 12, 350, 23, hwnd, (HMENU)ID_EDIT_PATH, NULL, NULL);

            // エクスプローラー呼び出しボタン
            CreateWindow("BUTTON", "参照...", WS_VISIBLE | WS_CHILD,
                         450, 11, 75, 25, hwnd, (HMENU)ID_BTN_BROWSE, NULL, NULL);

            // 読み込み実行ボタン
            CreateWindow("BUTTON", "直接指定して読み込み", WS_VISIBLE | WS_CHILD,
                         535, 11, 150, 25, hwnd, (HMENU)ID_BTN_READ, NULL, NULL);

            // 結果表示用エディットボックス（等幅フォント設定用）
            hEditOutput = CreateWindow("EDIT", "", 
                                       WS_VISIBLE | WS_CHILD | WS_BORDER | WS_VSCROLL | WS_HSCROLL | 
                                       ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                                       10, 45, 675, 400, hwnd, (HMENU)ID_EDIT_OUTPUT, NULL, NULL);

            // 文字列を見やすく整列させるため等幅フォント（Courier New）をセット
            HFONT hFont = CreateFont(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                     ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, "Courier New");
            SendMessage(hEditOutput, WM_SETFONT, (WPARAM)hFont, TRUE);
            break;
        }

        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            if (wmId == ID_BTN_BROWSE) {
                // エクスプローラーで選択
                SelectFileViaExplorer(hwnd);
            } else if (wmId == ID_BTN_READ) {
                // パス入力欄から直接読み込み
                char szPath[MAX_PATH];
                GetWindowText(hEditPath, szPath, MAX_PATH);
                if (strlen(szPath) > 0) {
                    LoadAndConvertBinary(szPath);
                } else {
                    MessageBox(hwnd, "ファイルパスを入力してください。", "通知", MB_OK | MB_ICONINFORMATION);
                }
            }
            break;
        }

        case WM_DESTROY:
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// エントリポイント WinMain
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    const char CLASS_NAME[] = "BinaryViewerWindow";

    WNDCLASS wc = { 0 };
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);

    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(
        0,
        CLASS_NAME,
        "バイナリファイル可読化ツール",
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME, // サイズ固定
        CW_USEDEFAULT, CW_USEDEFAULT, 710, 495,
        NULL, NULL, hInstance, NULL
    );

    if (hwnd == NULL) {
        return 0;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    // メッセージループ
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}
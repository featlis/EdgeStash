# DropLocker システム基本設計書 (C言語 / Win32 API版)
**バージョン:** 2.0.0 (Pure C Architecture)  
**対象プラットフォーム:** Windows 10 / 11 (x64)  
**開発言語:** C言語 (C99/C11, MSVC `cl.exe`)  
**分類:** デスクトップ常駐型ネイティブウィジェット・ユーティリティ  

---

## 1. システム概要 & 目的

### 1.1 概要
Tauri/Webviewを全廃し、**純C言語 (Win32 API + GDI+)** で構築する超軽量・超低遅延の画面端スライドイン型「24時間自動消滅スクラップドック」。

### 1.2 C言語化による利点
- **メモリ消費極小**: Tauriの30〜50MBから **3〜8MB** へ激減。
- **起動・反応速度**: ゼロ秒起動。ホバー時の描画ラグ皆無。
- **完全単一exe**: ランタイム（Webview2やNode/Rust不要）なしで単体動作可能。
- **ネイティブ統合**: OLE `IDropTarget` によるOSネイティブのドラッグ＆ドロップ完全対応。

---

## 2. 技術スタック & ビルド構成

| 領域 | 採用技術 / API | 役割 |
|---|---|---|
| **コンパイラ** | MSVC (`cl.exe` 64-bit) | Visual Studio Build Tools 2022 |
| **ウィンドウ制御** | Win32 API (`user32.lib`) | レイヤードウィンドウ、エッジ配置、アニメーション |
| **GUI描画** | GDI+ (`gdiplus.lib`) | アンチエイリアス、角丸矩形、半透明カード、画像サムネ描画 |
| **視覚効果** | DWM API (`dwmapi.lib`) | Windows 10/11のアクリル/ブラー/影効果 |
| **D&D (入出力)** | OLE / COM (`ole32.lib`) | `IDropTarget` (受取), `IDropSource` (外部へのD&D) |
| **クリップボード** | Win32 Clipboard API | `CF_UNICODETEXT`, `CF_HDROP`, `CF_BITMAP` |
| **データ管理** | SQLite Amalgamation (`sqlite3.c`) | 単一Cソース組み込み、外部DLL不要 |
| **キャッシュ実体** | `%LOCALAPPDATA%\DropLocker\cache\` | 一時ファイル保管 |

---

## 3. アーキテクチャ構成図

```mermaid
graph TD
    subgraph UI_Thread [UIスレッド (Win32 Message Loop)]
        WndProc[メイン WndProc]
        GdiRender[GDI+ ダブルバッファ描画エンジン]
        AnimTimer[スライドアニメーションタイマー (60fps)]
        DropTarget[OLE IDropTarget (ファイル/テキスト受取)]
        DropSource[OLE IDropSource (外部アプリへドラッグアウト)]
    end

    subgraph Background_Thread [ワーカースレッド]
        TTLWorker[TTL監視スレッド (60秒毎)]
        FileCleaner[期限切れファイル削除ルーチン]
    end

    subgraph Storage [永続化・キャッシュ]
        SQLite[(組み込み SQLite: metadata.db)]
        DiskCache[ローカルディスク: %LOCALAPPDATA%\cache\]
    end

    WndProc -->|イベント駆動| GdiRender
    WndProc -->|マウス検知| AnimTimer
    DropTarget -->|受取データ格納| SQLite
    DropTarget -->|ファイルコピー| DiskCache
    TTLWorker -->|期限切れクエリ| SQLite
    TTLWorker -->|実体ファイル消去| FileCleaner
    FileCleaner --> DiskCache
```

---

## 4. モジュール構成 & ファイル設計

```text
src/
├── main.c           // エントリポイント (WinMain), メッセージループ, 初期化
├── window.c / .h    // ウィンドウ作成, レイヤード属性, アニメーション (スライドイン/アウト)
├── renderer.c / .h  // GDI+ 描画ロジック (カード, TTLゲージ, アイコン, テキスト)
├── drop_target.c/.h // OLE IDropTarget C言語実装 (D&D受取)
├── drop_source.c/.h // OLE IDropSource / IDataObject C言語実装 (外へのドラッグ)
├── storage.c / .h   // SQLiteラッパー (アイテム追加, 削除, ピン留め, 一覧取得)
├── ttl_daemon.c/.h  // バックグラウンドTTL監視・パージスレッド
├── sqlite3.c / .h   // SQLite 公式 Amalgamation
└── resource.rc      // アイコン、マニフェスト (Per-Monitor DPI v2対応)
```

---

## 5. 詳細機能仕様

### 5.1 ウィンドウ状態・エッジスライド仕様
1. **Collapsed 状態 (待機)**:
   - 画面右端、X座標 = `ScreenWidth - 4`、幅 = `4px`、高さ = `140px`。
   - `WS_EX_TOPMOST | WS_EX_TOOLWINDOW`（Alt+Tabに非表示）。
   - マウスホバーまたはドラッグ進入検知で `Expanded` へ遷移。
2. **スライドアニメーション**:
   - `WM_TIMER` (16ms = ~60fps) で `SetWindowPos` をイージング補間。
   - 展開時: 幅 `340px`、高さ `画面高の80%`（スライド時間: 150ms）。
3. **折りたたみ判定**:
   - マウスポインタがウィンドウ外に出てから `400ms` のディレイ後に自動スライドバック。
   - パネル内「Pin」ボタン有効時は自動折りたたみを停止（画面固定）。

### 5.2 OLE `IDropTarget` (C言語実装)
C言語では COM インターフェースを構造体と関数ポインタテーブル (`Vtbl`) で実装:
```c
typedef struct {
    IDropTargetVtbl *lpVtbl;
    LONG refCount;
    HWND hwnd;
} CDropTarget;

// 処理対象クリップボードフォーマット:
// 1. CF_HDROP      -> エクスプローラーからのファイルドラッグ
// 2. CF_UNICODETEXT -> ブラウザやエディタからのテキスト選択ドラッグ
// 3. CF_DIB / DIBV5 -> 画像データの直接ドラッグ
```

### 5.3 GDI+ 描画パイプライン
- ウィンドウのチラつき防止のため、メモリDC上に `GdipCreateFromHDC` で全要素をオフスクリーン描画。
- `BitBlt` または `UpdateLayeredWindow` で一括転送。
- Windows 11 Fluent風デザイン:
  - 背景: 深いダークグレー `#1E1E1E` (Alpha: 230 / 半透明)
  - カード: `#2A2A2A` (角丸 8px, 1pxの淡いボーダー `#3E3E3E`)
  - TTLゲージ: 残り時間に応じた円形プログレスまたはバー表示 (緑 -> 黄 -> 赤)

### 5.4 24時間TTL自動消滅デーモン
```c
DWORD WINAPI TTLMonitorThread(LPVOID lpParam) {
    while (g_bRunning) {
        Sleep(60000); // 1分間隔
        Storage_PurgeExpiredItems(); // 期限切れアイテムDB & ファイル削除
        PostMessage(g_hWnd, WM_APP_ITEMS_UPDATED, 0, 0); // UI再描画要求
    }
    return 0;
}
```

---

## 6. 長時間常駐・省リソース設計 (Zero-Leak / Ultra-Low-Footprint)

常時起動ウィジェットで問題となる「メモリリーク」「GDIハンドル枯渇」「アイドル時CPU浪費」を根本から封じる設計方針。

### 6.1 CPU使用率 0.0% 維持（アイドル完全休止）
- **アニメーションタイマー即時破棄**:
  - スライド完了の瞬間、`KillTimer(hWnd, TIMER_ANIM)` を実行。アイドル中に定期タイマーを回さない。
- **ポーリング禁止 (OSイベント駆動)**:
  - `GetCursorPos` のループ監視は禁止。
  - エッジ判定は極小ウィンドウへの `WM_MOUSEMOVE` + `TrackMouseEvent(&tme)` (`TME_LEAVE`) のみで検知。
- **TTLパージの省電力待機**:
  - バックグラウンドスレッドは `WaitForSingleObject` / `CreateWaitableTimer` で休止。CPUサイクル消費ゼロ。

### 6.2 メモリ消費極小化 & ワーキングセット刈り取り
- **Collapsed時のメモリ自動返還 (Working Set Trim)**:
  - ドックが閉じた（Collapsed）5秒後に、不要な描画バッファ・サムネイルキャッシュを全破棄。
  - `SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);` を呼び出し、OSへ物理メモリを強制返還。
  - **平常時メモリ消費: 1.5MB〜3MB** を維持。
- **サムネイルのLRUキャッシュ (上限10枚)**:
  - 画像ファイルはフルサイズで保持せず、128x128ピクセルにリサイズした小型ビットマップのみを最大10枚までメモリキャッシュ。
  - 10枚を超えたものは解放し、スクロール時にディスクからオンデマンド再生成。
- **仮想化描画 (Virtual Scrolling)**:
  - カードが100件あっても、画面内に見えている5〜7件のカードのみGDI+で描画。

### 6.3 GDI / User ハンドル リーク完全防止
Windowsプロセスの上限（10,000オブジェクト）到達によるクラッシュを防止:
- **静的リソースの永続化**:
  - フォント (`GpFont`)、基本ブラシ (`GpBrush`)、ペン (`GpPen`) は起動時（`InitRenderer`）に1度だけ生成し、終了時（`ShutdownRenderer`）まで再生成しない。
- **動的リソースの厳格なライフサイクル**:
  - 一時的な描画パスやオフスクリーンビットマップは必ず関数脱出時に `GdipDeleteGraphics`, `GdipDisposeImage` を対で呼ぶ構造をマクロ・規約で固定。

### 6.4 ディスク I/O 抑制
- **SQLite設定**:
  - `PRAGMA journal_mode = WAL;`
  - `PRAGMA synchronous = NORMAL;`
  - `PRAGMA temp_store = MEMORY;`
  - ディスク書き込み頻度を極小化し、SSD/HDDへの負荷を排除。

---

## 7. ビルドコマンド (MSVC)

Visual Studio Build Tools の開発者コマンドプロンプト (`vcvars64.bat` / `LaunchDevCmd.bat`) で直接ビルド:

```cmd
cl /O2 /W3 /DUNICODE /D_UNICODE ^
   src\main.c src\window.c src\renderer.c src\drop_target.c ^
   src\storage.c src\ttl_daemon.c src\sqlite3.c ^
   /Fe:DropLocker.exe ^
   /link user32.lib gdi32.lib gdiplus.lib shell32.lib ole32.lib dwmapi.lib shlwapi.lib
```
→ 出力: 外部DLL不要の軽量な `DropLocker.exe` 1つのみ。

---

## 8. 実装ロードマップ

1. **Step 1**: 省リソース Win32 エッジウィンドウ (タイマー即破棄 + TrackMouseEvent + メモリ刈り取り機構)
2. **Step 2**: GDI+ ダブルバッファ描画エンジン (静的ブラシ使い回し + 仮想描画)
3. **Step 3**: OLE `IDropTarget` C言語実装 (D&D受取 + 128pxサムネイル生成)
4. **Step 4**: SQLite組み込み + 24H TTL自動パージデーモン (WaitableTimer)
5. **Step 5**: 外部アプリへのファイルドラッグアウト (`IDropSource`) & トレイ常駐


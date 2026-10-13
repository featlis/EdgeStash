# EdgeStash (DropLocker v2.0)

**超軽量・ゼロ遅延・24時間自動消滅デスクトップスクラップドック**  
TauriやWebviewを全廃し、純ネイティブ（Win32 API + GDI+ + SQLite）で構築された常駐型ウィジェットです。

---

## 1. 特徴と利点

- **超省メモリ (3〜8MB)**:
  - Collapsed（折りたたみ待機）状態では 5秒後に `SetProcessWorkingSetSize` によるワーキングセット強制返還を実行。平常時 1.5MB〜3MB を維持。
- **CPU 0.0% 完全休止**:
  - スライドアニメーション完了直後にタイマーを即時破棄 (`KillTimer`)。
  - アイドル時のポーリング監視は一切排除し、OSイベント駆動 (`TrackMouseEvent`) のみで動作。
  - バックグラウンドTTL監視は `WaitForMultipleObjects` による省電力休止。
- **単一バイナリ (ポータブル)**:
  - SQLite Amalgamation (`sqlite3.c`) を内蔵し、外部DLLやランタイム（Node/Webview/Rust）に依存せず単体 `EdgeStash.exe` のみで動作。
- **双方向ネイティブ D&D 完全対応**:
  - **受取 (`IDropTarget`)**: エクスプローラーからのファイル (`CF_HDROP`)、ブラウザ・エディタからのテキスト (`CF_UNICODETEXT`)、画像のドラッグ＆ドロップに対応。自動で128x128サムネイルを生成。
  - **外部搬出 (`IDropSource`)**: ドック内のカードをエクスプローラー、Photoshop、Discord、ブラウザ等へ直接ドラッグアウト可能。
- **24時間 TTL 自動消滅 & ピン留め**:
  - ドロップされたアイテムは24時間後に自動パージ（ファイル実体およびメタデータ）。
  - 重要アイテムは「Pin」ボタンで永続保持可能。

---

## 2. 操作方法

| 操作 | アクション |
|---|---|
| **画面右端にマウスホバー** | ドックがスムーズにスライドイン展開 (160ms / 60fps) |
| **画面右端へファイル等をD&D** | ドックが自動展開され、ドロップで即座にスタッシュ |
| **画面外へマウス移動** | 400ms 後に自動スライドバック (Pin時は画面固定) |
| **ショートカット `Win + Alt + V`** | ドックの展開 / 折りたたみをトグル |
| **タスクトレイアイコン** | 左クリックでトグル、右クリックでコンテキストメニュー表示 |
| **カードのドラッグ** | 外部アプリケーション（エクスプローラー等）へ直接ファイル / テキストをD&D |
| **カード右側の [C] ボタン** | クリップボードへコピー |
| **カード右側の [P] ボタン** | 24時間消滅の無効化（永続化ピン留め）/ 解除 |
| **カード右側の [x] ボタン** | アイテム即時削除 |

---

## 3. ディレクトリ構成

```text
EdgeStash/
├── src/
│   ├── main.c           // エントリポイント, 単一インスタンス制御, メッセージループ
│   ├── window.c / .h    // レイヤードウィンドウ, 60fpsイージング, ホバー検知, ワーキングセット刈り取り
│   ├── renderer.cpp/.h  // GDI+ ダブルバッファ描画エンジン, サムネイルLRUキャッシュ, 仮想スクロール
│   ├── drop_target.c/.h // OLE IDropTarget C言語実装 (D&D受取)
│   ├── drop_source.c/.h // OLE IDropSource / IDataObject C言語実装 (外部アプリへのD&D搬出)
│   ├── storage.c / .h   // SQLite組み込みラッパー (WALモード, TTLパージ, キャッシュ管理)
│   ├── ttl_daemon.c/.h  // バックグラウンドTTL監視デーモンスレッド
│   ├── tray.c / .h      // タスクトレイアイコン・コンテキストメニュー
│   ├── common.h         // 共通データ構造, メッセージ定義
│   ├── sqlite3.c / .h   // SQLite 3.47 公式 Amalgamation
│   ├── manifest.xml     // Per-Monitor DPI v2 対応マニフェスト
│   └── resource.rc      // リソース定義
├── build.bat            // MSVC 64-bit 自動ビルドスクリプト
└── README.md
```

---

## 4. ビルド方法

### 4.1 実行ファイル (EdgeStash.exe) のビルド
Visual Studio 2022 (Build Tools または Community) がインストールされている環境で、以下を実行します:

```cmd
build.bat
```
ルート直下に最適化済み単一実行ファイル `EdgeStash.exe` が生成されます。

### 4.2 インストーラー (Setup.exe) の作成
Inno Setup 6 環境で以下を実行します:

```cmd
build_installer.bat
```
`dist/EdgeStash_Setup_v2.0.0.exe` が生成されます。

---

## 5. インストーラーの機能

- **特権昇格不要 (User-Level Install)**: 管理者権限不要で `%LOCALAPPDATA%\Programs\EdgeStash` にインストール可能（管理者権限での全体インストールにも対応）。
- **スタートメニュー & デスクトップ ショートカット登録**。
- **Windows ログオン時の自動起動オプション** (`HKCU\Software\Microsoft\Windows\CurrentVersion\Run`)。
- **多言語対応**: 日本語 / 英語インストーラー。
- **完全アンインストール機能**: Windows「設定」→「インストールされているアプリ」から削除可能。アンインストール時にキャッシュ・DBの完全消去確認プロンプト付き。
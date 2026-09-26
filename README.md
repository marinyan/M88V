# M88V — V is for Vibe coding

PC-8001 / PC-8801 向けの開発支援エミュレータです。
Windows GUI での実行に加え、ローカル HTTP API からキー入力・画面取得・メモリ参照・実行計測を行えます。
自作プログラムの動作確認や、入力と実行状態を再現するテストに使えます。

## 主な機能

- **プログラムの実行**：BIN の直接ロード、起動時の機種・BASIC モード指定。
- **テープ操作**：T88 の読み込み・空ファイル作成・巻き戻し・取り外し、CMT 出力の録音と T88／CMT 保存。
- **リズム音源**：YM2608のリズムROMをADPCMで再生。ROMがない場合は従来のWAVを使用。[設定と検証](docs/RHYTHM-ja.md)。
- **FM音源**：OPN設定はYM2203、OPNA設定はYM2608のymfmコアで生成。本体・増設音源を個別に設定可能。[設定と検証](docs/FM-ja.md)。
- **自動操作**：画面を開かない headless 実行、指定フレーム数の実行、キー入力、PNG 取得。
- **デバッグ**：Z80 レジスタ・RAM/TVRAM/GVRAM の参照、メモリダンプ、CPU の読み書き別メモリマップ。
- **計測と再現**：T-state 計測、書き込み元追跡、ウォッチ停止、命令・レジスタ履歴、状態保存・復元、入力記録・再生。

Windows の標準 GUI は Win32 版です。任意選択の raylib 版と headless 版も共通の CPU コアを使います。
M88V の変更は Windows x64 で検証しています。他 OS での実行確認は未実施です。

## はじめる

**ROM は同梱していません。** 所有する実機から取得した ROM を用意してください。
必要なファイルと対応モードは [機種・ROM・起動設定](docs/SETUP-ja.md) を参照してください。
以下のコマンドはリポジトリのルートで実行します。

### Windows GUI を使う

ビルド済みの [m88v.exe](dist/windows-x64/m88v.exe) を収録しています。
PowerShell で ROM の場所とモードを指定して起動できます。

```powershell
$env:M88V_ROM_DIR = 'D:\path\to\roms'
$env:M88V_BASIC_MODE = 'N88V2'
.\dist\windows-x64\m88v.exe
```

PC-8001mkII なら `N802`、PC-8001mkIISR なら `N80V2` を指定します。
設定は EXE と同じ場所の `m88v.ini` に保存されます。
T88 の操作と録音の保存は `Tape` メニューから行います。再生テープと録音バッファは独立しています。
詳しくは [テープ操作](docs/TAPE-ja.md) を参照してください。

### 自作 BIN を GUI で起動する

PowerShell 7 で、プログラムに合った機種とロード先を指定します。

```powershell
.\scripts\start_gui_game.ps1 -RomDirectory D:\path\to\roms `
    -BasicMode N88V2 -Bin D:\path\to\program.bin -Address 0xB000
```

BIN は物理主 RAM へ読み込みます。バンク切替は行わないため、対象に合う ROM/RAM マッピングが必要です。
ロード範囲・起動用ランチャ・スタックの詳細は [BIN 直接ロード](docs/HEADLESS_API-ja.md#bin直接ロード) を参照してください。

### API で操作する

headless 版を [ビルド](#ビルドと検証) してから、PowerShell 7 で起動します。

```powershell
.\scripts\start_headless.ps1 -RomDirectory D:\path\to\roms -BasicMode N88V2
.\scripts\m88ctl.ps1 run -Frames 180
.\scripts\type_nbasic.ps1 -Line ''
.\scripts\type_nbasic.ps1 -Line 'PRINT "M88V"'
.\scripts\m88ctl.ps1 capture -Output build\frame.png
.\scripts\m88ctl.ps1 shutdown
```

エミュレーション時間は要求した分だけ進みます。HTTP API は `127.0.0.1` で待ち受け、
起動ごとのトークン認証が必要です。接続ファイルにはトークンが含まれるため、公開・コミットしないでください。
エンドポイントと操作例は [ローカル API](docs/HEADLESS_API-ja.md) にまとめています。

## ドキュメント

| 調べたいこと | 資料 |
| --- | --- |
| 対応する機種・BASIC モード、必要な ROM、環境変数 | [機種・ROM・起動設定](docs/SETUP-ja.md) |
| T88 の作成・再生、CMT 出力の保存 | [テープ操作](docs/TAPE-ja.md) |
| ディスク・テープ名とアクセスランプの試作 | [メディア状態バー](docs/MEDIA_STATUS-ja.md) |
| API、BIN ロード、キー入力、画面・メモリ取得 | [ローカル API](docs/HEADLESS_API-ja.md) |
| 計測、ウォッチ、状態保存、入力記録・再生 | [開発ツール](docs/DEVELOPMENT_TOOLS-ja.md) |
| Windows GUI の構成、設定、raylib 版のビルド | [Windows GUI](docs/NATIVE_WINDOWS-ja.md) |
| 実施済みの検証と未確認項目 | [検証範囲と結果](docs/VALIDATION-ja.md) |
| 派生元のリビジョンと改変内容 | [取り込み元と変更履歴](UPSTREAM.md) |

## ビルドと検証

Windows では C++ デスクトップ開発環境を含む Visual Studio、Windows SDK、CMake、Ninja、PowerShell 7 を使います。

```powershell
.\scripts\build_gui.ps1
.\scripts\build_headless.ps1
```

GUI のビルドは `dist/windows-x64/m88v.exe` を更新します。
headless のビルドスクリプトは ROM 不要の CTest も実行します。

```powershell
# ビルド後にテストだけ実行
ctest --test-dir build/headless-msvc --output-on-failure

# 手元の ROM を使用する統合テスト
.\scripts\test_development_modes.ps1 -RomDirectory D:\path\to\roms
.\scripts\test_debug_tools.ps1 -RomDirectory D:\path\to\roms
.\scripts\test_checkpoints.ps1 -RomDirectory D:\path\to\roms
```

M88M 由来の macOS / Linux / FreeBSD / Haiku 向けビルド設定も保持しています。
GUI を使わない CMake ビルドは次のとおりです。

```sh
cmake -S . -B build/headless -DM88M_BUILD_GUI=OFF -DBUILD_TESTING=ON
cmake --build build/headless --parallel
ctest --test-dir build/headless --output-on-failure
```

検証の範囲は [検証記録](docs/VALIDATION-ja.md) を参照してください。
実機と同一のタイミングや全ソフトの互換性を保証するものではありません。最終確認は対象実機でも行ってください。

## 派生元

[cisc 氏の M88](http://retropc.net/cisc/m88/) を元にした [bubio/M88M](https://github.com/bubio/M88M) から派生しています。
Windows GUI は [rururutan/m88](https://github.com/rururutan/m88) の従来 Win32 版を基礎に、M88V の修正済みコア・開発 API と組み合わせています。
自作ゲームの開発で追加した機能を、ゲームに依存しない専用プロジェクトへ分離しました。
取り込み元のリビジョンと改変内容は [UPSTREAM.md](UPSTREAM.md) に記載しています。

## ライセンス

**M88Vの新規ファイル・追加コードはBSD-2-Clauseです。リポジトリ全体をBSDへ再ライセンスしたものではありません。**

- M88Vの追加部分、M88Mの新規コード・移植部分：[BSD-2-Clause](LICENSE)。
- 元のM88コア：copyright cisc。[元の独自ライセンス](docs/README.md#ライセンス)を維持。
  著作権・改変内容の表示、`src/pc88`を組み込む場合のソース公開、商用利用時の事前合意などの条件があります。
- 同梱フォント：[SIL Open Font License 1.1](assets/OFL.txt)、[著作権表示](assets/NOTICE.md)。
- その他の第三者コードには、それぞれのファイルに記載された条件が適用されます。

M88Vはcisc氏・Bubio氏による公式リリースではありません。

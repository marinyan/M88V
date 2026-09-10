# Windows GUIの土台

Windowsの標準GUIはrururutan/m88の従来Win32版です。
基準は`1c48d83070202eef43ab00db757131d0cc4768cc`（2020-12-17）。
リポジトリ全体を昔の状態へ戻したり、Git履歴をリベースしたものではありません。
既存のWin32ソースを正式にビルド対象へ戻し、M88Vの共通コアへ接続しています。

- `m88_win32` → 従来メニュー・ディスク・Tape/Open・キーボード・描画・音声。
- `m88_raylib` → 任意のポータブルGUI。Windowsでは別名`m88v-raylib.exe`。
- `m88_headless` → 既存の開発用HTTP API、計測、メモリマップ、ウォッチ、履歴、記録・再生。
- 3つとも同じ`m88core`・`m88_development`をリンクします。GUIだけ古いx86 CPUへ戻しません。

53Hのリセット時初期化とC++ Z80のVRAMウェイト修正は維持しています。
計測結果はCPUのエミュレート時間であり、実機そのものの測定値ではありません。

## ビルド

```powershell
.\scripts\build_gui.ps1
# 任意のraylib版。標準配布EXEを上書きしない別名。
.\scripts\build_gui.ps1 -Frontend raylib
```

Win32版は`dist/windows-x64/m88v.exe`です。`-NoPublish`はビルドのみ行います。
`M88V_GUI_FRONTEND=win32|raylib`でCMakeからも切り替えられます。
旧Visual Studio 2008プロジェクトではなく、CMakeと現在のWindows SDKを使ってください。

MSVC/Ninjaでは`/showIncludes`の言語と依存追跡の接頭辞を一致させています。
日本語版コンパイラだけがある場合も英語リソースを前提にしません。
`scripts/test_msvc_dependencies.ps1 -BuildDirectory build/gui-win32-msvc`で確認できます。
新しいソースを追加した場合は、ビルドスクリプトを再実行してCMakeを再構成してください。

## 設定とROM

設定はEXEと同じ場所の`m88v.ini`へ保存します。INIのセクション名は旧形式互換の
`[M88p2 for Windows]`です。別名のEXEを作った場合はそのファイル名のINIになります。
旧M88のINIやraylib版のバイナリ設定を自動移行・変更する処理はありません。

ROM参照先の優先順位は次のとおりです。

1. `M88V_ROM_DIR`、未設定または空なら`M88M_ROM_DIR`。
2. INIの`BIOSPath`。
3. EXEと同じフォルダの`roms`、なければ`rom`。
4. EXEと同じフォルダ（`N88.ROM`または`PC88.ROM`がある場合）。
5. `%APPDATA%/M88M/roms`。

明示指定した参照先の不足はエラーとし、別のROMセットへ黙って切り替えません。
N80 ROMの別名処理は共通の一時オーバーレイを使い、元ROMを書き換えません。
通常起動の既定はN88V2。ゲーム開発用スクリプトでは従来どおり機種を明示できます。

## テープと開発用起動

`Tape → Open`でT88を開き、対象に合った`CLOAD`／`MON`等を入力します。
T88のドラッグ＆ドロップも従来のテープ処理へ渡します。開くだけで実行は始まりません。
開いたテープは保存先にもなるため、大切な原本はコピーを使ってください。

`start_gui_game.ps1`と`M88V_LOAD_BIN`／`M88V_LOAD_ADDRESS`を引き継いでいます。
BINは物理RAMへ置き、EFF0Hのランチャから開始します。ロード前にCPUを走らせません。
Win32版とraylib版のBINローダ実装は共通です。

## 状態保存

ディスク未マウント・テープを閉じた開発セッションでは、従来の状態保存メニューから
M88Vの検証付きチェックポイント形式を使います。Win32キーボード状態も保持します。
異なるGUI／headlessのチェックポイントは入力状態の形式が違うため相互ロードしません。
音声出力キュー・音源位相のサンプル単位の一致は対象外です。

メディアがある場合は従来のスナップショット経路を使用します。外部ディスク・テープの
書き込み自体を巻き戻す機能ではありません。開発APIの詳細は既存ドキュメントを参照してください。

## 確認範囲

ビルド・自動テストと、実際のWin32ウィンドウの操作確認は区別して記録します。
最新の実施結果と未確認項目は[検証記録](VALIDATION-ja.md)を参照してください。

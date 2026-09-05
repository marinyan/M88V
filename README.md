# M88V — V is for Vibe coding

PC-8001 / PC-8801向けの開発支援エミュレータです。
[cisc氏のM88](http://retropc.net/cisc/m88/)を元にした
[bubio/M88M](https://github.com/bubio/M88M)から派生しています。
Defender80の開発で追加した機能を、ゲームに依存しない専用プロジェクトへ分離しました。
取り込み元のリビジョンと改変内容は[UPSTREAM.md](UPSTREAM.md)に記載しています。

## 開発機能

- GUIでのBIN直接起動。起動時に機種・BASICモードを指定できます。
- ウィンドウ不要のローカルHTTP API。要求したフレーム数だけ実行します。
- キー入力、Z80レジスタ・物理RAM/TVRAM/GVRAMの参照。
- PNG画面取得、同一時点のメモリダンプ、T88テープのオープン。
- GUIとheadlessで共通の機種定義・ROM検査・一時的なROMファイル名の別名対応。

通常GUIのディスク管理・音声・設定などはM88Mから引き継いでいます。
HTTP APIは`127.0.0.1`だけで待ち受け、起動ごとのトークン認証を必要とします。
接続ファイルにはトークンがあるため、公開・コミットしないでください。

## 開発起動で指定できるモード

| 指定値 | 対象 |
| --- | --- |
| `N802`（既定） | PC-8001mkII / N80 BASIC |
| `N80V2` | PC-8001mkIISR / N80 BASIC Ver.1.2 |
| `N` | PC-8801 / N-BASIC |
| `N88V1` | PC-8801 / N88-BASIC V1S |
| `N88V1H` | PC-8801 / N88-BASIC V1H |
| `N88V2` | PC-8801 / N88-BASIC V2 |

`N`はPC-8001mkIIモードではありません。曖昧な`N80`指定は受け付けません。
PC-88VAのネイティブモードは対象外です。
元のコアにはCD-ROM用の`N88V2CD`もありますが、この開発起動インターフェースの
選択・検証対象には含めていません。

## ROM

ROMは同梱しません。所有する実機から取得したROMを、別のディレクトリに用意してください。
ファイル名の大文字・小文字は区別せず、一時ディレクトリ内でのみ別名を作ります。
元のROMは変更しません。

- 全モードで`N88.ROM`または結合形式の`PC88.ROM`が必要です（元のコアの初期化用）。
- フォントは`FONT.ROM`、`FONT80.ROM`、または`KANJI1.ROM`が必要です。
- `N802`では`N80_2.ROM`を使います。代替名として`N80_11.ROM`、`N80_102.ROM`、
  `N80_101.ROM`も認識します。この順で優先し、`-N80Rom`で明示指定できます。
- `N80V2`では上記に加えて`N80_3.ROM`が必要です。
- PC-88の分割ROM形式では、`N`に`N80.ROM`、N88系に`N88_0.ROM`～`N88_3.ROM`、
  いずれも`DISK.ROM`が必要です。結合`PC88.ROM`使用時はこれらを内包します。
- PC-88モードにはPC-80用の`N80_2.ROM`／`N80_3.ROM`は不要です。

必要なROMの内容・版は対象機種に合わせてください。別名の認識だけで機種互換性が
保証されるわけではありません。漢字・辞書・80SR専用フォント等は用途に応じて追加します。

## Windowsでのビルドと起動

C++デスクトップ開発環境を含むVisual Studio、CMake、PowerShell 7を使用します。
この環境での専用ビルドスクリプトはMSVC＋Ninjaを利用します。

```powershell
.\scripts\build_headless.ps1
.\scripts\build_gui.ps1

# PC-88 V2。PC-80 mkIIなら -BasicMode N802
.\scripts\start_headless.ps1 -RomDirectory D:\path\to\roms -BasicMode N88V2
.\scripts\m88ctl.ps1 run -Frames 180
.\scripts\type_nbasic.ps1 -Line ''
.\scripts\type_nbasic.ps1 -Line 'PRINT "M88V"'
.\scripts\m88ctl.ps1 capture -Output build\frame.png
.\scripts\m88ctl.ps1 shutdown

# GUIで自作BINを起動（ロード先とBINはプログラムに合わせる）
.\scripts\start_gui_game.ps1 -RomDirectory D:\path\to\roms `
    -BasicMode N88V2 -Bin D:\path\to\program.bin -Address 0xB000
```

`dist/windows-x64/m88m.exe`にWindows GUIビルドを収録しています。
スクリプト・実行ファイル名や`M88M_ROM_DIR`など一部の名称は既存環境との互換性のため維持します。
GUIを直接起動する場合は既存の設定モードを保持し、BINの指定だけでN802に変更しません。
`M88V_BASIC_MODE`を指定すると上の6モードから選べます。

BIN直接ロードは物理主RAMへの書き込みで、バンク切替は行いません。
`EFF0H`のランチャと`F000H`から下向きのスタックを使います。
対象プログラムに合うROM/RAMマッピングが必要です。
詳細は[ローカルAPI](docs/HEADLESS_API-ja.md)を参照してください。

## 検証

```powershell
# ROM不要のCTest 3件は build_headless.ps1 でも実行
ctest --test-dir build/headless-msvc --output-on-failure

# 手元のROMを使用する6モードの統合テスト
.\scripts\test_development_modes.ps1 -RomDirectory D:\path\to\roms
```

[検証範囲と結果](docs/VALIDATION-ja.md)を参照してください。
実機と同一のタイミング・全ソフトの互換性を保証するものではありません。
最終確認は対象実機でも行ってください。

M88M由来のmacOS/Linux/FreeBSD/Haiku向けCMake設定・ビルドスクリプトも保持しています。
今回のM88V変更はWindows x64で検証しました。他OSでの実行確認は未実施です。
GUIを使わないビルドは次のとおりです。

```sh
cmake -S . -B build/headless -DM88M_BUILD_GUI=OFF -DBUILD_TESTING=ON
cmake --build build/headless --parallel
ctest --test-dir build/headless --output-on-failure
```

## ライセンス

**M88Vの新規ファイル・追加コードはBSD-2-Clauseです。リポジトリ全体をBSDへ再ライセンスしたものではありません。**

- M88Vの追加部分、M88Mの新規コード・移植部分：[BSD-2-Clause](LICENSE)。
- 元のM88コア：copyright cisc。[元の独自ライセンス](docs/README.md#ライセンス)を維持。
  著作権・改変内容の表示、`src/pc88`を組み込む場合のソース公開、商用利用時の事前合意などの条件があります。
- 同梱フォント：[SIL Open Font License 1.1](assets/OFL.txt)、[著作権表示](assets/NOTICE.md)。
- その他の第三者コードには、それぞれのファイルに記載された条件が適用されます。

M88Vはcisc氏・Bubio氏による公式リリースではありません。

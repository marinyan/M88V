# M88V / m88-headless ローカルAPI

## 性質

- 待受先は常に`127.0.0.1`で、外部インターフェースへは公開しない。
- `/v1/*`は起動時に生成したトークンを`X-M88-Token`または
  `Authorization: Bearer`で要求する。
- エミュレーション時間は自動進行しない。`POST /v1/run?frames=N`でのみ進む。
- BASICモードは既定でPC-8001mkII用の`PC8801::Config::N802`を使う。
- `--basic-mode n80v2`（PowerShellでは`-BasicMode N80V2`）を指定すると、
  `N80_3.ROM`を使うN80 BASIC Ver.1.2モードで起動する。
- PC-8801向けには`n`（N-BASIC）、`n88v1`（V1S）、`n88v1h`（V1H）、
  `n88v2`（V2）を指定できる。PC-80用ROMは不要。選択は起動時に行う。
- `status.machine`は`PC-8001mkII`、`PC-8001mkIISR`、`PC-8801`のいずれか。
  `status.mode`は選択したモード名、PC-88の場合`status.n80_rom`は空文字列。
- アドレス値は10進、`0xC000`、`C000H`のいずれも受け付ける。

接続URLとトークンは既定で`.m88-headless/connection.json`へ書かれる。
通常はHTTPを直接組み立てず、`scripts/m88ctl.ps1`を使用する。

`N80_2.ROM`のほか、一般的な吸い出し名`N80_11.ROM`、`N80_102.ROM`、
`N80_101.ROM`を認識する。`FONT80.ROM`も`FONT.ROM`相当として認識する。
起動スクリプトの`-N80Rom`で版を明示できる。M88用の別名は一時オーバーレイ
内だけに作られ、元のROMディレクトリは変更しない。

## エンドポイント

| Method | Path | 用途 |
| --- | --- | --- |
| `GET` | `/health` | トークン不要の生存確認 |
| `GET` | `/v1/status` | モード、フレーム数、画面サイズ、CPUレジスタ |
| `GET` | `/v1/registers` | メインZ80レジスタ |
| `POST` | `/v1/reset` | 起動時に選択したBASICモードでリセットし、全キーを解放 |
| `POST` | `/v1/run?frames=N` | 指定フレームだけ同期実行 |
| `POST` | `/v1/load-bin?path=...&address=C000H` | BINを物理主RAMへロードしてランチャを設置 |
| `POST` | `/v1/tape/open?path=...` | T88テープを開き、先頭へ巻き戻す |
| `POST` | `/v1/key?name=left&down=1` | 名前指定のキー押下・解放 |
| `POST` | `/v1/key?row=10&bit=2&down=1` | マトリクス位置指定のキー押下・解放 |
| `POST` | `/v1/keys/release` | 全キー解放 |
| `GET` | `/v1/frame.png` | 640×400の現在フレームをPNG取得 |
| `POST` | `/v1/capture?path=...` | サーバー側の指定パスへPNG保存 |
| `GET` | `/v1/memory?space=ram&address=C000H&length=256` | メモリを16進JSONで取得 |
| `GET` | `/v1/dump` | 同一時点の`M88DMP1`を取得 |
| `POST` | `/v1/dump-file?path=...` | サーバー側の指定パスへ`M88DMP1`保存 |
| `POST` | `/v1/shutdown` | 応答後に正常終了 |

`space`は`ram`、`tvram`、`gvram-b`、`gvram-r`、`gvram-g`を指定できる。

## BIN直接ロード

既定のロード先は`C000H`である。ランチャを使う場合、BIN末尾は`EFF0H`
未満でなければならない。ロード後、`EFF0H`へ次のコードを置き、PCを
`EFF0H`へ設定する。

```text
LD SP,F000H
CALL <load-address>
JP 0000H
```

`launcher=0`を明示した場合はPCをロードアドレスへ直接設定する。
ロードは物理主RAMに対して行い、CPUのROM/RAMバンク切替はしない。
実行開始先・ランチャ・スタックがCPUからRAMとして見える状態で使う。
GVRAMを切り替えるコードは、対象の窓（PC-80では8000H～BFFFH、
PC-88ではC000H～FFFFH）の外へ置き、切替中のスタック参照にも注意する。

## T88テープ

`POST /v1/tape/open`はGUI版の`Tape -> Open`と同じ`TapeManager`へT88を渡す。
T88を開くだけでは転送・実行は始まらない。対象機種とテープ形式に合った
MON/CLOAD等の操作が別途必要。PowerShellクライアントでは次を使う。

```powershell
.\scripts\m88ctl.ps1 tape -Tape D:\path\to\program.t88
```

## キー名

英字`a`～`z`、数字`0`～`9`に加え、主に次を使用できる。

```text
up down left right enter space esc stop shift ctrl grph kana caps tab
f1 f2 f3 f4 f5 f6 f7 f8 f9 f10 backspace help copy
numpad0 numpad1 numpad2 numpad3 numpad4 numpad5 numpad6 numpad7 numpad8 numpad9
kp0 kp1 kp2 kp3 kp4 kp5 kp6 kp7 kp8 kp9 leftshift left_shift
```

キーの用途は対象プログラムが決める。エミュレータのキーマトリクスでは左右Shiftを区別しない。

キーはactive-lowで、`down=1`の間は対応ビットを`0`に保つ。ゲームの連続入力は
押下、複数フレーム実行、解放の順に行う。

## M88DMP1

ダンプは48バイトのヘッダに、物理主RAM 64 KiB、TVRAM 4 KiB、B/R/G順の
GVRAM各16 KiBを連結する。ヘッダは`M88DMP1\0`とlittle-endian 32-bit値10個で、
順にバージョン、PC、各領域の位置と長さ、GVRAM面数、予約値を持つ。
形式は全モードで共通。メモリ参照・ダンプは物理領域であり、現在のCPUバンクマッピングを
通した読み取りではない。N802のpacked GVRAMは`gvram-b`にあり、
`gvram-r/g`を加えてPC-88の3プレーン形式として解釈してはいけない。

## 実機との差

直接BINロード、フレーム単位実行、画面取得、メモリ取得は開発補助機能であり、
対象実機には存在しない。リリース候補は各プロジェクトの実機転送経路でも確認する。
今回の検証範囲は[VALIDATION-ja.md](VALIDATION-ja.md)を参照。

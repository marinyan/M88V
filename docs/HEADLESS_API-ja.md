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

ディスク・シリアル・Windows COM接続の詳細は
[ディスクとシリアルAPI](MEDIA_SERIAL_API-ja.md)を参照。

| Method | Path | 用途 |
| --- | --- | --- |
| `GET` | `/health` | トークン不要の生存確認 |
| `GET` | `/v1/status` | モード、フレーム数、画面サイズ、CPUレジスタ |
| `GET` | `/v1/capabilities` | ディスク装着・録音・録画への対応と出力形式 |
| `GET` | `/v1/registers` | メインZ80レジスタ |
| `POST` | `/v1/reset` | 起動時に選択したBASICモードでリセットし、全キーを解放 |
| `POST` | `/v1/run?frames=N` | 指定フレームだけ同期実行 |
| `POST` | `/v1/load-bin?path=...&address=C000H` | BINを物理主RAMへロードしてランチャを設置 |
| `GET` | `/v1/disks` | ドライブ1・2の装着状態、パス、イメージ番号、書き込み保護 |
| `POST` | `/v1/disk/mount?drive=1&path=...&index=0&readonly=1` | ディスク装着（既定は読み取り専用） |
| `POST` | `/v1/disk/unmount?drive=1` | ディスク取り外し |
| `POST` | `/v1/disk/select?drive=1&index=1` | 複数枚入りイメージ内のディスク切り替え |
| `POST` | `/v1/audio/start?path=...wav` | 音声のWAV保存を開始 |
| `POST` | `/v1/audio/stop` | WAVのヘッダを確定し保存終了 |
| `GET` | `/v1/audio/status` | 録音状態、保存先、サンプル数、秒数、エラー |
| `POST` | `/v1/video/start?path=...avi` | 音声付き動画のAVI保存を開始 |
| `POST` | `/v1/video/stop` | AVIのヘッダ・索引を確定し保存終了 |
| `GET` | `/v1/video/status` | 録画状態、保存先、フレーム数、秒数、エラー |
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

## ディスク装着

`drive`は1または2、`index`は0始まり。装着するだけではリセットしません。
ディスクから起動する場合は、装着後に`reset`し、それから実行を進めます。
返却されるディスクパスは、シンボリックリンク・ジャンクションを解決した絶対パスです。

```powershell
.\scripts\m88ctl.ps1 disk-mount -Drive 1 -Disk D:\images\disk1.d88
.\scripts\m88ctl.ps1 disk-mount -Drive 2 -Disk D:\images\disk2.d88
.\scripts\m88ctl.ps1 disks
.\scripts\m88ctl.ps1 reset
.\scripts\m88ctl.ps1 run -Frames 180
.\scripts\m88ctl.ps1 disk-unmount -Drive 2
```

書き込みが必要な場合は`-ReadOnly $false`を明示します。D88の保護フラグは優先されます。
複数枚入りD88、共有時の制限は[ディスクとシリアルAPI](MEDIA_SERIAL_API-ja.md)を参照。

## 音声・動画の保存

- 音声：WAV、48kHz・16bit・ステレオPCM。エミュレータ内のFM/SSG/BEEP等の出力を直接保存します。
- 動画：AVI、640×400、60fps、可逆PNG＋同じPCM音声。外部エンコーダや音声デバイスは不要です。
  PNG入りAVIに対応するプレーヤーを使用してください。MP4出力には対応していません。
- 録音・録画の時間はCPUの実行時間に従います。HTTP要求間の実時間の待ちは含まれません。
  映像は各実行フレームの画面を60fpsへ割り当て、必要に応じて重複・間引きを行います。
  動画の末尾は音声より最大1/60秒だけ長くなる場合があります。
- WAVとAVIは同時に保存できます。AVIだけを開始しても音声が入ります。
  GUIのメニュー・メディア状態バーは含まず、エミュレータのフレームバッファを保存します。
- `path`はサーバー側のパスです。保存先ディレクトリを先に作り、新しい`.wav`／`.avi`名を指定します。
  既存ファイルは上書きしません。PowerShellの`-Output`はクライアントで絶対パスに変換します。
- `stop`または正常な`shutdown`でファイルを確定します。強制終了した場合は未完成になることがあります。
  1ファイルは2GiB未満、AVI索引は約100万チャンクまで。上限・書き込みエラー時は保存を終了し、APIでエラーを返します。
- 録音・録画中はディスク装着・取り外しが可能です。リセット、状態ロード、入力リプレイ再生は一度停止してから行います。
  `/v1/replay/record/*`による入力記録と、音声・動画のファイル保存は別の機能です。

```powershell
.\scripts\m88ctl.ps1 capabilities
.\scripts\m88ctl.ps1 audio-start -Output build\sound.wav
.\scripts\m88ctl.ps1 video-start -Output build\screen.avi
.\scripts\m88ctl.ps1 run -Frames 600
.\scripts\m88ctl.ps1 audio-stop
.\scripts\m88ctl.ps1 video-stop
```

`start`／`stop`／`status`の応答は`active`、`path`、`format`、`sample_rate`、`channels`、
`samples`（左右1組を1サンプルとする）、`duration_seconds`、`frames`、`bytes`、`error`を含みます。
動画には`fps`もあります。録音のみでは`frames`は0です。停止後も直近の結果を保持します。
`status.media_recording.audio/video`からも実行中か確認できます。
二重開始・未開始の停止・録画中のリセット等は409、不正な保存先や形式は400、
HTTPメソッド違いは405、認証なしは401です。

保存状態からの音声再現には、SSG/BEEPの位相を含む精密チェックポイント形式6と、
headlessメタデータ版2を使用します。旧版の状態ファイル・入力リプレイは再作成してください。

## BIN直接ロード

既定のロード先は`C000H`である。ランチャを使う場合、BIN の最終バイトは
`FFF0H`未満に収める必要がある。ロード先とサイズに応じてランチャと初期スタックを選び、
PC をランチャの先頭へ設定する。

| ロード範囲（終端は含まない） | ランチャ | 初期 SP |
| --- | --- | --- |
| ロード先＋サイズが`EFF0H`以下 | `EFF0H` | `F000H` |
| ロード先＋サイズが`EFF0H`超、`FFF0H`以下 | `FFF0H` | `FFFFH` |

ランチャには次のコードを置く。

```text
LD SP,<initial-stack>
CALL <load-address>
JP 0000H
```

必要ならランチャは独立 TVRAM にも置く。スタックは下向きに伸びるため、
プログラムはまとまったスタックを使う前に、自身の配置に合う SP を設定する。

`launcher=0`を明示した場合はPCをロードアドレスへ直接設定する。
ロードは物理主RAMに対して行い、CPUのROM/RAMバンク切替はしない。
実行開始先・ランチャ・スタックがCPUからRAMとして見える状態で使う。
GVRAMを切り替えるコードは、対象の窓（PC-80では8000H～BFFFH、
PC-88ではC000H～FFFFH）の外へ置き、切替中のスタック参照にも注意する。

## T88テープ

空 T88 の作成、巻き戻し・末尾移動・取り外し、録音の T88／CMT 保存にも対応する。
追加エンドポイントと録音バッファの扱いは [テープ操作](TAPE-ja.md#ローカル-http-api) を参照。

`POST /v1/tape/open`はWin32 GUI版の`Tape -> Open`と同じ`TapeManager`へT88を渡す。
任意のraylib GUIには従来のTapeメニューはない。
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

シンボル計測、書き込みウォッチ/履歴、状態保存・入力リプレイ、現在のCPUメモリマップは
[開発ツールAPI](DEVELOPMENT_TOOLS-ja.md)を参照してください。

直接BINロード、フレーム単位実行、画面取得、メモリ取得は開発補助機能であり、
対象実機には存在しない。リリース候補は各プロジェクトの実機転送経路でも確認する。
今回の検証範囲は[VALIDATION-ja.md](VALIDATION-ja.md)を参照。

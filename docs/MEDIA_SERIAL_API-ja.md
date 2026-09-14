# ヘッドレスのディスク・シリアルAPI

すべて `/v1/*` の既存トークン認証が必要。接続後も時間は自動進行せず、
`POST /v1/run?frames=N` でゲストを実行する。

## ディスク

| Method | Path | 操作 |
| --- | --- | --- |
| GET | `/v1/disks` | 2台の状態（`/v1/status` の `disks` にも含む） |
| POST | `/v1/disk/mount?drive=1&path=...&index=0&readonly=1` | 既存イメージを装着 |
| POST | `/v1/disk/select?drive=1&index=1` | 複数枚入りコンテナの選択変更 |
| POST | `/v1/disk/unmount?drive=1` | 書き戻して取り外す |

`drive` は1または2、`index` は0始まり（0～63）。mountのindex既定値は0、
selectでは必須。pathはUTF-8でURLエンコードする。
`readonly` は0/1、既定値は1。D88ヘッダー側の書き込み禁止は優先する。
状態には `mounted`, `path`, `index`, `image_count`, `readonly`（実効値）、
`requested_readonly`（装着時の指定）を含む。未装着時のindexは-1。
同じコンテナの別イメージは両ドライブに装着できるが、読み取り専用指定を揃える。
同じイメージの二重装着は拒否する。不正なファイルや範囲外のindexは交換前に検査する。
新規作成・フォーマットはこのAPIの対象外。

```powershell
./scripts/m88ctl.ps1 disks
./scripts/m88ctl.ps1 disk-mount -Drive 1 -Disk C:\images\game.d88
./scripts/m88ctl.ps1 disk-select -Drive 1 -Index 1
./scripts/m88ctl.ps1 disk-unmount -Drive 1
# 書き込みを許可する場合
./scripts/m88ctl.ps1 disk-mount -Drive 2 -Disk C:\images\work.d88 -ReadOnly $false
```

## 仮想シリアル

8251のデータポート20H、制御ポート21Hへ接続するバイト単位のホスト接続。
ゲスト側でモード設定と送受信許可が必要。カセットとUSARTを共有するため、
テープを閉じて出力記録を保存・クリアしてから接続する。

| Method | Path | 操作 |
| --- | --- | --- |
| GET | `/v1/serial/status` | 状態（`/v1/status` の `serial` にも含む） |
| POST | `/v1/serial/open` | 仮想シリアル接続 |
| POST | `/v1/serial/write?hex=0041ff` | ホストからゲストへ送信 |
| POST | `/v1/serial/read?max=4096` | ゲストからの送信を取り出す |
| POST | `/v1/serial/clear` | キューと送信破棄数をクリア |
| POST | `/v1/serial/close` | 仮想・COM接続を閉じる |

hexは空白なしの偶数桁、1～4096バイト。readのmaxは1～4096（既定4096）。
readはデータを消費するのでPOST。結果の `hex` と `length` が受信内容。
送受信キューはそれぞれ65536バイト。受信空き不足のwriteは409で全量拒否する。
ゲストはTXRDYを確認して送信する。満杯を無視した送信は破棄し、`tx_dropped` に数える。
`rx_pending` はゲスト未読、`tx_pending` はホスト未読のバイト数。
ゲストのデータ長設定に従って上位ビットをマスクする。

```powershell
./scripts/m88ctl.ps1 serial-open
./scripts/m88ctl.ps1 serial-write -Hex 0041ff
./scripts/m88ctl.ps1 run -Frames 60
./scripts/m88ctl.ps1 serial-read -MaxBytes 256
./scripts/m88ctl.ps1 serial-close
```

## Windows COMへの接続

Windows GUIでは **Tools → Serial...** から設定する。
COM port、Baud rate、Data bits、Parity、Stop bits、Flow controlを選び、
Connectで接続、Disconnectで切断する。Refreshはポート一覧を再取得する。
Closeは設定画面だけを閉じ、接続は維持する。状態・キュー数・エラーは画面内に表示する。
設定値はアプリ実行中に保持し、起動時の自動接続は行わない。
マシンリセット時は切断する。接続中のテープ操作とスナップショット操作は拒否する。

`GET /v1/serial/ports` でポート名を列挙する。ポートは自動選択しない。
`POST /v1/serial/open?port=COM3&baud=9600&data_bits=8&parity=none&stop_bits=1&flow=none`
で、COM受信→ゲスト受信、ゲスト送信→COM送信を接続する。

```powershell
./scripts/m88ctl.ps1 serial-ports
./scripts/m88ctl.ps1 serial-open -ComPort COM3 -Baud 9600 -DataBits 8 -Parity none -StopBits 1 -Flow none
./scripts/m88ctl.ps1 run -Frames 60
./scripts/m88ctl.ps1 serial
./scripts/m88ctl.ps1 serial-close
```

- portは `COM1` などの大文字表記。任意のファイル・デバイスパスは受け付けない。
- baudは1～4000000、data_bitsは5～8。実際に使える値はドライバー依存。
- parityはnone/odd/even、stop_bitsは1/1.5/2（1.5は5ビットのみ、5ビットで2は不可）。
- flowはnone/rtscts。ソフトウェアフロー制御は行わず、00Hや11H/13Hもバイナリとして扱う。
- 省略時は9600、8N1、flowなし。通信条件は接続時に固定し、相手側と揃える。
- Windowsの非同期I/Oを使用。待機中のRead/WriteでVMを止めない。
  API呼び出し時とフレーム実行の前後にデータを移す。停止中の受信はOSのバッファに溜まる。
  継続通信にはrunを繰り返す。無制限の停止中受信は保証しない。
- COM接続中の手動read/write/clearは409。切り替えは一度closeしてからopenする。
- `com_connected`, `com_port`, `com_error` で接続先とエラーを確認できる。
  I/Oエラー・回線エラー時はCOMを閉じ、最後のエラーを残す。自動再接続はしない。
  エラー後はserial-closeで仮想側も閉じてから再接続する。
- closeとマシンresetはCOMの保留I/Oをキャンセルする。未送信データは破棄される。
  `tx_pending` はUSART側のみで、COMドライバーに渡した分の送信完了を示さない。
- DTRは接続時に有効、RTSは有効またはCTS/RTSフロー制御。
  ゲストのモデム信号・BREAK・パリティエラーをピン単位には再現しない。
  ゲストのbaudによるビット時間も再現しない、バイト転送用のブリッジ。
- Windows以外は仮想シリアルを使用できるが、COM openはエラーになる。

ディスク操作とシリアル操作は入力記録中には拒否する。
ディスク装着中やシリアル接続中はチェックポイント保存・復元・入力記録を開始できない。

## 検証

`serial_test` はデータ長、キュー上限、TXRDY、リセットをROMなしで検証する。
`scripts/test_disk_api.ps1` と `scripts/test_serial_api.ps1` は
`-RomDirectory` と `-BuildDirectory` を指定し、実ROM上のAPIを検証する。
シリアルのAPIテストではゲストのZ80エコーループで全256バイトを往復させる。
COMポート一覧と不正入力も検証するが、物理ケーブルの往復テストは含まない。

実装の参照: [Microsoftの非同期シリアルI/O](https://learn.microsoft.com/en-us/windows/win32/devio/overlapped-operations)。

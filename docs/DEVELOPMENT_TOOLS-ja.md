# 開発ツール：計測・書き込み追跡・チェックポイント・メモリマップ

PC-80/88の6モードで共通です。ゲーム固有の操作シナリオや合否判定は持ちません。
状態と入力を再現し、結果の判定は各プロジェクトのテストに置く構成です。

以下はheadless起動後に実行します。別セッションには各コマンドの
`-ConnectionFile`を指定してください。APIは既存と同じloopback限定・トークン認証です。

## 1. シンボル付きサイクル計測

```powershell
.\scripts\m88dev.ps1 symbols -Path build\program.sym
.\scripts\m88dev.ps1 configure -Profile $true
.\scripts\m88dev.ps1 region -Name draw -Begin DRAW_BEGIN -End DRAW_END
.\scripts\m88ctl.ps1 run -Frames 120
.\scripts\m88dev.ps1 profile -Top 30
```

シンボル形式は`NAME C000`、`NAME EQU $C000`、`NAME: EQU 0xC000`など。
アドレスは16進数、`;`以降はコメントです。名前の大小文字は区別します。
APIのアドレス引数はシンボル名、10進数、`0xC000`、`C000H`、`$C000`を受け付けます。

- `tstates`：コアで実際に進んだT-state。ホスト側の処理時間ではありません。
- `wait_tstates`：メモリアクセス・命令フェッチのウェイト。
- `idle_tstates`：コアがHALT中に消費した待機時間。
- `instructions`：通常命令の実行イベント数。`entry_hits`はラベルのアドレスを通った回数で、関数呼び出し回数の推定ではありません。
- `max_frame_tstates`：そのラベル範囲が1フレーム中に消費した最大値。進行中の部分フレームも含みます。
- 明示区間には`hits`、合計、1区間の`max_tstates`、ウェイト・待機時間が付きます。

ラベル集計は「そのアドレス以前で最も近いラベル」による区間分けです。
関数の終端やコールツリーは推測しません。バンク名も集計キーに含めますが、
シンボルファイル自体は1つの論理64 KiB空間です。重複アドレスの別バンクプログラムでは
適切なシンボルに入れ替えてください。厳密な処理区間にはbegin/endマーカーを使います。
割り込み受付、I/O同期再試行、出力命令の先読みは別項目として計上します。

計測対象はメインZ80です。サブCPU・DMA・音声生成のプロファイラではありません。
コアのタイミングを測る機能であり、実機と同じT-stateを保証するものではありません。

## 2. 最終書き込み元・ウォッチポイント・履歴

```powershell
.\scripts\m88dev.ps1 configure -Profile $true -History 256 -Writes $true
.\scripts\m88dev.ps1 watch -Address E000H -Length 16
.\scripts\m88ctl.ps1 run -Frames 120
.\scripts\m88dev.ps1 trace -Last 32
.\scripts\m88dev.ps1 writer -Address E000H
.\scripts\m88dev.ps1 watch-clear
.\scripts\m88dev.ps1 resume
.\scripts\m88ctl.ps1 run -Frames 1
```

ウォッチは該当命令の書き込みを完了してから停止します。PUSHや16 bitストアなら
2バイトとも書き終えた状態です。次の命令は実行しません。
`status.debug.hit`にPC・SP・IFF1・書き込み値・物理バンク・オフセットが入り、
履歴には命令前後の主レジスタ、先頭4バイト、サイクル、書き込み一覧が入ります。
`kind`で`instruction` / `irq` / `nmi` / `lookahead` / `sync-retry` / `halt-idle`を区別します。
先頭4バイトは逆アセンブル結果ではなく参照用バイト列です。

`space=cpu`（既定）はCPU論理アドレスです。物理バンク指定ではその領域内の
オフセットを使います。例：`watch -Space gvram-g -Address 0 -Length 2`。
対応する物理領域は`ram`、`tvram`、`gvram-b/r/g`、`gvram-alu`です。
ROM裏RAMとGVRAM切替を区別して最終書き込み元を保持します。

書き込み追跡はCPUが発行したストアが対象です。DMA・APIのBINロードは対象外。
ALU窓は`gvram-alu`として追跡し、ALU演算後の各プレーン値の最終書き込み履歴を
合成するものではありません。履歴参照のために副作用のあるALU読み出しは行いません。

履歴は最大16,384件、ウォッチは64範囲。ウォッチ中は履歴指定が0でも直近32件を保持。
最終書き込み表は最大262,144エントリで、上限後も登録済みアドレスは更新します。
ウォッチ命中中の`run`は進みません。`resume`、設定変更、ウォッチ解除、`clear`で解除されます。

`configure`は全設定を置き換えます。省略項目はfalse/0です。
`clear`は測定値・履歴・最終書き込み表を消去しますが、シンボル・ウォッチ・区間定義は保持します。
BINロードや状態ロードでも測定値はクリアされます。

## 3. 状態保存と入力記録・再生

```powershell
.\scripts\m88dev.ps1 state-save -Path build\start.m88vstate
.\scripts\m88ctl.ps1 run -Frames 60
.\scripts\m88dev.ps1 state-load -Path build\start.m88vstate

.\scripts\m88dev.ps1 watch-clear
.\scripts\m88dev.ps1 record-start
.\scripts\m88ctl.ps1 key -Key numpad4
.\scripts\m88ctl.ps1 run -Frames 60
.\scripts\m88ctl.ps1 release
.\scripts\m88ctl.ps1 run -Frames 30
.\scripts\m88dev.ps1 record-stop -Path build\input.m88replay
.\scripts\m88dev.ps1 replay -Path build\input.m88replay
```

リプレイファイルには記録開始時の状態と、キーマトリクス変更・実行フレーム数が入ります。
再生はその状態を復元してから開始するので、毎回reset→起動操作を組み立てる必要はありません。
ウォッチで停止した部分フレームもチェックポイントに保存できます。

状態にはCPU・主RAM/VRAM・デバイス状態・スケジューラ・メモリウェイト・CPUの
フェッチキャッシュ・キーマトリクス・フレーム位置を含めます。headlessのカレンダーは
2000-01-01 00:00 UTCを基準とするエミュレーション時間で進み、HTTP要求間の実時間には依存しません。
カレンダー表示はホストのタイムゾーンを使用します。

制限と安全性：

- 同じM88Vビルド/ABI、ROMセット、機種・CPU/音源等の設定、フロントエンドで使用します。状態の異版互換性は保証しません。
- ROM識別・設定・長さ・CRCを検査してからロードします。壊れたファイル・構成違いを拒否します。
- 状態は自分で生成したものを使用してください。CRCは誤破損の検出であり、署名や不正ファイルの安全性保証ではありません。
- 開いたテープ・マウント中のディスクがある状態は対象外です。外部メディアの書き換えを巻き戻す機能ではありません。
- 記録は最大50,000イベント／100,000フレーム。記録中のreset、BINロード、メディア変更、状態ロード、ウォッチ追加は拒否します。
- APIの出力は既存ファイルを上書きしません。別の名前で保存します。失敗時も記録は保持されるため、保存先を変えて再試行できます。
- 状態・リプレイにはプログラム、メモリ内容、フォント由来データ等が含まれます。公開・コミットしないでください。既定の拡張子はgitignore済みです。

GUIのState Save / Loadも、ディスク・テープを開いていないBIN開発セッションでは
同じ`src/development/snapshot`を使用します。GUIのスロット保存は従来どおり上書き可能です。
従来のメディア付きスロットは旧M88形式のまま保持し、旧形式のロードにも対応します。
GUIとheadlessでは入力/UI/音声構成が違うため、保存ファイルの相互交換は行いません。
GUIはホストの時計を使い、音声出力キュー・FM波形の位相は保存しません。
サンプル単位の音声再現は保証せず、入力の記録・再生操作はheadless APIが対象です。

## 4. CPUから見たメモリマップ

```powershell
.\scripts\m88dev.ps1 map
```

実際のCPUページテーブルと現在のI/O設定を調べ、読み出し元と書き込み先を別々に返します。
ROM裏RAM、テキスト窓、GVRAMプレーン/ALU、拡張RAMの物理オフセット、
各領域のウェイト、SP・IFF1/IFF2、関連ポートのラッチ値を確認できます。
`wait`は現在のメモリ待ちテーブル値です。フェッチキャッシュに残る待ち値まで表示するものではありません。

## API一覧

| Method | Path | 主な引数 |
| --- | --- | --- |
| GET / POST | `/v1/symbols` | POST: `path` |
| POST | `/v1/debug/config` | `profile`, `history`, `writes` |
| POST | `/v1/debug/clear`, `/v1/debug/resume` | なし |
| GET | `/v1/profile` | `top`（最大1000） |
| POST | `/v1/profile/region` | `name`, `begin`, `end` |
| POST | `/v1/debug/watch` | `address`, `length`, `space` |
| POST | `/v1/debug/watch/clear` | なし |
| GET | `/v1/debug/trace` | `last` |
| GET | `/v1/debug/writer` | `address`, `space` |
| GET | `/v1/map` | なし |
| POST | `/v1/state/save`, `/v1/state/load` | `path` |
| POST | `/v1/replay/record/start` | なし |
| POST | `/v1/replay/record/stop`, `/v1/replay/play` | `path` |

参照はGET、変更はPOSTです。`status`には`debug`と`recording`が追加されています。
計測・履歴・書き込み追跡・ウォッチをすべて無効にすると、命令ごとの記録処理は外れます。

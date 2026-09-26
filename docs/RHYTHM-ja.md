# YM2608リズム音源

M88Vは `ym2608_adpcm_rom.bin`（8,192バイト）をROMフォルダから読み、ymfmのADPCM-Aエンジンで6種類の打楽器を再生する。FMにもymfmを使い、SSG・外部RAMのADPCM-Bには従来のfmgenを使う。リズムとADPCM-Bが発音するのはOPNA設定の音源だけである。[FM音源の切り替え](FM-ja.md)も参照。

ROMがない、読み込めない、サイズが違う場合は従来の `2608_BD.WAV`、`2608_SD.WAV`、`2608_TOP.WAV`、`2608_HH.WAV`、`2608_TOM.WAV`、`2608_RIM.WAV`（旧名RYMも可）へフォールバックする。両方あるとROMを優先する。GUIのROMフォルダ設定とヘッドレスの `--rom-dir` のどちらでも利用できる。ヘッドレスの一時ROM配置と識別値にもリズム素材を含める。

標準ROMの照合値：CRC32 `23c9e0d8`、SHA1 `50b6c3e288eaa12ad275d4f323267bb72b0445df`。実装は8KBであれば読み込めるため、長さだけで正しい音色を保証するわけではない。ROMは実行ファイルやリポジトリに同梱しない。

レジスタ10H・11H・18H〜1DHによる発音、停止、全体／楽器音量、左右定位を反映する。通常分周では外部OPNAクロックの1/432でBD・SD・TOP・HH、1/864でTOM・RIMを復号する。分周変更にも追従し、ミュート中もデコーダは進む。出力レートへの変換は時間加重平均を使う。アナログ出力回路までの完全再現ではない。

精密チェックポイントは現在形式5を使う。ROM方式では予測値・読出位置・クロック位相、WAV方式では再生位置を保存し、FMの内部状態も保存する。ROM/WAVの識別値も照合する。形式1〜4の精密チェックポイントと、それらを含むリプレイは再作成する。旧形式のM88状態ファイルの形式は変更せず、従来どおりレジスタ再設定による復元であり、発音途中の完全な継続は保証しない。

## 検証

`opna_rhythm_test` は合成ROMの既知ADPCMベクトル、6音の再生速度、再発音、左右定位、音量・チャンネルミュート、停止、8/44.1/48kHz、分割Mixと状態復元の出力一致、ROM優先と不正サイズ時のWAVフォールバックを検査する。`adpcm_state_test` は一時ROM配置を通したコアでリズムを鳴らし、精密チェックポイント復元後のPCM一致も確認する。

`opna_rhythm_test <ROMフォルダ> <出力.wav>` で外部ROMによるBD→SD→TOP→HH→TOM→RIMの6秒デモを生成できる。自動検査のROMは合成データであり、外部ROMを必要としない。

## 参考

- [ymfm](https://github.com/aaronsgiles/ymfm/tree/81aec25ccbb98f4873a255f7551ac4dadac59b4a)：ADPCM-A本体、YM2608アドレス・クロック。
- [MAME YM2608](https://github.com/mamedev/mame/blob/master/src/devices/sound/ymopn.cpp)：標準ROMのサイズ・ハッシュ。
- [QUASI88作者の説明](https://www.eonet.ne.jp/~showtime/quasi88/memo/sound.html)：xmame方式ではWAV不要、fmgen方式ではWAVを使用する経緯。

# M88V初回分離時の検証

2026-09-05、Windows x64 / MSVCで実施。

- headlessビルド成功、CTest 3件成功（API自己テスト、Z80 wait、共通プロファイル／ROM別名）。
- Windows GUIビルド成功。今回の6モードの実行検証はheadless版で行い、GUI各モードの対話操作・音声の実測は未実施。
- `N802` / `N80V2` / `N` / `N88V1` / `N88V1H` / `N88V2`の統合テスト成功。
- PC-80用ROMを含めないPC-88専用セットでも、結合`PC88.ROM`形式・分割ROM形式の両方でPC-88の4モード成功。

## 統合テストの内容

`scripts/test_development_modes.ps1`がモードごとに別プロセスを起動します。
クロックは開発用設定の4 MHzです。

1. 機種・モード表示、起動時フレーム数、認証、health。
2. BASIC起動、キーボード経由のPRINT/POKE実行、RAMの結果とPNG取得。
3. Z80の小さな自作BINを直接ロードして実行し、キー入力・レジスタ・RAMを検査。
4. CPU側のバンク切替と物理GVRAM参照の整合を確認。
   N802は単一packedバンク、N80V2とPC-88はB/R/Gの3プレーン。
5. run=0で進まないこと、不正なフレーム数の拒否。
6. M88DMP1全118,832バイトの形式と、RAM/TVRAM/B/R/G領域がAPI参照値と一致すること。
7. 自作T88のオープン、リセット時のプロファイル維持、正常終了。

画面取得結果はBASICの表示内容も目視確認しています。
T88についてはオープンまでの試験であり、各モードでのMON/CLOADによる全転送経路を
検証したものではありません。市販ソフト全般、ディスク・音声・CD-ROM、VAモード、
実機の厳密なタイミング互換性を保証する試験でもありません。

## 実行方法

```powershell
.\scripts\build_headless.ps1
.\scripts\test_development_modes.ps1 -RomDirectory D:\path\to\roms
```

結果CSV・PNG・ダンプ・接続ファイルは既定で`build/mode-tests/`へ保存します。
接続ファイルには認証トークンが含まれます。ROM・接続ファイル・ダンプを公開しないでください。

BASIC文字入力は各押下・解放を既定6フレーム保持します。N88 V1で3フレームだと
文字の取りこぼしがあったためです。必要なら`type_nbasic.ps1 -KeyFrames N`で調整します。

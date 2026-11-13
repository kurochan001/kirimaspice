# 既知の問題

[English](KNOWN_ISSUES.md) · この文書は英語版を正とします。日本語版はその訳で、両者が食い違う場合は英語版に従います。

この一覧には、測ったこと、またはコードで読んだことだけを載せています。特に書いていない限り、
測定は 2026-11-14 に、macOS 27.2（arm64）、GCC 16.2.0、CMake 4.4.4、SuiteSparse 7.6.1 で行いました。

## テスト

- **7 件のテストが失敗します。** `tests/known_failures.txt` に並べてあります。
  - `ManufacturerModels.*`（5 件）と `BJTModelTest.SimpleCommonEmitter` は、リポジトリにない
    `tests/models/2N3904.lib` と `tests/models/1N4148.lib` を読み込みます。
  - `CMOS_Inverter.DCTransferCharacteristic_HighInput` は、出力電圧が 0 V 以上であることを
    確かめ、-4.1e-25 V を得ます。
- **比較のテスト 20 件が、常にスキップされます。** テストは参照データを `tests/validation/` の下で
  探しますが（例：`tests/validation/rc_lowpass_ac_ngspice.csv`）、ファイルは `validation/` にあります。
  テストが探す場所にファイルを置くと、20 件のうち 19 件が合格し、
  `MOSFET2StageAmplifierValidation.MatchesNgspice` が失敗します。10 Hz 付近で位相が参照と最大
  11.7 度ずれ、許容は 5 度です。どちらが正しいかは未確認です。
- `validation/` と `tests/validation/` の参照データを作った ngspice の版は記録されていません。
- `BSIM4TemperatureValidationTest.TemperatureSweepValidation` はスキップされます。
  `scripts/` のスクリプトが書き出す `/tmp/phase_b7_temperature_refs.json` を読むためです。
- `BSIM4ComprehensiveTest.NgspiceComparison` は `ngspice` を実行し、ngspice が入っていなければ
  スキップされます。
- テストの一部は、ビルドディレクトリからの相対位置でデータを探すので、ビルドディレクトリが
  ソースの直下の `build/` のときだけ合格します。

## デバイスモデル

- **BSIM4 のノイズは、別の簡略化した電流のモデルを使います。** 動作点、DC、過渡、AC の解析は
  `BSIM4Core`（`src/devices/bsim4_core.cpp`）を通して BSIM4 を評価します。ノイズ源
  （`MOSFET_BSIM4::get_noise_sources`）は、代わりに `src/devices/mosfet_bsim4.cpp` の
  `compute_drain_current` と `compute_threshold_voltage` を使い、gm を数値微分で求めます。
- **BSIM4 の UB の既定値は見直し中です。** 値は 2.5e-21 (m/V)² で、横のコメントは、0.18 µm の
  プロセスのモデルカードに合わせて 1.0e-19 から変えたと書いています。BSIM4 マニュアルの
  既定値と合っているかは未確認です。
- **JFET はネットリストから使えません。** ライブラリには JFET のモデル
  （`include/kirimaspice/devices/jfet.hpp`）がありますが、パーサーに `J` 素子がありません。

## ソルバー

- 動く線形ソルバーは KLU だけです。SuperLU_DIST のバックエンドは仮置きで、指定しても KLU に
  切り替わります。

## コマンドラインと出力

- `--version` オプションはありません。
- `-o <パス>` を指定すると、AC 解析の CSV のファイル名は、パス全体の最後の点から後ろを除いて
  作られます。そのため、ディレクトリ名に点があると、ファイルが親のディレクトリに書かれます。
- SSF の出力形式の仕様は、このリポジトリにはまだ公開していません。

## プラットフォーム

- ビルドとテストは、macOS（arm64）と GCC 16.2.0 でしか行っていません。対象とする Ubuntu 24.04 と
  その標準のコンパイラでの動作は未確認です。
- Apple clang 21 では、固定している {fmt} 9.1.0 と spdlog 1.13.0 がコンパイルできません
  （spdlog の中の `consteval` のエラー）。
- Windows は試していません。

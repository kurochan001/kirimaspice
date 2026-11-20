# NETLIST_CONVENTIONS：kirimaspice が ngspice に合わせるところ

[English](NETLIST_CONVENTIONS.md) · この文書は英語版を正とします。日本語版はその訳で、両者が食い違う場合は英語版に従います。

状態：**承認（2026-11-14）**。

kirimaspice は SPICE シミュレータ向けに書かれたネットリストを読みますが、SPICE シミュレータの間では
いくつかの決まりが違います。下の点では kirimaspice は ngspice に合わせ、ngspice 向けに書かれた
ネットリストが kirimaspice でも同じ意味になるようにします。それぞれの点に、決まり、ngspice がそれを
書いている ngspice User's Manual の節（版 47+ の開発版の HTML 版を 2026-11-14 に読みました）、同じ日に
ngspice-47（Homebrew、macOS arm64）で小さなネットリストを動かした結果を書きます。マニュアルは引用せず、
要約しています。

## 1. 倍率の接尾辞

- **決まり：** 数の後ろには、大文字でも小文字でも、次の倍率を付けられます。`T`（10¹²）、`G`（10⁹）、
  `MEG`（10⁶）、`K`（10³）、`MIL`（25.4×10⁻⁶）、`M`（10⁻³）、`U`（10⁻⁶）、`N`（10⁻⁹）、`P`（10⁻¹²）、
  `F`（10⁻¹⁵）、`A`（10⁻¹⁸）。数の後ろの倍率でない文字と、倍率の後ろの文字は無視します。そのため、
  `M` と `m` はどちらもミリ、`10MHz` は 10×10⁻³、`1A` は 10⁻¹⁸ です。
- **ngspice：** 2.1.3.2 節（表 2.1）と 2.1.3.3 節。`1M` の抵抗は 1 mΩ、`1MEG` は 1 MΩ でした。
- **これまで：** 大文字の `M` と、`H`、`O`、`V`、`W`、`S`、`F` が続く `M` は 10⁶ でした。`MIL` と `A` は
  倍率ではありませんでした。

## 2. AC 解析の入力

- **決まり：** 独立電源は、その行に `AC` があるときだけ AC 解析の入力になります。振幅のない `AC` は
  振幅 1 で、位相の既定値は 0 です。`AC` のない電源は、AC 解析では振幅 0 です。
- **ngspice：** 4.1 節。`AC` を付けずに `DC 5` と書いた電圧源は AC 解析で 0 V、振幅を付けずに
  `DC 0 AC` と書いた電圧源は 1 V でした。
- **これまで：** `AC` のない電圧源は振幅 1 でした。電流源はもともと振幅 0 でした。

## 3. 電圧で定まる枝の電流

- **決まり：** 電圧源と、`E`、`H`、電圧の形の `B` 電源の電流は、`+` 端子から入って電源の中を通り、
  `−` 端子から出ていく向きを正とします。`I(<電源>)` として表示し書き出すのは、この電流です。`F` と `H`
  が制御に使うのも、この電流です。そのため、回路に電流を供給している電源の電流は負になります。電流源は
  変えません。ngspice と同じく、正の値は `n+` から電源の中を通って `n−` へ電流を流します。
- **ngspice：** 4.1 節。1 kΩ に `V1 in 0 DC 1` をかけると `i(V1)` = −1 mA で、1 kΩ につないだ
  `F1 0 f V1 3` は `v(f)` = −3 V、`H1 h 0 V1 100` は `v(h)` = −0.1 V でした。1 kΩ につないだ
  `E1 e 0 in 0 2` の `i(E1)` は −2 mA、1 kΩ に 1 V をかける `H` 電源の `i(H1)` は −1 mA、1 kΩ に
  つないだ `B1 b 0 V=2*V(in)` の `i(B1)` は −2 mA でした。
- **これまで：** 逆の符号でした。そのため `F` と `H` も逆の結果になっていました。

## 4. 入れ子の DC スイープ

後で使う例 `.dc V1 0 1 0.5 V2 0 1 1` の点の順序です。

| 点 | V2、外側のループ（V） | V1、内側のループ（V） |
| --- | --- | --- |
| 1 | 0 | 0 |
| 2 | 0 | 0.5 |
| 3 | 0 | 1 |
| 4 | 1 | 0 |
| 5 | 1 | 0.5 |
| 6 | 1 | 1 |

- **決まり：** `.DC src1 start1 stop1 step1 src2 start2 stop2 step2` では、`src2` の値ごとに `src1` を
  その範囲で掃引します。`src1` が内側のループです。点はこの順に計算し、書き出します。
- **ngspice：** 11.3.2 節。`.dc V1 0 1 0.5 V2 0 1 1` は、`V2` = 0 で `V1` = 0、0.5、1、続いて `V2` = 1 で
  同じ並びになりました。
- **これまで：** `src2` が内側のループでした。

## 5. グラウンド

- **決まり：** ノード `0` がグラウンドです。大文字小文字を問わず `gnd` という名前のノードも、
  サブサーキットの中と同じく、トップレベルでもグラウンドです。
- **ngspice：** 2.1.3.5 節（ngspice は `gnd` をグローバルなノードとして扱い、`0` に置き換えます。
  それを止める `no_auto_gnd` の切り替えは、ここでは用意しません）。`V1 a gnd DC 1` と `R1 a GND 1k` で
  `v(a)` = 1 V でした。
- **これまで：** `GND` はサブサーキットの中でだけグラウンドでした。

## ここでは決めないこと

- ngspice はバッチモードではノード名の大文字と小文字を区別しません（2.1.3.4 節）。kirimaspice も
  `8364a9c` から区別せず（2026-11-21 にこの文書の外で決定）、小文字で書き出します。
- ngspice は DC の値が 0 なら省けるので、`V1 in 0 PULSE(...)` を受け付けます（4.1 節）。kirimaspice も
  `aef9135` から受け付け、ngspice-47 と同じく、動作点には波形の時刻 0 の値を使います（`PULSE(0.3 2 ...)` で
  0.3 V。2026-11-21 に実行）。
- ngspice は `.DC` で抵抗や温度も掃引できます（11.3.2 節）。kirimaspice が掃引できるのは独立電源だけです。

## 期待値が変わるテスト

これらの決まりに従うと、既存のテストの期待値が変わります。その変更は実装とは別のコミットにし、
`Approved-by` を付けます。

- 決まり 1：`UnitsParse.MixedCaseAndFollowedUnits`（`10MHz`）
- 決まり 3：`DCSolver.MultipleVoltageSources`、`DCSolver.NODESETInitialGuess`、
  `DCSolver.SingleResistor`、`DCSolver.VoltageDivider`、`DCSweepTest.BranchCurrentExtraction`、
  `LargeScale100.DC_ResistorMesh_100Devices`、`LargeScale300.DC_ResistorMesh_300Devices`、
  `LargeScale1000.DC_ResistorMesh_1000Devices`、`LargeScale3000.DC_ResistorMesh_3000Devices`、
  `TransferFunctionSolver.InputResistanceCheck`、`TransientSolver.DCCircuitSteadyState`、
  `cli_op_gnd_is_a_node`（電圧源の符号だけを変えて測ったもので、`E`、`H`、`B` でさらに増えることが
  あります）
- 決まり 4：`DCSweepTest.TwoDimensionalSweep_BasicStructure`
- 決まり 5：`cli_op_gnd_is_a_node`
- 決まり 2：なし

`cli_op_gnd_is_a_node` は、期待値を変えたときに `cli_op_gnd_is_ground` へ名前を変えました（コミット
`e147d5c`）。

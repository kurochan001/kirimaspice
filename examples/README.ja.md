# 例題

[English](README.md) · [プロジェクトの README](../README.ja.md) · この文書は英語版を正とします。日本語版はその訳で、両者が食い違う場合は英語版に従います。

機能を 1 つずつ見せる小さなネットリストです。どれにも解析解があり、CTest がそれぞれを実行して解析解と
比べます（`tests/scripts/test_examples.py`、テスト `example_*`）。下に書いた数値は、テストを走らせる
たびに確かめられます。[ビルド](../docs/USER_MANUAL.ja.md#ビルド)してから、ソースの最上位で実行します。

| ファイル | 見せるもの | 解析 |
| --- | --- | --- |
| [test_simple_divider.sp](test_simple_divider.sp) | [ユーザーマニュアル](../docs/USER_MANUAL.ja.md)の最初の実行 | 動作点 |
| [params_subckt.sp](params_subckt.sp) | `.PARAM`、`.FUNC`、パラメータ付きのサブ回路 | 動作点 |
| [diode_iv.sp](diode_iv.sp) | `.MODEL`、DC スイープ、電源の電流の符号 | DC スイープ |
| [rc_step.sp](rc_step.sp) | `PULSE` 電源と raw 出力 | 過渡解析 |
| [rc_discharge.sp](rc_discharge.sp) | `UIC` 付きの `.IC` と、積分法 | 過渡解析 |
| [rc_lowpass_ac.sp](rc_lowpass_ac.sp) | AC 解析の入力と AC の CSV | AC |

## 分圧回路

```sh
build/kirimaspice examples/test_simple_divider.sp
```

3.3 V に 1 kΩ の抵抗を 2 つつなぐので、`V(n2) = 1.65 V`、`I(VDD) = -0.00165 A` です。電圧源の電流は
`+` 端子に流れ込む向きを正とするので、電流を供給する電源は負の値になります
（[NETLIST_CONVENTIONS](../docs/spec/NETLIST_CONVENTIONS.ja.md) の規則 3）。

## パラメータ、関数、サブ回路

```sh
build/kirimaspice examples/params_subckt.sp
```

`VH = HALF(VIN) = 2 V` が 1 kΩ と 2 kΩ の分圧回路を駆動するので、`V(out) = 1.33333 V` です。`X1` は
`RTOP=3k` を与えたサブ回路 `DIV` で、3 kΩ と 1 kΩ の分圧回路なので、`V(xout) = 0.5 V` です。`V1` は
2/3 mA + 1/2 mA を供給するので、`I(V1) = -0.00116667 A` です。`X` の行に書いたパラメータは、`.SUBCKT`
の行の既定値に代わります。

## ダイオードの I–V 特性

```sh
build/kirimaspice examples/diode_iv.sp --raw-out diode_iv.raw
```

プログラムはスイープの要約しか表示しないので、値は raw ファイルにあります。27 °C で `IS=1e-14`、`N=1` の
とき、電流は I = IS·(exp(V/V<sub>T</sub>) − 1)、V<sub>T</sub> = kT/q = 25.865 mV に従い、`i(V1)` は −I
です。スイープのどの点でも、プログラムはこの式と 100 万分の 1 より良く一致します。たとえば 0.6 V で
−1.18719×10⁻⁴ A、0.8 V で −0.270827 A で、後者は ngspice-47 でも同じです。約 0.73 V より上では、SPICE と
同じく、Newton 法の 1 回ごとに接合電圧が動く幅を制限しますが、収束した結果は式のとおりです。

## RC のステップ応答

```sh
build/kirimaspice examples/rc_step.sp --raw-out rc_step.raw
```

1 kΩ と 1 µF（RC = 1 ms）を 0 V から 1 V へのステップで駆動します。1 ms での厳密な値は
1 − e⁻¹ = 0.632121 で、既定の台形則、`tstep` = 10 µs のプログラムの値は 0.632105 です。差の大部分は、
最初のステップが後退オイラー法であることによります。

## RC の放電と積分法

```sh
build/kirimaspice examples/rc_discharge.sp --raw-out rc_discharge.raw
```

コンデンサは `.IC V(out)=1` により 1 V から始まります。`.TRAN` の行の `UIC` により、過渡解析は動作点では
なく `.IC` から始まります。`UIC` がないと、プログラムは `.IC` を無視し、コンデンサは 0 V のままです。
ngspice はどちらの場合も `.IC` を使います（[KNOWN_ISSUES](../KNOWN_ISSUES.ja.md#解析)）。

1 ms での厳密な値は e⁻¹ = 0.367879 です。`.OPTIONS` の行の `METHOD` を変えると、方法を比べられます。

| `METHOD` | 方法 | 次数 | 1 ms の V(out) |
| --- | --- | --- | --- |
| `trap`（既定） | 台形則 | 2 | 0.367895 |
| `gear` | BDF2 | 2 | 0.367913 |
| `euler` | 後退オイラー法 | 1 | 0.369711 |

## RC 低域通過フィルタ

```sh
build/kirimaspice examples/rc_lowpass_ac.sp -o rc_lowpass
```

AC 解析の結果は表示されず、`rc_lowpass_ac.csv` に書かれます。`AC 1` により `V1` が入力になります。
1 kHz で応答 1/(1 + j2πfRC) は −16.0722 dB、−80.957° で、プログラムの値は CSV に書かれた桁まで同じです。
CSV の見出しの行の `Simulator: kirimaspice v0.27` は既知の問題で、版は 0.0.0 です。

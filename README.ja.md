# kirimaspice

[English](README.md)

[文書の案内](docs/README.ja.md) · [ユーザーマニュアル](docs/USER_MANUAL.ja.md) · [リファレンスマニュアル](docs/REFERENCE_MANUAL.ja.md) · [例題](examples/README.ja.md) · [用語集](docs/GLOSSARY.ja.md) · [FAQ](docs/FAQ.ja.md) · [既知の問題](KNOWN_ISSUES.ja.md) · [貢献の仕方](CONTRIBUTING.ja.md) · [第三者の著作物に関する表示](THIRD_PARTY_NOTICES.ja.md)

[![CI](https://github.com/kurochan001/kirimaspice/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/kurochan001/kirimaspice/actions/workflows/ci.yml?query=branch%3Amain)
[![Nightly](https://github.com/kurochan001/kirimaspice/actions/workflows/nightly.yml/badge.svg?branch=main)](https://github.com/kurochan001/kirimaspice/actions/workflows/nightly.yml?query=branch%3Amain)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-00599C?logo=cplusplus&logoColor=white)](#ビルド)
[![Linux](https://img.shields.io/badge/OS-Linux-FCC624?logo=linux&logoColor=black)](#ビルド)
[![macOS](https://img.shields.io/badge/OS-macOS-000000?logo=apple&logoColor=white)](#ビルド)
[![License: Apache-2.0 WITH LLVM-exception](https://img.shields.io/badge/license-Apache--2.0%20WITH%20LLVM--exception-blue)](LICENSE)

kirimaspice は、開発初期のオープンソースの SPICE 回路シミュレータで、SystemVerilog シミュレータ
[kirimasim](https://github.com/kurochan001/kirimasim) の姉妹プロジェクトです。SPICE のネットリストを読み、
動作点、DC スイープ、過渡、AC の解析を行い、結果を SPICE3 の raw ファイル、CSV、SSF、WAV で書き出します。
リリースはまだありません。コードの大部分は AI のツールが生成しています。仕様、テスト、レビューには、
[AI ポリシー](docs/spec/AI_POLICY.ja.md)のもとで人が責任を持ちます。

## 現状

2026-11-23 にコミット `6b334da` で `python3 tools/local_ci.py` を実行し、すべての検査に合格しました。
環境は macOS 27.2.1（arm64）、GCC 16.2.0、CMake 4.4.4、SuiteSparse 7.6.1 です。
ngspice-47 を実行して結果を照合するテストも含みます。

| macOS での CTest の結果 | 件数 |
| --- | ---: |
| 登録 | 1505 |
| 合格 | 1505 |
| 失敗 | 0 |
| スキップ | 0 |
| 無効 | 0 |

ダイオードと BJT のテスト 6 件は、プロジェクトのモデルカードを使って実行できます。
BSIM4 の 3 段インバータのテストも通常実行の対象です。ngspice-47 との波形の最大誤差は
2.51 mV で、許容値は 50 mV です。

コミット `8e748f7` では、Ubuntu 24.04 の GCC・Clang によるビルドとテスト、
リポジトリの履歴検査に合格しました。[この版の CI 結果](https://github.com/kurochan001/kirimaspice/actions/runs/45192220562) · [既知の問題](KNOWN_ISSUES.ja.md)

## 扱えるもの

パーサーが受け付ける素子：

| 文字 | 素子 |
| --- | --- |
| `R`、`C`、`L` | 抵抗、キャパシタ、インダクタ |
| `K` | 相互インダクタンス |
| `V`、`I` | 独立電圧源、独立電流源 |
| `E`、`F`、`G`、`H` | 制御電源（VCVS、CCCS、VCCS、CCVS） |
| `B` | ビヘイビア電源 |
| `D` | ダイオード |
| `Q` | バイポーラトランジスタ（NPN、PNP） |
| `M` | MOSFET と関連するデバイス。モデルの `LEVEL` で選ぶ（下記） |
| `Z` | MESFET（`NMF`、`PMF`） |
| `S`、`Y` | SCR、TRIAC |
| `T`、`O` | 無損失と有損失の伝送線路 |
| `X` | サブサーキットのインスタンス |

`M` では、`.model` の `LEVEL` でモデルを選びます。1（既定）、3、8 または 49（BSIM3）、
14 または 54（BSIM4）、10（SiC MOSFET）、11（GaN HEMT）、12（FinFET）、20（VDMOS）、21（IGBT）です。
ライブラリには JFET のモデルもありますが、パーサーにはまだ `J` 素子がありません。どこまで
実装されているかはモデルごとに違い、それぞれにテストがあります。

CLI は動作点、DC スイープ（入れ子も可）、過渡解析、AC 解析を実行します。4 種類の指定が
どれもなければ動作点を求めます。`.OP` と `.DC` を併記すると DC スイープを実行します。
`.FOUR` は過渡解析の結果から高調波を求め、`.TEMP` で温度を複数指定すると各温度で解析を繰り返します。

`.MODEL`、`.PARAM`、`.SUBCKT` などは回路を定義します。一方、`.STEP`、`.TF`、`.MC` などは
パーサーが受け付けても CLI は警告を出し、その解析を実行しません。ノイズ解析は C++ ライブラリから
利用できます。各ディレクティブの扱いは[リファレンスマニュアル](docs/REFERENCE_MANUAL.ja.md#ディレクティブ)、
ライブラリで利用できる解析は[設計](docs/ARCHITECTURE.ja.md#ライブラリにあるほかの解析)にあります。

電圧源と電流源には DC 値、AC 入力のほか、`PULSE`、`SIN`、`EXP`、`PWL`、`SFFM` の
波形を指定できます。線形ソルバーは SuiteSparse の KLU です。

## ビルド

CMake 3.25 以上、C++23 のコンパイラ、Ninja、Python 3、SuiteSparse の開発用ファイル（KLU 2.3.2 以上。
SuiteSparse 7.6.1 に含まれる版）、Git が必要です。最初の configure にはネットワーク接続が必要です。CMake は
Boost のリリースアーカイブを取得してチェックサムを照合し、残りの依存は Git で指定のコミットを取得します。

Ubuntu 24.04 の場合（CI が入れるパッケージと、CI のランナーには最初から入っている Git です）：

```sh
sudo apt install build-essential cmake ninja-build python3 libsuitesparse-dev git
```

そのうえで、ソースのディレクトリで次を実行します。

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

ビルドディレクトリはどこに置いてもかまいません。`tools/local_ci.py` は `build/` を使います。
SuiteSparse がほかの場所にある場合は、最初のコマンドに `-DCMAKE_PREFIX_PATH=<その場所>` を加えます。

## 実行

ビルドしたら、リポジトリの最上位で分圧回路の例を実行します。

```sh
build/kirimaspice examples/test_simple_divider.sp
```

1 kΩ の抵抗 2 本で 3.3 V を分圧するので、標準出力に `V(n2) = 1.65 V` と表示されます。
ほかの解析も `build/kirimaspice` に次の引数を渡して試せます。

| 調べたいこと | 同梱の例題と引数 | 結果の読み方 |
| --- | --- | --- |
| 動作点 `.OP` | `examples/test_simple_divider.sp` | 標準出力にノード電圧と電源電流が出ます。 |
| ダイオードの I–V 特性 `.DC` | `examples/diode_iv.sp --raw-out diode.raw` | 標準出力はスイープの要約です。各電圧での電源電流は `diode.raw` の `i(v1)` で読めます。 |
| RC の充電波形 `.TRAN` | `examples/rc_step.sp --raw-out step.raw` | `step.raw` の `time` と `v(out)` が時刻と出力電圧です。 |
| RC の周波数応答 `.AC` | `examples/rc_lowpass_ac.sp -o lowpass` | カレントディレクトリの `lowpass_ac.csv` に振幅（dB）と位相（度）が出ます。 |

バイナリ raw、SSF、WAV の保存方法は[ユーザーマニュアル](docs/USER_MANUAL.ja.md#結果を保存する)にあります。
すべてのオプションは `build/kirimaspice --help` で確認できます。

## 文書

[文書の案内](docs/README.ja.md)で、したいことから文書を選べます。

- [ユーザーマニュアル](docs/USER_MANUAL.ja.md)：ビルド、ネットリストの書き方、実行、結果の読み方、Python のツール
- [リファレンスマニュアル](docs/REFERENCE_MANUAL.ja.md)：オプション、素子、ディレクティブ、出力形式
- [回路方程式の解き方](docs/SOLVER_ALGORITHMS.ja.md)（MNA、スタンプ、ニュートン法、時間積分、誤差推定をRC回路で追う）
- [例題](examples/README.ja.md)：解析解のある小さなネットリスト。CTest が確かめる
- [ngspice からの移行](docs/PORTING.ja.md)：同じ点と、ネットリストの確認項目
- [用語集](docs/GLOSSARY.ja.md)：文書で使う用語
- [FAQ](docs/FAQ.ja.md)：ネットリストの実行から貢献まで、よくある質問
- [KNOWN_ISSUES.md](KNOWN_ISSUES.ja.md)：分かっている問題
- [設計](docs/ARCHITECTURE.ja.md)：ライブラリの部品と、ネットリストがそれらを通る道筋
- [開発](docs/DEVELOPMENT.ja.md)：開発用のビルド、テスト、push の前の確認、CI
- [CONTRIBUTING.md](CONTRIBUTING.ja.md)：受け付けるものと、コードの変え方
- [CLAUDE.md](CLAUDE.ja.md) と [AGENTS.md](AGENTS.ja.md)：コーディングエージェント（と人）の規則
- [SECURITY.md](SECURITY.ja.md)：脆弱性の報告
- [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.ja.md)：Contributor Covenant 2.1
- [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.ja.md)：依存とそのライセンス

## ライセンス

kirimaspice のライセンスは Apache License 2.0 with LLVM Exceptions（`Apache-2.0 WITH LLVM-exception`。
[LICENSE](LICENSE) を参照）です。BSIM4 4.8.3 のモデルのコードに基づく 6 つのファイルは、ヘッダに
書いてあるとおり ECL-2.0 にも従います。システムから動的にリンクする KLU と BTF は LGPL-2.1-or-later です。
すべての依存は [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.ja.md) にあります。

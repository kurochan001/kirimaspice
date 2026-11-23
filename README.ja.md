# kirimaspice

[English](README.md)

[文書の案内](docs/README.ja.md) · [ユーザーマニュアル](docs/USER_MANUAL.ja.md) · [リファレンスマニュアル](docs/REFERENCE_MANUAL.ja.md) · [例題](examples/README.ja.md) · [用語集](docs/GLOSSARY.ja.md) · [FAQ](docs/FAQ.ja.md) · [既知の問題](KNOWN_ISSUES.ja.md) · [貢献の仕方](CONTRIBUTING.ja.md) · [第三者の著作物に関する表示](THIRD_PARTY_NOTICES.ja.md)

[![CI](https://github.com/kurochan001/kirimaspice/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/kurochan001/kirimaspice/actions/workflows/ci.yml?query=branch%3Amain)
[![Nightly](https://github.com/kurochan001/kirimaspice/actions/workflows/nightly.yml/badge.svg?branch=main)](https://github.com/kurochan001/kirimaspice/actions/workflows/nightly.yml?query=branch%3Amain)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-00599C?logo=cplusplus&logoColor=white)](#ビルド)
[![Linux](https://img.shields.io/badge/OS-Linux-FCC624?logo=linux&logoColor=black)](#ビルド)
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

GitHub Actions では Ubuntu 24.04 の GCC と clang でテストします。この版の履歴検査では、
過去のコミットが指摘されています。[この版の CI 結果](https://github.com/kurochan001/kirimaspice/actions/runs/45160936314) · [既知の問題](KNOWN_ISSUES.ja.md)

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

パーサーが受け付けるディレクティブ：`.OP`、`.AC`、`.DC`、`.TRAN`、`.OPTIONS`、`.IC`、`.NODESET`、
`.PRINT`、`.SAVE`、`.GLOBAL`、`.PARAM`、`.FUNC`、`.MODEL`、`.SUBCKT`/`.ENDS`、`.INCLUDE`、
`.LIB`/`.ENDL`、`.IF`/`.ELIF`/`.ELSE`/`.ENDIF`、`.CONTROL`/`.ENDC`、`.STEP`、`.TEMP`、`.SENS`、`.MC`、
`.TF`、`.FOUR`、`.PZ`、`.DISTO`、`.PORT`、`.WCASE`、`.OPTIMIZE`。このうちプログラムが実行するのは
`.OP`、`.DC`、`.TRAN`（`.FOUR` を含む）、`.AC` で、1 つもなければ動作点を実行します。読んでも実行しない
ものは、警告で名指しします（[リファレンスマニュアル](docs/REFERENCE_MANUAL.ja.md#プログラムが解析を選ぶ仕組み)）。

プログラム `kirimaspice` が実行するのは、動作点、DC スイープ（入れ子を含む）、`.FOUR` のフーリエ解析を
含む過渡解析、AC 解析で、どれも `.TEMP` に並べた各温度でも実行します。電源は、DC の値、AC の入力と、
`PULSE`、`SIN`、`EXP`、`PWL`、`SFFM` のいずれかの波形を取ります。ライブラリには、ノイズ、伝達関数、
極零点、ひずみ、感度、S パラメータ、モンテカルロ、ワーストケース、最適化の解析も実装されていて
テストで使われていますが、プログラムからはまだ実行しません。

線形ソルバーは SuiteSparse の KLU です。

## ビルド

CMake 3.25 以上、C++23 のコンパイラ、Ninja、Python 3、SuiteSparse の開発用ファイル（KLU 2.3.2 以上。
SuiteSparse 7.6.1 に含まれる版）、Git が必要です。そのほかの依存は、configure のときに CMake が Git で
固定した版を取得するので、最初の configure にはネットワークへの接続が要ります。

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

```sh
build/kirimaspice circuit.cir --raw-out circuit.raw
```

| オプション | 出力 |
| --- | --- |
| `--raw-out <ファイル>` | ASCII の SPICE3 raw ファイル（動作点、DC スイープ、過渡） |
| `--raw-binary-out <ファイル>` | バイナリの SPICE3 raw ファイル（動作点、DC スイープ、過渡） |
| `-o <ファイル>` | SSF（動作点と過渡）。AC 解析の CSV の名前もこれから決まる |
| `--wav-out <ファイル>` | 過渡解析の結果の WAV 音声 |

すべてのオプションは `build/kirimaspice --help` で見られます。AC 解析の結果は常に CSV ファイルに
書き出されます。その場所は[ユーザーマニュアル](docs/USER_MANUAL.ja.md#結果を保存する)にあります。

## 文書

[文書の案内](docs/README.ja.md)で、したいことから文書を選べます。

- [ユーザーマニュアル](docs/USER_MANUAL.ja.md)：ビルド、ネットリストの書き方、実行、結果の読み方、Python のツール
- [リファレンスマニュアル](docs/REFERENCE_MANUAL.ja.md)：オプション、素子、ディレクティブ、出力形式
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

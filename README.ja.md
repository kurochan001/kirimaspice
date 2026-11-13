# kirimaspice

[English](README.md) · この文書は英語版を正とします。日本語版はその訳で、両者が食い違う場合は英語版に従います。

kirimaspice は、C++20 で書かれたオープンソースの SPICE 回路シミュレータです。SPICE のネットリストを
読み、動作点、DC スイープ、過渡、AC の解析を行い、結果を SPICE3 の raw ファイル、CSV、SSF で
書き出します。SystemVerilog シミュレータ [kirimasim](https://github.com/kurochan001/kirimasim) の
姉妹プロジェクトです。

kirimaspice は開発の初期段階にあり、リリースはまだありません。コードの大部分は AI のツールが
生成しており、仕様、テスト、レビューの責任は人が持ちます（[docs/spec/AI_POLICY.md](docs/spec/AI_POLICY.ja.md)）。

## 現状

2026-11-14 に、macOS 27.2（arm64）、GCC 16.2.0、CMake 4.4.4、SuiteSparse 7.6.1 で測った結果です。

| CTest のテスト | 件数 |
| --- | --- |
| 登録 | 1072 |
| 合格 | 1041 |
| スキップ | 23 |
| 失敗（`tests/known_failures.txt` に記載） | 7 |
| 無効 | 1 |

対象とするプラットフォームは Ubuntu 24.04 ですが、そこでのビルドとテストはまだ確かめていません。
失敗するテスト、スキップされるテスト、そのほかの分かっている問題は
[KNOWN_ISSUES.md](KNOWN_ISSUES.ja.md) にあります。

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

`M` では、`.model` の `LEVEL` でモデルを選びます。1（既定）、3、9（BSIM1）、8 または 49（BSIM3）、
14 または 54（BSIM4）、10（SiC MOSFET）、11（GaN HEMT）、12（FinFET）、20（VDMOS）、21（IGBT）です。
ライブラリには JFET のモデルもありますが、パーサーにはまだ `J` 素子がありません。どこまで
実装されているかはモデルごとに違い、それぞれにテストがあります。

パーサーが受け付けるディレクティブ：`.AC`、`.DC`、`.TRAN`、`.OPTIONS`、`.IC`、`.NODESET`、
`.PRINT`、`.SAVE`、`.GLOBAL`、`.PARAM`、`.FUNC`、`.MODEL`、`.SUBCKT`/`.ENDS`、`.INCLUDE`、
`.LIB`/`.ENDL`、`.IF`/`.ELSE`/`.ENDIF`、`.STEP`、`.TEMP`、`.SENS`、`.MC`、`.TF`、`.FOUR`、`.PZ`、
`.DISTO`、`.PORT`、`.WCASE`、`.OPTIMIZE`。

プログラム `kirimaspice` が実行するのは、動作点と DC スイープ（入れ子と温度スイープを含む）、
過渡解析、AC 解析です。ライブラリには、ノイズ、伝達関数、極零点、フーリエ、ひずみ、感度、
S パラメータ、モンテカルロ、ワーストケース、最適化の解析も実装されていてテストで使われていますが、
プログラムからはまだ実行しません。

線形ソルバーは SuiteSparse の KLU です。

## ビルド

CMake 3.25 以上、C++20 のコンパイラ、Ninja、Python 3、SuiteSparse の開発用ファイル（KLU 2.3.2 以上。
SuiteSparse 7.6.1 に含まれる版）が必要です。そのほかの依存は、configure のときに CMake が固定した版で
取得するので、最初の configure にはネットワークへの接続が要ります。

Ubuntu 24.04 の場合（まだ確かめていません）：

```sh
sudo apt install build-essential cmake ninja-build python3 libsuitesparse-dev
```

そのうえで、ソースのディレクトリで次を実行します。

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

ビルドディレクトリは `build/` にしてください。テストの一部は、そこからの相対位置でデータを探します。
SuiteSparse がほかの場所にある場合は、最初のコマンドに `-DCMAKE_PREFIX_PATH=<その場所>` を加えます。

## 実行

```sh
build/kirimaspice circuit.cir --raw-out circuit.raw
```

| オプション | 出力 |
| --- | --- |
| `--raw-out <ファイル>` | ASCII の SPICE3 raw ファイル（DC と過渡） |
| `--raw-binary-out <ファイル>` | バイナリの SPICE3 raw ファイル（DC と過渡） |
| `-o <ファイル>` | `<ファイル>` から名前を付けた SSF と CSV の出力 |
| `--wav-out <ファイル>` | 過渡解析の結果の WAV 音声 |

すべてのオプションは `build/kirimaspice --help` で見られます。

## 文書

- [KNOWN_ISSUES.md](KNOWN_ISSUES.ja.md)：分かっている問題
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

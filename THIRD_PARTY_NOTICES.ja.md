# 第三者の著作物に関する表示

[English](THIRD_PARTY_NOTICES.md) · この文書は英語版を正とします。日本語版はその訳で、両者が食い違う場合は英語版に従います。

kirimaspice 自体のライセンスは Apache License 2.0 with LLVM Exceptions です
（`LICENSE` を参照）。6 つのファイルの一部は BSIM4 4.8.3 のモデルのコードに基づき、
その表示（ECL-2.0）を残しています。それ以外に、このリポジトリは第三者のソースの
写しを持ちません。下の部品は、configure のときに CMake の `FetchContent` が
表に書いた版に固定して取得するか、システムから見つけます。ライセンスと著作権表示は、
取得したソースのライセンスファイルから 2026-11-13 と 2026-11-14 に読み取りました。
取得する部品のライセンスの全文は、英語版（`THIRD_PARTY_NOTICES.md`）の末尾にあります。

ビルド済みの kirimaspice はまだ配布していません。配布するのはソースだけです。
ビルドした `kirimaspice` の実行ファイルやライブラリを配布する人は、組み込まれた部品の
条件と、KLU と BTF の LGPL の条件を満たす必要があります。

## kirimaspice に組み込まれるもの

ライブラリ `kirimaspice_lib` に組み込まれ、そこから `kirimaspice` とテストの
プログラムに入ります。

| 部品 | 版 | ライセンス | 著作権表示 | 入手先 |
| --- | --- | --- | --- | --- |
| {fmt} | 9.1.0 (`a33701196adfad74917046096bf5a2aa0ab0bb50`) | MIT（`LICENSE.rst` にある任意の例外条項つき） | Copyright (c) 2012 - present, Victor Zverovich | https://github.com/fmtlib/fmt |
| spdlog | v1.13.0 (`7c02e204c92545f869e2f04edaab1f19fe8b19fd`) | MIT | Copyright (c) 2016 Gabi Melman. | https://github.com/gabime/spdlog |
| PEGTL | 3.2.7 (`cf639f7f4ee125f68e1ccfba8d99ebc0de57b9fe`) | MIT | Copyright (c) 2007-2022 Dr. Colin Hirsch and Daniel Frey | https://github.com/taocpp/PEGTL |
| ExprTk | 2026-11-13 時点の master (`1e4a80b5ec9b4832ed59c6faa65f625a01b18ef0`) | MIT | Copyright (c) 1999-2025 Arash Partow | https://github.com/ArashPartow/exprtk |
| Boost（Graph と、それが必要とする Boost のライブラリ） | 1.86.0, `boost-1.86.0-cmake.tar.xz` (SHA-256 `2c5ec5edcdff47ff55e27ed9560b0a0b94b07bd07ed9928b476150e16b0efc57`) | BSL-1.0 | 各ファイルの冒頭 | https://github.com/boostorg/boost |
| Zstandard（SSF の書き出しで使用） | v1.5.6 (`794ea1b0afca0f020f4e57b6732332231fb23c70`) | BSD-3-Clause（BSD-3-Clause か GPL-2.0 の選択で提供され、BSD-3-Clause として使用） | Copyright (c) Meta Platforms, Inc. and affiliates. | https://github.com/facebook/zstd |
| NLopt（`NLOPT_LUKSAN=OFF` でビルド） | v2.11.0 (`88c424d4f458412787df96fcc95218acbca224fd`) | MIT。SLSQP のコードは BSD-3-Clause | Copyright (c) 2007-2024 Massachusetts Institute of Technology、および各アルゴリズムの表示にある著作者 | https://github.com/stevengj/nlopt |

## システムから動的にリンクするもの

同梱も取得もしません。CMake がシステムに入っている SuiteSparse（Ubuntu なら
`libsuitesparse-dev`）を見つけ、動的にリンクします。KLU 2.3.2 以上が必要です。
kirimaspice は KLU を公開 API を通じてだけ呼びます。版は試したもので、著作権表示は
SuiteSparse 7.6.1（各パッケージの `Doc/License.txt` と `SuiteSparse_config/README.txt`）
から読みました。入手先：https://github.com/DrTimothyAldenDavis/SuiteSparse

| 部品 | 試した版 | ライセンス | 著作権表示 |
| --- | --- | --- | --- |
| KLU (SuiteSparse) | 2.3.2 (SuiteSparse 7.6.1) | LGPL-2.1-or-later | KLU, Copyright (c) 2004-2022, University of Florida. |
| BTF (SuiteSparse) | 2.3.1 (SuiteSparse 7.6.1) | LGPL-2.1-or-later | BTF, Copyright (C) 2004-2013, University of Florida |
| AMD (SuiteSparse) | 3.3.1 (SuiteSparse 7.6.1) | BSD-3-Clause | AMD, Copyright (c), 1996-2022, Timothy A. Davis, Patrick R. Amestoy, and Iain S. Duff. |
| COLAMD (SuiteSparse) | 3.3.2 (SuiteSparse 7.6.1) | BSD-3-Clause | COLAMD, Copyright 1998-2022, Timothy A. Davis. |
| SuiteSparse_config | 7.6.1 | BSD-3-Clause | SuiteSparse_config, Copyright (c) 2012-2024, Timothy A. Davis. |

OpenMP が見つかった場合は、コンパイラの OpenMP ランタイムもリンクされます。
そのライセンスはコンパイラのランタイムライブラリのもので、ここでは確かめていません。

## 他のコードに基づく、このリポジトリのコード

| ファイル | 基にしたもの | ライセンス |
| --- | --- | --- |
| `src/devices/bsim4_core.cpp`、`src/devices/mosfet_bsim4.cpp`、`src/devices/mosfet_bsim4_params.cpp` と、`include/kirimaspice/devices/` にあるそのヘッダ | BSIM4 4.8.3 のモデルのコード（`b4ld.c`、`b4temp.c`）。UC Berkeley と ngspice が配布しているもの | ECL-2.0、Copyright (c) 2025 University of California。表示は各ファイルの先頭に残してあり、ECL-2.0 の本文は英語版の末尾にあります |

### コメントで他のプロジェクトに触れているコード

| 場所 | コメントの内容 | そのプロジェクトのライセンス |
| --- | --- | --- |
| `src/devices/bjt_pnp.cpp` | PNP のモデルは「type multiplier」の方式を使うと書き、ngspice の `src/spicelib/devices/bjt/bjtload.c` を参照している | ngspice のこの部分は Modified BSD（ngspice の `COPYING`、2026-11-13 に読んだもの）。コードを取り込んだかどうかは未確認 |
| `src/devices/capacitor.cpp`、`src/devices/inductor.cpp` | 打ち切り誤差の見積もりの電荷と磁束のスケールについて、ngspice の `src/spicelib/analysis/cktdump.c` と Berkeley SPICE3f5 を参照している | 同上 |
| 上の BSIM4 の 6 ファイル | ngspice が持つ `b4ld.c` と `b4temp.c` の行番号を参照している | それらのファイルには BSIM4 の表示（ECL-2.0）があり、ngspice による変更は ngspice の Modified BSD に従う |

## テストだけで使うもの

| 部品 | 版 | ライセンス | 著作権表示 | 入手先 |
| --- | --- | --- | --- | --- |
| GoogleTest | v1.14.0 (`f8d7d77c06936315286eb55f8de22cd23c188571`) | BSD-3-Clause | Copyright 2008, Google Inc. | https://github.com/google/googletest |
| JSON for Modern C++（テスト 1 件で使用） | v3.11.3 (`9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03`) | MIT | Copyright (c) 2013-2022 Niels Lohmann | https://github.com/nlohmann/json |
| Google Benchmark（`BUILD_BENCHMARKS=ON` のときだけ） | v1.8.3 (`344117638c8ff7e239044fd0fa7085839fc03021`) | Apache-2.0（`LICENSE` による） | `AUTHORS` ファイルに挙げられた著作者 | https://github.com/google/benchmark |

一部のテストは、`ngspice` が入っていれば実行し、入っていなければスキップします。
`validation/` と `tests/validation/` の参照データは ngspice で作られたもので、
使った ngspice の版は記録されていません。ngspice のファイルはこのリポジトリに
写していません。

## 使っていないもの

- NLopt の Luksan のソルバー（LGPL-2.1-or-later）：`NLOPT_LUKSAN=OFF` で外している。
- SuperLU と SuperLU_MT：使っていない。
- SuperLU_DIST：`KIRIMASPICE_WITH_SUPERLUDIST=ON` にすると CMake は探すが、何もリンクしない。バックエンドは仮置き。
- SUNDIALS：オプション `KIRIMASPICE_WITH_SUNDIALS` はマクロを定義するだけで、何も探さず、何もリンクしない。
- SuiteSparse のほかのパッケージ（CHOLMOD、UMFPACK など）：リンクしない。

## ライセンスの全文

英語版（`THIRD_PARTY_NOTICES.md`）の「License texts」を見てください。
ライセンスの文面は英語のものだけが正式です。

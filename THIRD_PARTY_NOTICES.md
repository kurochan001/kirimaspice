# Third-party notices

[日本語](THIRD_PARTY_NOTICES.ja.md) · This English text is the main version; the Japanese version follows it, and where they differ, this text holds.

kirimaspice itself is licensed under the Apache License 2.0 with LLVM
Exceptions (see `LICENSE`). Parts of six files are based on the BSIM4 4.8.3
model code and keep its notice (ECL-2.0, below). Apart from that, this
repository holds no copies of third-party sources: the components below are
downloaded at configure time by CMake `FetchContent`, pinned to the versions
listed, or found on the system. Their licenses and copyright notices were read
from the license files of the fetched sources on 2026-11-13 and 2026-11-14.
The full texts of the licenses of the fetched components are at the end of
this file.

No built kirimaspice has been released; only the source is distributed.
Whoever distributes a built `kirimaspice` executable or library must meet the
terms of the components compiled into it, and of the LGPL for KLU and BTF.

## Compiled into kirimaspice

These are compiled into the library `kirimaspice_lib`, and from it into the
`kirimaspice` program and the test programs.

| Component | Version | License | Copyright | Source |
| --- | --- | --- | --- | --- |
| {fmt} | 9.1.0 (`a33701196adfad74917046096bf5a2aa0ab0bb50`) | MIT, with the optional exception in its `LICENSE.rst` | Copyright (c) 2012 - present, Victor Zverovich | https://github.com/fmtlib/fmt |
| spdlog | v1.13.0 (`7c02e204c92545f869e2f04edaab1f19fe8b19fd`) | MIT | Copyright (c) 2016 Gabi Melman. | https://github.com/gabime/spdlog |
| PEGTL | 3.2.7 (`cf639f7f4ee125f68e1ccfba8d99ebc0de57b9fe`) | MIT | Copyright (c) 2007-2022 Dr. Colin Hirsch and Daniel Frey | https://github.com/taocpp/PEGTL |
| ExprTk | master as of 2026-11-13 (`1e4a80b5ec9b4832ed59c6faa65f625a01b18ef0`) | MIT | Copyright (c) 1999-2025 Arash Partow | https://github.com/ArashPartow/exprtk |
| Boost (Graph and the Boost libraries it needs) | 1.86.0, `boost-1.86.0-cmake.tar.xz` (SHA-256 `2c5ec5edcdff47ff55e27ed9560b0a0b94b07bd07ed9928b476150e16b0efc57`) | BSL-1.0 | In the headers of the files | https://github.com/boostorg/boost |
| Zstandard (used by the SSF writer) | v1.5.6 (`794ea1b0afca0f020f4e57b6732332231fb23c70`) | BSD-3-Clause (offered as BSD-3-Clause or GPL-2.0; used under BSD-3-Clause) | Copyright (c) Meta Platforms, Inc. and affiliates. | https://github.com/facebook/zstd |
| NLopt, built with `NLOPT_LUKSAN=OFF` | v2.11.0 (`88c424d4f458412787df96fcc95218acbca224fd`) | MIT; its SLSQP code is BSD-3-Clause | Copyright (c) 2007-2024 Massachusetts Institute of Technology, and the authors named in the notices of its algorithms | https://github.com/stevengj/nlopt |

## Linked dynamically from the system

These are not included and not downloaded: CMake finds the SuiteSparse
installed on the system (for example `libsuitesparse-dev` on Ubuntu) and links
it dynamically. KLU 2.3.2 or newer is required. kirimaspice calls KLU only
through its public API. The versions are those tested; the copyright lines were
read from SuiteSparse 7.6.1 (`Doc/License.txt` of each package, and
`SuiteSparse_config/README.txt`). Source: https://github.com/DrTimothyAldenDavis/SuiteSparse

| Component | Version tested | License | Copyright |
| --- | --- | --- | --- |
| KLU (SuiteSparse) | 2.3.2 (SuiteSparse 7.6.1) | LGPL-2.1-or-later | KLU, Copyright (c) 2004-2022, University of Florida. |
| BTF (SuiteSparse) | 2.3.1 (SuiteSparse 7.6.1) | LGPL-2.1-or-later | BTF, Copyright (C) 2004-2013, University of Florida |
| AMD (SuiteSparse) | 3.3.1 (SuiteSparse 7.6.1) | BSD-3-Clause | AMD, Copyright (c), 1996-2022, Timothy A. Davis, Patrick R. Amestoy, and Iain S. Duff. |
| COLAMD (SuiteSparse) | 3.3.2 (SuiteSparse 7.6.1) | BSD-3-Clause | COLAMD, Copyright 1998-2022, Timothy A. Davis. |
| SuiteSparse_config | 7.6.1 | BSD-3-Clause | SuiteSparse_config, Copyright (c) 2012-2024, Timothy A. Davis. |

When OpenMP is found, the compiler's OpenMP runtime is linked as well; its
license is that of the compiler's runtime library and was not checked here.

## Code in this repository based on other code

| Files | Based on | License |
| --- | --- | --- |
| `src/devices/bsim4_core.cpp`, `src/devices/mosfet_bsim4.cpp`, `src/devices/mosfet_bsim4_params.cpp` and their headers in `include/kirimaspice/devices/` | The BSIM4 4.8.3 model code (`b4ld.c`, `b4temp.c`), as distributed by UC Berkeley and in ngspice | ECL-2.0, Copyright (c) 2025 University of California. The notice is kept at the top of each file; the text of ECL-2.0 is below |

### Code whose comments cite other projects

| Place | What its comment says | License of that project |
| --- | --- | --- |
| `src/devices/bjt_pnp.cpp` | The PNP model uses the "type multiplier" approach, with a reference to ngspice's `src/spicelib/devices/bjt/bjtload.c` | ngspice: Modified BSD for this part (ngspice `COPYING`, read on 2026-11-13). Whether code was taken from it is not confirmed |
| `src/devices/capacitor.cpp`, `src/devices/inductor.cpp` | The charge and flux scaling of the truncation-error estimate refers to ngspice's `src/spicelib/analysis/cktdump.c` and Berkeley SPICE3f5 | As above |
| The six BSIM4 files above | Line references to ngspice's copies of `b4ld.c` and `b4temp.c` | Those files carry the BSIM4 notice (ECL-2.0); changes made by ngspice are under its Modified BSD license |

## Used only by the tests

| Component | Version | License | Copyright | Source |
| --- | --- | --- | --- | --- |
| GoogleTest | v1.14.0 (`f8d7d77c06936315286eb55f8de22cd23c188571`) | BSD-3-Clause | Copyright 2008, Google Inc. | https://github.com/google/googletest |
| JSON for Modern C++ (used by one test) | v3.11.3 (`9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03`) | MIT | Copyright (c) 2013-2022 Niels Lohmann | https://github.com/nlohmann/json |
| Google Benchmark (only with `BUILD_BENCHMARKS=ON`) | v1.8.3 (`344117638c8ff7e239044fd0fa7085839fc03021`) | Apache-2.0 (its `LICENSE`) | The authors listed in its `AUTHORS` file | https://github.com/google/benchmark |

Some tests run `ngspice` when it is installed and are skipped otherwise. The
reference data in `validation/` and `tests/validation/` was produced with
ngspice; the ngspice version used is not recorded. No ngspice file is copied
into this repository.

## Not used

- NLopt's Luksan solvers (LGPL-2.1-or-later): left out with `NLOPT_LUKSAN=OFF`.
- SuperLU and SuperLU_MT: not used.
- SuperLU_DIST: with `KIRIMASPICE_WITH_SUPERLUDIST=ON`, CMake looks for it,
  but nothing is linked; the backend is a placeholder.
- SUNDIALS: the option `KIRIMASPICE_WITH_SUNDIALS` only sets a macro; nothing
  is searched for or linked.
- The other SuiteSparse packages (CHOLMOD, UMFPACK and so on): not linked.

## License texts

Copied from the fetched sources of the versions above.

### {fmt} (`LICENSE.rst`)

```text
Copyright (c) 2012 - present, Victor Zverovich

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

--- Optional exception to the license ---

As an exception, if, as a result of your compiling your source code, portions
of this Software are embedded into a machine-executable object form of such
source code, you may redistribute such embedded portions in such object form
without including the above copyright and permission notices.
```

### spdlog (`LICENSE`)

```text
The MIT License (MIT)

Copyright (c) 2016 Gabi Melman.                                       

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

-- NOTE: Third party dependency used by this software --
This software depends on the fmt lib (MIT License),
and users must comply to its license: https://raw.githubusercontent.com/fmtlib/fmt/master/LICENSE
```

### PEGTL (`LICENSE`)

```text
The MIT License (MIT)

Copyright (c) 2007-2022 Dr. Colin Hirsch and Daniel Frey

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

### ExprTk (`license.txt`)

```text
MIT License

Copyright (c) 1999-2025 Arash Partow

https://www.partow.net/programming/exprtk/index.html

Permission is hereby granted, free of charge, to any person  obtaining
a  copy  of  this software  and  associated  documentation files  (the
"Software"), to  deal in  the Software  without restriction, including
without limitation the  rights to use,  copy, modify, merge,  publish,
distribute, sublicense,  and/or sell  copies of  the Software,  and to
permit persons to whom the Software is furnished to do so, subject  to
the following conditions:

The  above  copyright  notice  and  this  permission  notice  shall be
included in all copies or substantial portions of the Software.

THE  SOFTWARE  IS PROVIDED  "AS  IS", WITHOUT  WARRANTY  OF ANY  KIND,
EXPRESS OR  IMPLIED, INCLUDING  BUT NOT  LIMITED TO  THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS  OR COPYRIGHT HOLDERS BE LIABLE FOR  ANY
CLAIM, DAMAGES OR OTHER LIABILITY,  WHETHER IN AN ACTION OF  CONTRACT,
TORT OR  OTHERWISE, ARISING  FROM, OUT  OF OR  IN CONNECTION  WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

### Boost Software License 1.0 (`LICENSE_1_0.txt`)

```text
Boost Software License - Version 1.0 - August 17th, 2003

Permission is hereby granted, free of charge, to any person or organization
obtaining a copy of the software and accompanying documentation covered by
this license (the "Software") to use, reproduce, display, distribute,
execute, and transmit the Software, and to prepare derivative works of the
Software, and to permit third-parties to whom the Software is furnished to
do so, all subject to the following:

The copyright notices in the Software and this entire statement, including
the above license grant, this restriction and the following disclaimer,
must be included in all copies of the Software, in whole or in part, and
all derivative works of the Software, unless such copies or derivative
works are solely in the form of machine-executable object code generated by
a source language processor.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
```

### Zstandard (`LICENSE`)

```text
BSD License

For Zstandard software

Copyright (c) Meta Platforms, Inc. and affiliates. All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

 * Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

 * Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

 * Neither the name Facebook, nor Meta, nor the names of its contributors may
   be used to endorse or promote products derived from this software without
   specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

### NLopt (`COPYING`)

```text
NLopt combines several free/open-source nonlinear optimization
libraries by various authors.  See the COPYING, COPYRIGHT, and README
files in the subdirectories for the original copyright and licensing
information of these packages.

The compiled NLopt library, i.e. the combined work of all of the
included optimization routines, is licensed under the conjunction of
all of these licensing terms. By default, the most restrictive terms
are for the code in the "luksan" directory, which is licensed under
the GNU Lesser General Public License (GNU LGPL), version 2.1 or
later (see luksan/COPYRIGHT). That means that, by default, the compiled
NLopt library is governed by the terms of the LGPL.

---------------------------------------------------------------------------

However, NLopt also offers the option to be built without the code in
the "luksan" directory. In this case, NLopt, including any modifications
to the abovementioned packages, are licensed under the standard "MIT License:"

Copyright (c) 2007-2024 Massachusetts Institute of Technology

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

### NLopt `src/algs/ags/COPYRIGHT`

```text
The MIT License (MIT)

Copyright (c) 2018 Sovrasov V.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

### NLopt `src/algs/bobyqa/COPYRIGHT`

```text
/* Copyright (c) 2009 M. J. D. Powell (mjdp@cam.ac.uk)
 * Modifications Copyright (c) 2010 Massachusetts Institute of Technology
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE. 
 */
```

### NLopt `src/algs/cobyla/COPYRIGHT`

```text
Copyright (c) 1992, Michael J. D. Powell (M.J.D.Powell@damtp.cam.ac.uk)
Copyright (c) 2004, Jean-Sebastien Roy (js@jeannot.org)
Copyright (c) 2008, Steven G. Johnson (stevenj@alum.mit.edu)

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

### NLopt `src/algs/direct/COPYING`

```text
This code is based on the DIRECT 2.0.4 Fortran code by Gablonsky et al. at
	http://www4.ncsu.edu/~ctk/SOFTWARE/DIRECTv204.tar.gz
The C version was initially converted via f2c and then cleaned up and
reorganized by Steven G. Johnson (stevenj@alum.mit.edu), August 2007.

******** Copyright and license for the original Fortran DIRECT code ********
Copyright (c) 1999, 2000, 2001 North Carolina State University

This program is distributed under the MIT License (see
http://www.opensource.org/licenses/mit-license.php):

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

### NLopt `src/algs/esch/COPYRIGHT`

```text
/* Copyright (c) 2008-2013 Carlos Henrique da Silva Santos
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE. 
 */
```

### NLopt `src/algs/newuoa/COPYRIGHT`

```text
/* Copyright (c) 2004 M. J. D. Powell (mjdp@cam.ac.uk)
 * Copyright (c) 2007-2011 Massachusetts Institute of Technology
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE. 
 */
```

### NLopt `src/algs/slsqp/COPYRIGHT`

```text
Copyright (c) 1988 Dieter Kraft

Copyright (c) 1994 Association for Computing Machinery

Copyright (c) 2001, 2002 Enthought, Inc.
All rights reserved.

Copyright (c) 2003-2009 SciPy Developers.
All rights reserved.

Copyright (c) 2010 Massachusetts Institute of Technology
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

  a. Redistributions of source code must retain the above copyright notice,
     this list of conditions and the following disclaimer.
  b. Redistributions in binary form must reproduce the above copyright
     notice, this list of conditions and the following disclaimer in the
     documentation and/or other materials provided with the distribution.
  c. Neither the name of the Enthought nor the names of its contributors
     may be used to endorse or promote products derived from this software
     without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
DAMAGE.

C      http://permalink.gmane.org/gmane.comp.python.scientific.devel/6725
C      ------
C      From: Deborah Cotton <cotton@hq.acm.org>
C      Date: Fri, 14 Sep 2007 12:35:55 -0500
C      Subject: RE: Algorithm License requested
C      To: Alan Isaac
C
C      Prof. Issac,
C
C      In that case, then because the author consents to [the ACM] releasing
C      the code currently archived at http://www.netlib.org/toms/733 under the
C      BSD license, the ACM hereby releases this code under the BSD license.
C
C      Regards,
C
C      Deborah Cotton, Copyright & Permissions
C      ACM Publications
C      2 Penn Plaza, Suite 701**
C      New York, NY 10121-0701
C      permissions@acm.org
C      212.869.7440 ext. 652
C      Fax. 212.869.0481
C      ------
```

### NLopt `src/algs/stogo/COPYRIGHT`

```text
The original StoGO code is:

Copyright (c) 1998 by S. Zertchaninov and K. Madsen.

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

----------------------------------------------------------------------

The StoGO source code on the authors' web site has no copyright or
license information, but I (Steven G. Johnson) contacted the author
K. Madsen for clarification and he gave me permission to distribute
it under the MIT license above.  See the correspondence below:

----------------------------------------------------------------------
Date: Fri, 24 Aug 2007 10:46:09 +0200
From: Kaj Madsen <km@imm.dtu.dk>
To: stevenj@math.mit.edu
Subject: RE: open source license for StoGO software?

The MIT licence is ok with me, and so is your copyright suggestion. 

Kaj Madsen. 

-----Original Message-----
From: Steven G. Johnson [mailto:stevenj@fftw.org] 
Sent: 23. august 2007 22:23
To: Kaj Madsen
Subject: RE: open source license for StoGO software?

On Thu, 23 Aug 2007, Kaj Madsen wrote:
> Many thanks for your mail. You can definitely consider the software as
> open source, please use it as you like, however please refer to us if 
> you publish results based on the software.
>
> I am now in a heavy administrative position as a Head of Department 
> (has been since 1998, therefore I never really followed up on this). 
> The paper was never published, however I am attaching two fdf-files, 
> one with the paper, one with some descriptions of the code. I hope 
> this makes sense, otherwise please don't hesitate to contact me again.

Thanks so much for your response!

To be open source it needs some specific open-source license to specify
the permissions in legal terms.  The simplest open-source license is
probably the MIT license, is this okay?

        http://opensource.org/licenses/mit-license.php

Also, I need to know who the authors are for the copyright statement. 
Should I list it as:
        Copyright (c) 1998 by S. Zertchaninov and K. Madsen ?

Thanks again!  I will definitely cite it if I publish any results based
on StoGO!  (It's one of the few public global-optimization programs that
is able to exploit gradient information.)

Regards,
Steven G. Johnson
```

### GoogleTest (`LICENSE`)

```text
Copyright 2008, Google Inc.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

    * Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
copyright notice, this list of conditions and the following disclaimer
in the documentation and/or other materials provided with the
distribution.
    * Neither the name of Google Inc. nor the names of its
contributors may be used to endorse or promote products derived from
this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

### JSON for Modern C++ (`LICENSE.MIT`)

```text
MIT License 

Copyright (c) 2013-2022 Niels Lohmann

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

### Educational Community License, Version 2.0 (ECL-2.0)

The text as published at https://opensource.org/license/ecl-2-0 (read on
2026-11-14 and converted from HTML to plain text).

```text
The Educational Community License version 2.0 (“ECL”) consists of the Apache 2.0 license, modified to change the scope of the patent grant in section 3 to be specific to the needs of the education communities using this license. The original Apache 2.0 license can be found at: http://www.apache.org/licenses/LICENSE-2.0

TERMS AND CONDITIONS FOR USE, REPRODUCTION, AND DISTRIBUTION

1. Definitions.

“License” shall mean the terms and conditions for use, reproduction, and distribution as defined by Sections 1 through 9 of this document.

“Licensor” shall mean the copyright owner or entity authorized by the copyright owner that is granting the License.

“Legal Entity” shall mean the union of the acting entity and all other entities that control, are controlled by, or are under common control with that entity. For the purposes of this definition, “control” means (i) the power, direct or indirect, to cause the direction or management of such entity, whether by contract or otherwise, or (ii) ownership of fifty percent (50%) or more of the outstanding shares, or (iii) beneficial ownership of such entity.

“You” (or “Your”) shall mean an individual or Legal Entity exercising permissions granted by this License.

“Source” form shall mean the preferred form for making modifications, including but not limited to software source code, documentation source, and configuration files.

“Object” form shall mean any form resulting from mechanical transformation or translation of a Source form, including but not limited to compiled object code, generated documentation, and conversions to other media types.

“Work” shall mean the work of authorship, whether in Source or Object form, made available under the License, as indicated by a copyright notice that is included in or attached to the work (an example is provided in the Appendix below).

“Derivative Works” shall mean any work, whether in Source or Object form, that is based on (or derived from) the Work and for which the editorial revisions, annotations, elaborations, or other modifications represent, as a whole, an original work of authorship. For the purposes of this License, Derivative Works shall not include works that remain separable from, or merely link (or bind by name) to the interfaces of, the Work and Derivative Works thereof.

“Contribution” shall mean any work of authorship, including the original version of the Work and any modifications or additions to that Work or Derivative Works thereof, that is intentionally submitted to Licensor for inclusion in the Work by the copyright owner or by an individual or Legal Entity authorized to submit on behalf of the copyright owner. For the purposes of this definition, “submitted” means any form of electronic, verbal, or written communication sent to the Licensor or its representatives, including but not limited to communication on electronic mailing lists, source code control systems, and issue tracking systems that are managed by, or on behalf of, the Licensor for the purpose of discussing and improving the Work, but excluding communication that is conspicuously marked or otherwise designated in writing by the copyright owner as “Not a Contribution.”

“Contributor” shall mean Licensor and any individual or Legal Entity on behalf of whom a Contribution has been received by Licensor and subsequently incorporated within the Work.

2. Grant of Copyright License.

Subject to the terms and conditions of this License, each Contributor hereby grants to You a perpetual, worldwide, non-exclusive, no-charge, royalty-free, irrevocable copyright license to reproduce, prepare Derivative Works of, publicly display, publicly perform, sublicense, and distribute the Work and such Derivative Works in Source or Object form.

3. Grant of Patent License.

Subject to the terms and conditions of this License, each Contributor hereby grants to You a perpetual, worldwide, non-exclusive, no-charge, royalty-free, irrevocable (except as stated in this section) patent license to make, have made, use, offer to sell, sell, import, and otherwise transfer the Work, where such license applies only to those patent claims licensable by such Contributor that are necessarily infringed by their Contribution(s) alone or by combination of their Contribution(s) with the Work to which such Contribution(s) was submitted. If You institute patent litigation against any entity (including a cross-claim or counterclaim in a lawsuit) alleging that the Work or a Contribution incorporated within the Work constitutes direct or contributory patent infringement, then any patent licenses granted to You under this License for that Work shall terminate as of the date such litigation is filed. Any patent license granted hereby with respect to contributions by an individual employed by an institution or organization is limited to patent claims where the individual that is the author of the Work is also the inventor of the patent claims licensed, and where the organization or institution has the right to grant such license under applicable grant and research funding agreements. No other express or implied licenses are granted.

4. Redistribution.

You may reproduce and distribute copies of the Work or Derivative Works thereof in any medium, with or without modifications, and in Source or Object form, provided that You meet the following conditions:

You must give any other recipients of the Work or Derivative Works a copy of this License; and

You must cause any modified files to carry prominent notices stating that You changed the files; and

You must retain, in the Source form of any Derivative Works that You distribute, all copyright, patent, trademark, and attribution notices from the Source form of the Work, excluding those notices that do not pertain to any part of the Derivative Works; and

If the Work includes a “NOTICE” text file as part of its distribution, then any Derivative Works that You distribute must include a readable copy of the attribution notices contained within such NOTICE file, excluding those notices that do not pertain to any part of the Derivative Works, in at least one of the following places: within a NOTICE text file distributed as part of the Derivative Works; within the Source form or documentation, if provided along with the Derivative Works; or, within a display generated by the Derivative Works, if and wherever such third-party notices normally appear. The contents of the NOTICE file are for informational purposes only and do not modify the License. You may add Your own attribution notices within Derivative Works that You distribute, alongside or as an addendum to the NOTICE text from the Work, provided that such additional attribution notices cannot be construed as modifying the License.

You may add Your own copyright statement to Your modifications and may provide additional or different license terms and conditions for use, reproduction, or distribution of Your modifications, or for any such Derivative Works as a whole, provided Your use, reproduction, and distribution of the Work otherwise complies with the conditions stated in this License.

5. Submission of Contributions.

Unless You explicitly state otherwise, any Contribution intentionally submitted for inclusion in the Work by You to the Licensor shall be under the terms and conditions of this License, without any additional terms or conditions. Notwithstanding the above, nothing herein shall supersede or modify the terms of any separate license agreement you may have executed with Licensor regarding such Contributions.

6. Trademarks.

This License does not grant permission to use the trade names, trademarks, service marks, or product names of the Licensor, except as required for reasonable and customary use in describing the origin of the Work and reproducing the content of the NOTICE file.

7. Disclaimer of Warranty.

Unless required by applicable law or agreed to in writing, Licensor provides the Work (and each Contributor provides its Contributions) on an “AS IS” BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied, including, without limitation, any warranties or conditions of TITLE, NON-INFRINGEMENT, MERCHANTABILITY, or FITNESS FOR A PARTICULAR PURPOSE. You are solely responsible for determining the appropriateness of using or redistributing the Work and assume any risks associated with Your exercise of permissions under this License.

8. Limitation of Liability.

In no event and under no legal theory, whether in tort (including negligence), contract, or otherwise, unless required by applicable law (such as deliberate and grossly negligent acts) or agreed to in writing, shall any Contributor be liable to You for damages, including any direct, indirect, special, incidental, or consequential damages of any character arising as a result of this License or out of the use or inability to use the Work (including but not limited to damages for loss of goodwill, work stoppage, computer failure or malfunction, or any and all other commercial damages or losses), even if such Contributor has been advised of the possibility of such damages.

9. Accepting Warranty or Additional Liability.

While redistributing the Work or Derivative Works thereof, You may choose to offer, and charge a fee for, acceptance of support, warranty, indemnity, or other liability obligations and/or rights consistent with this License. However, in accepting such obligations, You may act only on Your own behalf and on Your sole responsibility, not on behalf of any other Contributor, and only if You agree to indemnify, defend, and hold each Contributor harmless for any liability incurred by, or claims asserted against, such Contributor by reason of your accepting any such warranty or additional liability.

END OF TERMS AND CONDITIONS

APPENDIX: How to apply the Educational Community License to your work

To apply the Educational Community License to your work, attach

the following boilerplate notice, with the fields enclosed by

brackets “[]” replaced with your own identifying information.

(Don’t include the brackets!) The text should be enclosed in the

appropriate comment syntax for the file format. We also recommend

that a file or class name and description of purpose be included on

the same “printed page” as the copyright notice for easier

identification within third-party archives.

Copyright [yyyy] [name of copyright owner] Licensed under the Educational Community License, Version 2.0 (the “License”); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.osedu.org/licenses/ECL-2.0 Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an “AS IS” BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
```

v8ppc
=====

Port of Google V8 javascript engine to PowerPC - PowerLinux and AIX.

Platform | Build Status | Test Status
---------|--------------|-------------
PowerLinux 32-bit | [![Build Status](http://v8ppc.osuosl.org:8080/buildStatus/icon?job=Build-PowerPC-V8)](http://v8ppc.osuosl.org:8080/job/Build-PowerPC-V8/) | [![Build Status](http://v8ppc.osuosl.org:8080/buildStatus/icon?job=Test-PowerPC-V8)](http://v8ppc.osuosl.org:8080/job/Test-PowerPC-V8/)
PowerLinux 64-bit | [![Build Status](http://v8ppc.osuosl.org:8080/buildStatus/icon?job=Build-PowerPC64-V8)](http://v8ppc.osuosl.org:8080/job/Build-PowerPC64-V8/) | [![Build Status](http://v8ppc.osuosl.org:8080/buildStatus/icon?job=Test-PowerPC64-V8)](http://v8ppc.osuosl.org:8080/job/Test-PowerPC64-V8/)
AIX 32-bit | [![Build Status](http://v8ppc.osuosl.org:8080/buildStatus/icon?job=Build-AIX-V8)](http://v8ppc.osuosl.org:8080/job/Build-AIX-V8/) | [![Build Status](http://v8ppc.osuosl.org:8080/buildStatus/icon?job=Test-AIX-V8)](http://v8ppc.osuosl.org:8080/job/Test-AIX-V8/)
AIX 64-bit | [![Build Status](http://v8ppc.osuosl.org:8080/buildStatus/icon?job=Build-AIX64-V8)](http://v8ppc.osuosl.org:8080/job/Build-AIX64-V8/) | [![Build Status](http://v8ppc.osuosl.org:8080/buildStatus/icon?job=Test-AIX64-V8)](http://v8ppc.osuosl.org:8080/job/Test-AIX64-V8/)

V8 JavaScript Engine
=============

V8 is Google's open source JavaScript engine.

V8 implements ECMAScript as specified in ECMA-262.

V8 is written in C++ and is used in Google Chrome, the open source
browser from Google.

V8 can run standalone, or can be embedded into any C++ application.

V8 Project page: https://code.google.com/p/v8/


Getting the Code
=============

Checkout [depot tools](http://www.chromium.org/developers/how-tos/install-depot-tools), and run

> `fetch v8`

This will checkout V8 into the directory `v8` and fetch all of its dependencies.
To stay up to date, run

> `git pull origin`
> `gclient sync`

For fetching all branches, add the following into your remote
configuration in `.git/config`:

        fetch = +refs/branch-heads/*:refs/remotes/branch-heads/*
        fetch = +refs/tags/*:refs/tags/*

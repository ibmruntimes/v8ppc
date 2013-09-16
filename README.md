v8ppc
=====

Port of Google V8 javascript engine to PowerPC - PowerLinux and AIX.

August 9th 98% of the tests were passing. (with crankshaft!)

Compile code:<br><code>
make dependencies; make -j8 ppc
</code>

Test code:<br><code>
tools/run-tests.py -j 12 --progress=dots --no-presubmit --arch-and-mode=ppc.debug --junitout v8tests-junit.xml
</code>

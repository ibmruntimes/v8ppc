v8ppc
=====

Port of Google V8 javascript engine to PowerPC

July 16th 95% of the tests were passing. (with crankshaft!)

Compile code:<br><code>
make dependencies; make -j8 ppc regexp=interpreted
</code>

Test code:<br><code>
tools/run-tests.py -j 12 --progress=dots --no-presubmit --arch-and-mode=ppc.debug --junitout v8tests-junit.xml
</code>

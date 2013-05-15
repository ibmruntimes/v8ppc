v8ppc
=====

Port of Google V8 javascript engine to PowerPC

May 14th 79% of the tests were passing. 

Compile code:<br><code>
make dependencies
make -j8 ppc snapshot=off regexp=interpreted
</code>

Test code:<br><code>
tools/test-wrapper-gypbuild.py -j 24 --progress=dots --no-presubmit --arch-and-mode=ppc.debug
</code>

include ../Make.inc

bftest:
	$(TESTOUT) -bfgs
	@cat fails
	@diff -rbB output newout > diffs || /bin/true
	@grep ^Only ./diffs || /bin/true


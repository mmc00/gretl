BINDIR = ../../bin

TESTOUT = $(BINDIR)/gretl_test_scripts
MAKEOUT = $(BINDIR)/gretl_scripts_baseline
MEMTEST = $(BINDIR)/gretl_leak_check
PROFILE = $(BINDIR)/gretl_scripts_profile

.PHONY:

test:
	$(TESTOUT)
	@cat fails
	@echo "=== `pwd` ===" > diffs
	@diff -rbB output newout >> diffs || /bin/true
	@grep ^Only ./diffs || /bin/true

new:
	$(MAKEOUT)

memtest:
	$(MEMTEST)

profile:
	$(PROFILE)

list:
	ls -1 *.inp > ps.list

clean:
	rm -rf ./newout
	rm -f diffs fails errs out
	rm -f *.log
	rm -f gpttmp* session.inp

BINDIR = ./bin

TESTOUT = $(BINDIR)/gretl_test_scripts
MAKEOUT = $(BINDIR)/gretl_scripts_baseline
COMPARE = $(BINDIR)/gretl_output_compare
MEMTEST = $(BINDIR)/gretl_leak_check
PROFILE = $(BINDIR)/gretl_scripts_profile
ASSESS = $(BINDIR)/assess_all
REFACTOR = $(BINDIR)/refactor_data

.PHONY:

test:
	$(TESTOUT)
	$(REFACTOR)
	@cat fails
	@echo "=== `pwd` ===" > diffs
	$(COMPARE) >> diffs

new:
	$(MAKEOUT)

counter: counter.c
	gcc -Wall -o $@ $<

memtest:
	$(MEMTEST)

profile:
	$(PROFILE)

clean:
	rm -rf ./newout
	rm -f diffs fails errs out
	rm -f *.log
	rm -f gpttmp* session.inp
	
# note: "gfn" excluded from subdirs: test separately	

subdirs = . append arbond arma bundles builtins count csv duration \
	fcast fnsample foreign gmm importer intreg \
	kalman loess matrix mle ordered panel rq sarma \
	stock-watson strings sysloop system ufunc vecm \
	probit cluster

test-all:
	for d in $(subdirs) ; do make -C $$d test ; done
	$(ASSESS)

clean-all:
	for d in $(subdirs) ; do make -C $$d clean ; done

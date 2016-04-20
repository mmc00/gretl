BINDIR = ./bin
TESTOUT = CLI_NO_PLOTS=1 GRETL_FORCE_SEED=1234 $(BINDIR)/gretl_test_scripts
MAKEOUT = CLI_NO_PLOTS=1 GRETL_FORCE_SEED=1234 $(BINDIR)/gretl_scripts_baseline
COMPARE = $(BINDIR)/gretl_output_compare

CC = gcc -O2 -Wall
CFLAGS = `pkg-config --cflags gretl`
LIBS = `pkg-config --libs gretl`

test: ps.list
	$(TESTOUT)
	@cat fails
	@echo "=== `pwd` ===" > diffs
	$(COMPARE) >> diffs

baseline: ps.list
	$(MAKEOUT)

ps.list: inp.stamp
	cat /dev/null > $@
	for f in *.gfn.inp ; do echo $$f >> $@ ; done

inp.stamp: get_sample_script gfn.stamp
	cat /dev/null > pkg_include_errors.txt
	cat /dev/null > zipdirs.txt
	for f in *.gfn *.zip ; do \
	./get_sample_script $$f; done
	cat pkg_include_errors.txt
	touch $@

get_sample_script: get_sample_script.c
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

gfn.stamp:
	./grab_gfn_files.sh && touch $@

clean:
	rm -rf ./newout
	rm -f diffs fails errs out
	rm -f *.log *~
	rm -f gpttmp* session.inp

gfnclean: clean
	rm -f *.gfn *.zip *.gfn.inp *.pdf ps.list *.stamp get_sample_script
	if [ -f zipdirs.txt ] ; then \
	for d in `cat zipdirs.txt` ; do \
	rm -rf $$d; done; fi

all:
	gretlcli -b csvtest.inp > csvtest.out.new
	LANG=es_ES.UTF8 gretlcli -b csvtest.inp > csvtest_es.out.new
	diff -bB csvtest.out csvtest.out.new > diffs || /bin/true
	diff -bB csvtest_es.out csvtest_es.out.new >> diffs || /bin/true
	
new:
	gretlcli -b csvtest.inp > csvtest.out
	LANG=es_ES.UTF8 gretlcli -b csvtest.inp > csvtest_es.out		

memtest:
	G_SLICE=always-malloc valgrind --tool=memcheck --leak-check=yes --log-fd=9 \
	gretlcli -b csvtest.inp >/dev/null 2>&1 9>valgrind.log

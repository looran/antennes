with_clang:
	clang -Wall -O2 -o antennes antennes.c

with_gcc:
	gcc -Wall -O2 -o antennes antennes.c

debug:
	clang -g -O0 -Weverything -DDEBUG -o antennes antennes.c

test:
	for d in extract/20*-*; do \
		echo ====================== $$d; \
		rm -rf /tmp/antennes_test; \
		mkdir /tmp/antennes_test; \
		./antennes -k /tmp/antennes_test -s $$d >/dev/null || exit 1; \
	done
	@echo test ok

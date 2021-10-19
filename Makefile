CLEAN_SUBDIRS = src doc tests
CHECKPATCH_IGNORES = SPDX_LICENSE_TAG,UNSPECIFIED_INT,STRLCPY,SIZEOF_PARENTHESIS,AVOID_EXTERNS,CONSIDER_COMPLETION,NEW_TYPEDEFS,PREFER_KERNEL_TYPES,MULTIPLE_ASSIGNMENTS,OPEN_ENDED_LINE
GIT_BASE_COMMIT = 84de5c2bc75074d309e9efabf022b0afa80167a7
GREP_GROUP_SEPARATOR = "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"

all::
	@echo "This makefile has only 'clean' and 'check' targets."

clean::
	for d in $(CLEAN_SUBDIRS); do $(MAKE) -C $$d $@; done

distclean:: clean
	find . -name '*~' -exec rm '{}' \;

check::
	make -C tests $@

lint::
	@files=$$(git diff $(GIT_BASE_COMMIT) --name-only 2>&1 | grep "\.[hc]$$");\
	echo "###################" | tee lint.out >/dev/null;\
	printf "$$files\n" | tee -a lint.out >/dev/null;\
	echo "###################" | tee -a lint.out >/dev/null;\
	./checkpatch.pl --strict --color=always -f --no-tree --ignore $(CHECKPATCH_IGNORES) --tab-size=2 --max-line-length=80 --show-types $$files 2>/dev/null\
	| grep -e WARNING: -e ERROR: -e CHECK: -A 1 --group-separator=$(GREP_GROUP_SEPARATOR) \
	| tr "\n" " " | sed "s/$(GREP_GROUP_SEPARATOR)/\n/g" \
	| awk '{$$1=$$1;print}' | sed 's/.$$//' \
	| grep -v -e PRI --no-group-separator \
	| grep -v -e "No space is necessary after a cast" --no-group-separator \
	| tee -a lint.out;\
	rm -f .checkpatch-camelcase.git.*
	@if cat lint.out | grep -e WARNING -e ERROR -e CHECK; then \
		exit 10; \
	fi

reformat::
	@git diff $(GIT_BASE_COMMIT) --name-only 2>&1 | grep "\.[hc]$$" | while read file; \
	do echo "$$file";\
	python3 reformat.py $$file;\
	done

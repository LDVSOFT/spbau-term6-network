include Makefile.vars

.PHONY: all clean build $(DEP)

all: elevate

build: | obj bin dep
	rm -f $(DEP)
	make -f Makefile.build
	ln -s server64 bin/server 2>/dev/null | true

elevate: build
	./elevate.sh

clean:
	rm -rf obj/ bin/ dep/

obj bin dep:
	mkdir $@

CONFIG ?= Release

all: core server tests

tests: core
	cd tests && make

server: core
	cd src/server && make

core:
	cd src/core && make

install: core server
	rm -f /usr/local/bin/scache
	cp "src/server/scache" /usr/local/bin/scache
	chmod +x /usr/local/bin/scache
	echo "Installed to /usr/local/bin/scache";

clean:
	cd src/server && make clean && cd ..
	cd src/core && make clean && cd ../..
	cd tests && make clean && cd ..
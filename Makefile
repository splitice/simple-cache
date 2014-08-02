CONFIG ?= Release

all: core server tests

tests: core
	cd tests && make

server: core
	cd src/server && make

core:
	cd src/core && make

install: core server
	chmod +x /usr/local/bin/scache
	echo "Installed to /usr/local/bin/scache";
all: core server tests

tests: core
	cd tests && make

server: core
	cd src/server && make

core:
	cd src/core && make

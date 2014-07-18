all: 
	cd src/core
	make
	cd ../server
	make
	cd ../../tests
	make

tests:
	cd src/core
	make
	cd ../../tests
	make
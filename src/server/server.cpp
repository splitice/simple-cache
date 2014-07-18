#include "server.h"

/*
	To test the library, include "server.h" from an application project
	and call serverTest().
	
	Do not forget to add the library to Project Dependencies in Visual Studio.
*/

static int s_Test = 0;

extern "C" int serverTest();

int serverTest()
{
	return ++s_Test;
}
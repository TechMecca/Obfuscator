#include <Windows.h>
#include <iostream>
#include <Junk.h>
#include <StringObfuscator.h>

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
	std::cout << "Hello CMake." << std::endl;
	return 0;
}
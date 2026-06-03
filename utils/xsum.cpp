#include "base.h"

#include <chrono>
#include <iostream>
#include <cstdlib>

#include <dcpp/FileReader.h>
#include <dcpp/Util.h>
#include <dcpp/MerkleTree.h>

using namespace std;
using namespace dcpp;

int main(int argc, char** argv)
{
	if(argc < 2) {
		cout << "You need to supply a file name" << endl;
		return 1;
	}

#ifdef _WIN32
	char pathBuf[_MAX_PATH] = { 0 };
	if(!_fullpath(pathBuf, argv[1], _MAX_PATH)) {
		cout << "Can't get full path" << endl;
		return 1;
	}
	string x(pathBuf);
#else
	char* pathBuf = realpath(argv[1], 0);
	if(!pathBuf) {
		cout << "Can't get full path" << endl;
		return 1;
	}
	string x(pathBuf);
	free(pathBuf);
#endif

	auto direct = argc < 3 ? true : argv[2][0] == '0';
	auto bufSize = argc < 4 ? 0 : Util::toInt(argv[3]);

	try {
		auto start = std::chrono::steady_clock::now();
		FileReader fr(direct, bufSize);

		TigerTree tt;
		size_t total = 0;
		fr.read(x, [&](const void* x, size_t n) -> bool {
			tt.update(x, n);
			total += n;
			if(total % (1024*1024) == 0) {
				std::cout << ".";
			}

			return true;
		});

		cout << endl << Encoder::toBase32(tt.finalize(), TigerTree::BYTES);

		auto diff = std::chrono::steady_clock::now() - start;
		auto s = std::chrono::duration<double>(diff).count();
		if(s == 0) s = 1;
		cout << endl << Util::formatBytes(total) << ", " << s << " s, " << Util::formatBytes(total / s) << " b/s" << endl;
	} catch(const std::exception& e) {
		cout << "Error: " << e.what() << endl;
	}

	return 0;
}




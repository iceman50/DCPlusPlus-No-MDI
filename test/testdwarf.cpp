#include "testbase.h"

#if defined(__MINGW32__)

#include <windows.h>

#include <dwarf.h>
#define LIBDWARF_STATIC
#include <libdwarf.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <string>

namespace {

__attribute__((noinline)) int dwarfTestMarker(int value) {
	volatile int marker = value + 1;
	return marker;
}

std::string executablePath() {
	std::string path(MAX_PATH, '\0');
	const auto size = ::GetModuleFileNameA(nullptr, &path[0], static_cast<DWORD>(path.size()));
	path.resize(size);
	return path;
}

std::string debugCompanionPath() {
	auto path = executablePath();
	const auto dot = path.rfind('.');
	if(dot != std::string::npos) {
		path.replace(dot, path.size() - dot, ".pdb");
	}
	return path;
}

std::uint64_t preferredImageBase() {
	std::ifstream file(executablePath(), std::ios::binary);
	IMAGE_DOS_HEADER dos = {};
	if(!file.read(reinterpret_cast<char*>(&dos), sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE) {
		return 0;
	}

	file.seekg(dos.e_lfanew, std::ios::beg);
	DWORD signature = 0;
	IMAGE_NT_HEADERS64 nt = {};
	if(!file.read(reinterpret_cast<char*>(&signature), sizeof(signature)) || signature != IMAGE_NT_SIGNATURE ||
		!file.read(reinterpret_cast<char*>(&nt.FileHeader), sizeof(nt.FileHeader)) ||
		!file.read(reinterpret_cast<char*>(&nt.OptionalHeader), sizeof(nt.OptionalHeader)))
	{
		return 0;
	}
	return nt.OptionalHeader.ImageBase;
}

Dwarf_Addr markerDebugAddress() {
	const auto module = reinterpret_cast<std::uintptr_t>(::GetModuleHandle(nullptr));
	const auto runtimeAddress = reinterpret_cast<std::uintptr_t>(&dwarfTestMarker);
	return preferredImageBase() + (runtimeAddress - module);
}

std::string dwarfError(Dwarf_Error error) {
	return error ? dwarf_errmsg(error) : "unknown libdwarf error";
}

}

TEST(testdwarf, resolves_line_from_debug_companion) {
	ASSERT_EQ(2, dwarfTestMarker(1));

	Dwarf_Debug dbg = nullptr;
	Dwarf_Error error = nullptr;
	const auto path = debugCompanionPath();
	const auto initResult = dwarf_init_path(path.c_str(), nullptr, 0, DW_GROUPNUMBER_ANY,
		nullptr, nullptr, &dbg, &error);
	ASSERT_EQ(DW_DLV_OK, initResult) << path << ": " << dwarfError(error);

	Dwarf_Arange* aranges = nullptr;
	Dwarf_Signed arangeCount = 0;
	const auto arangesResult = dwarf_get_aranges(dbg, &aranges, &arangeCount, &error);
	ASSERT_EQ(DW_DLV_OK, arangesResult) << dwarfError(error);

	Dwarf_Arange arange = nullptr;
	const auto address = markerDebugAddress();
	const auto arangeResult = dwarf_get_arange(aranges, arangeCount, address, &arange, &error);
	ASSERT_EQ(DW_DLV_OK, arangeResult) << "No arange for 0x" << std::hex << address << ": " << dwarfError(error);

	Dwarf_Off dieOffset = 0;
	ASSERT_EQ(DW_DLV_OK, dwarf_get_cu_die_offset(arange, &dieOffset, &error)) << dwarfError(error);

	Dwarf_Die cuDie = nullptr;
	ASSERT_EQ(DW_DLV_OK, dwarf_offdie_b(dbg, dieOffset, true, &cuDie, &error)) << dwarfError(error);

	Dwarf_Unsigned version = 0;
	Dwarf_Small tableCount = 0;
	Dwarf_Line_Context lineContext = nullptr;
	ASSERT_EQ(DW_DLV_OK, dwarf_srclines_b(cuDie, &version, &tableCount, &lineContext, &error)) << dwarfError(error);

	Dwarf_Line* lines = nullptr;
	Dwarf_Signed lineCount = 0;
	ASSERT_EQ(DW_DLV_OK, dwarf_srclines_from_linecontext(lineContext, &lines, &lineCount, &error)) << dwarfError(error);

	std::string source;
	Dwarf_Unsigned bestDelta = static_cast<Dwarf_Unsigned>(-1);
	for(Dwarf_Signed i = 0; i < lineCount; ++i) {
		Dwarf_Addr lineAddress = 0;
		Dwarf_Bool endSequence = false;
		if(dwarf_lineendsequence(lines[i], &endSequence, &error) != DW_DLV_OK || endSequence ||
			dwarf_lineaddr(lines[i], &lineAddress, &error) != DW_DLV_OK || address < lineAddress)
		{
			continue;
		}

		const auto delta = address - lineAddress;
		if(delta < bestDelta) {
			char* lineSource = nullptr;
			if(dwarf_linesrc(lines[i], &lineSource, &error) == DW_DLV_OK) {
				source = lineSource;
				dwarf_dealloc(dbg, lineSource, DW_DLA_STRING);
				bestDelta = delta;
			}
		}
	}

	EXPECT_NE(std::string::npos, source.find("testdwarf.cpp")) << source;

	dwarf_srclines_dealloc_b(lineContext);
	dwarf_dealloc(dbg, cuDie, DW_DLA_DIE);
	for(Dwarf_Signed i = 0; i < arangeCount; ++i) {
		dwarf_dealloc(dbg, aranges[i], DW_DLA_ARANGE);
	}
	dwarf_dealloc(dbg, aranges, DW_DLA_LIST);
	dwarf_finish(dbg);
}

#endif

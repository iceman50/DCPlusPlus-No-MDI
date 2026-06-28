/*
 * Copyright (C) 2001-2025 Jacek Sieka, arnetheduck on gmail point com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "stdafx.h"
#include "CrashLogger.h"

#include <iostream>
#include <fstream>

#include <dcpp/Util.h>
#include <dcpp/version.h>
#include "WinUtil.h"

namespace {
	

FILE* f;

#if defined(__MINGW32__)

/* All MinGW variants (even x64 SEH ones) store debug information as DWARF. We use libdwarf to
parse it. */

#include <imagehlp.h>

#include <dwarf.h>
#define LIBDWARF_STATIC
#include <libdwarf.h>

bool isPortableExecutable(const string& path) {
	std::ifstream file(path, std::ios::binary);
	WORD signature = 0;
	return file.read(reinterpret_cast<char*>(&signature), sizeof(signature)) &&
		signature == IMAGE_DOS_SIGNATURE;
}

bool dieContainsAddress(Dwarf_Debug dbg, Dwarf_Die die, Dwarf_Addr addr, Dwarf_Error& error) {
	Dwarf_Addr lowpc = 0;
	Dwarf_Addr highpc = 0;
	Dwarf_Attribute highAttr = nullptr;
	if(dwarf_lowpc(die, &lowpc, &error) == DW_DLV_OK &&
		dwarf_attr(die, DW_AT_high_pc, &highAttr, &error) == DW_DLV_OK)
	{
		Dwarf_Half form = 0;
		if(dwarf_whatform(highAttr, &form, &error) == DW_DLV_OK) {
			bool gotHigh = false;
			if(form == DW_FORM_addr) {
				Dwarf_Half highForm = 0;
				enum Dwarf_Form_Class highClass = DW_FORM_CLASS_UNKNOWN;
				gotHigh = dwarf_highpc_b(die, &highpc, &highForm, &highClass, &error) == DW_DLV_OK;
			} else {
				Dwarf_Unsigned offset = 0;
				if(dwarf_formudata(highAttr, &offset, &error) == DW_DLV_OK) {
					highpc = lowpc + offset;
					gotHigh = true;
				}
			}
			if(gotHigh && addr >= lowpc && addr < highpc) {
				dwarf_dealloc(dbg, highAttr, DW_DLA_ATTR);
				return true;
			}
		}
		dwarf_dealloc(dbg, highAttr, DW_DLA_ATTR);
	}

	Dwarf_Attribute rangesAttr = nullptr;
	if(dwarf_attr(die, DW_AT_ranges, &rangesAttr, &error) != DW_DLV_OK) {
		return false;
	}

	Dwarf_Half version = 0;
	Dwarf_Half offsetSize = 0;
	Dwarf_Half form = 0;
	Dwarf_Off rangesOffset = 0;
	bool found = false;
	if(dwarf_get_version_of_die(die, &version, &offsetSize) == DW_DLV_OK &&
		dwarf_whatform(rangesAttr, &form, &error) == DW_DLV_OK &&
		dwarf_global_formref(rangesAttr, &rangesOffset, &error) == DW_DLV_OK)
	{
		if(version >= 5) {
			Dwarf_Rnglists_Head head = nullptr;
			Dwarf_Unsigned count = 0;
			if(dwarf_rnglists_get_rle_head(rangesAttr, form, rangesOffset, &head, &count, nullptr, &error) == DW_DLV_OK) {
				for(Dwarf_Unsigned i = 0; i < count && !found; ++i) {
					unsigned int entryLength = 0;
					unsigned int entryType = 0;
					Dwarf_Bool addressUnavailable = false;
					Dwarf_Unsigned rangeStart = 0;
					Dwarf_Unsigned rangeEnd = 0;
					if(dwarf_get_rnglists_entry_fields_a(head, i, &entryLength, &entryType, nullptr, nullptr,
						&addressUnavailable, &rangeStart, &rangeEnd, &error) == DW_DLV_OK &&
						!addressUnavailable &&
						entryType != DW_RLE_end_of_list &&
						entryType != DW_RLE_base_address &&
						entryType != DW_RLE_base_addressx)
					{
						found = addr >= rangeStart && addr < rangeEnd;
					}
				}
				dwarf_dealloc_rnglists_head(head);
			}
		} else {
			Dwarf_Ranges* ranges = nullptr;
			Dwarf_Signed count = 0;
			Dwarf_Unsigned byteCount = 0;
			Dwarf_Off realOffset = 0;
			if(dwarf_get_ranges_b(dbg, rangesOffset, die, &realOffset, &ranges, &count, &byteCount, &error) == DW_DLV_OK) {
				Dwarf_Bool knownBase = false;
				Dwarf_Bool hasRangesOffset = false;
				Dwarf_Unsigned baseAddress = 0;
				Dwarf_Unsigned actualRangesOffset = 0;
				dwarf_get_ranges_baseaddress(dbg, die, &knownBase, &baseAddress,
					&hasRangesOffset, &actualRangesOffset, &error);
				for(Dwarf_Signed i = 0; i < count && !found; ++i) {
					if(ranges[i].dwr_type == DW_RANGES_ADDRESS_SELECTION) {
						baseAddress = ranges[i].dwr_addr2;
					} else if(ranges[i].dwr_type == DW_RANGES_ENTRY) {
						found = addr >= baseAddress + ranges[i].dwr_addr1 &&
							addr < baseAddress + ranges[i].dwr_addr2;
					}
				}
				dwarf_dealloc_ranges(dbg, ranges, count);
			}
		}
	}
	dwarf_dealloc(dbg, rangesAttr, DW_DLA_ATTR);
	return found;
}

/* this recursive function browses through the children and siblings of a DIE, looking for the one
DIE that specifically describes the enclosing function of the given address. */
Dwarf_Die browseDIE(Dwarf_Debug dbg, Dwarf_Addr addr, Dwarf_Die die, Dwarf_Error& error) {

	/* only care about DIEs that represent functions (section 3.3 of the DWARF 4 spec). */
	Dwarf_Half tag;
	if(dwarf_tag(die, &tag, &error) == DW_DLV_OK &&
		(tag == DW_TAG_subprogram || tag == DW_TAG_inlined_subroutine) &&
		dieContainsAddress(dbg, die, addr, error))
	{
		return die;
	}

	// flow to the next DIE. start with children then move to siblings.
	Dwarf_Die next;

	if(dwarf_child(die, &next, &error) == DW_DLV_OK) {
		Dwarf_Die ret = browseDIE(dbg, addr, next, error);
		if(ret) {
			if(next != ret) {
				dwarf_dealloc(dbg, next, DW_DLA_DIE);
			}
			return ret;
		}
		dwarf_dealloc(dbg, next, DW_DLA_DIE);
	}

	if(dwarf_siblingof_c(die, &next, &error) == DW_DLV_OK) {
		Dwarf_Die ret = browseDIE(dbg, addr, next, error);
		if(ret) {
			if(next != ret) {
				dwarf_dealloc(dbg, next, DW_DLA_DIE);
			}
			return ret;
		}
		dwarf_dealloc(dbg, next, DW_DLA_DIE);
	}

	return 0;
}

// utility function that retrieves the name of a DIE.
bool getName(Dwarf_Debug dbg, Dwarf_Die die, string& ret, Dwarf_Error& error) {
	char* name;
	if(dwarf_diename(die, &name, &error) == DW_DLV_OK) {
		ret = name;
		dwarf_dealloc(dbg, name, DW_DLA_STRING);
		return true;
	}
	return false;
}

// utility function that follows a reference-type attribute (one that points to another DIE).
Dwarf_Die followRef(Dwarf_Debug dbg, Dwarf_Die die, Dwarf_Half attribute, Dwarf_Error& error) {
	Dwarf_Die ret = 0;
	Dwarf_Attribute attr;
	if(dwarf_attr(die, attribute, &attr, &error) == DW_DLV_OK) {
		Dwarf_Off offset = 0;
		Dwarf_Bool isInfo = true;
		if(dwarf_global_formref_b(attr, &offset, &isInfo, &error) == DW_DLV_OK) {
			dwarf_offdie_b(dbg, offset, isInfo, &ret, &error);
		}

		dwarf_dealloc(dbg, attr, DW_DLA_ATTR);
	}
	return ret;
}

/* retrieve the name of the DIE pointed to by the "type" attribute of a DIE. for a function DIE,
this is the return value of the function (section 3.3.2 of the DWARF 2 spec). for an object DIE,
this is the type of that object (section 4.1.4 of the DWARF 2 spec). */
string getType(Dwarf_Debug dbg, Dwarf_Die die, Dwarf_Error& error, bool recursing = false) {
	string ret;

	// follow section 5 of the DWARF 2 spec. multiple type DIEs may be chained together.
	if(recursing && getName(dbg, die, ret, error)) {
		return ret;
	}
	Dwarf_Die type_die = followRef(dbg, die, DW_AT_type, error);
	if(type_die) {
		ret = getType(dbg, type_die, error, true);
		Dwarf_Half tag;
		if(dwarf_tag(die, &tag, &error) == DW_DLV_OK) {
			switch(tag) {
			case DW_TAG_const_type: ret += " const"; break;
			case DW_TAG_pointer_type: ret += '*'; break;
			case DW_TAG_reference_type: ret += '&'; break;
			case DW_TAG_volatile_type: ret += " volatile"; break;
			}
		}
		dwarf_dealloc(dbg, type_die, DW_DLA_DIE);
	}

	if(ret.empty())
		ret = "void/unknown";

	return ret;
}

void getDebugInfo(string path, DWORD_PTR addr, string& file, int& line, int& column, string& function) {
	if(path.empty())
		return;

	// Replace the extension with "pdb". Local release builds keep symbols for
	// DCPlusPlus-stripped.exe in DCPlusPlus.pdb, so also try the unstripped stem.
	auto dot = path.rfind('.');
	if(dot != string::npos)
		path.replace(dot, path.size() - dot, ".pdb");

	// GNU debug companions are PE/COFF images. Native Microsoft PDB files are handled by DbgHelp.
	if(!isPortableExecutable(path)) {
		const string strippedSuffix = "-stripped.pdb";
		if(path.size() <= strippedSuffix.size() ||
			path.compare(path.size() - strippedSuffix.size(), strippedSuffix.size(), strippedSuffix) != 0)
		{
			return;
		}

		path.erase(path.size() - strippedSuffix.size(), strippedSuffix.size());
		path += ".pdb";
		if(!isPortableExecutable(path)) {
			return;
		}
	}

	Dwarf_Debug dbg = nullptr;
	Dwarf_Error error = 0;
	if(dwarf_init_path(path.c_str(), nullptr, 0, DW_GROUPNUMBER_ANY, nullptr, nullptr, &dbg, &error) == DW_DLV_OK) {

		/* use the ".debug_aranges" DWARF section to pinpoint the CU (Compilation Unit) that
		corresponds to the address we want to find information about. */
		Dwarf_Arange* aranges;
		Dwarf_Signed arange_count;
		if(dwarf_get_aranges(dbg, &aranges, &arange_count, &error) == DW_DLV_OK) {

			Dwarf_Arange arange;
			if(dwarf_get_arange(aranges, arange_count, addr, &arange, &error) == DW_DLV_OK) {

				/* great, got a range that matches. let's find the CU it describes, and the DIE
				(Debugging Information Entry) related to that CU. */
				Dwarf_Off die_offset;
				if(dwarf_get_cu_die_offset(arange, &die_offset, &error) == DW_DLV_OK) {

					Dwarf_Die cu_die;
					if(dwarf_offdie_b(dbg, die_offset, 1, &cu_die, &error) == DW_DLV_OK) {

						/* inside this CU, find the exact statement (DWARF calls it a "line") that
						corresponds to the address we want to find information about. */
						Dwarf_Line* lines = nullptr;
						Dwarf_Signed line_count = 0;
						Dwarf_Unsigned line_version = 0;
						Dwarf_Small table_count = 0;
						Dwarf_Line_Context line_context = nullptr;
						if(dwarf_srclines_b(cu_die, &line_version, &table_count, &line_context, &error) == DW_DLV_OK &&
							dwarf_srclines_from_linecontext(line_context, &lines, &line_count, &error) == DW_DLV_OK) {

							/* skim through all available statements to find the one that fits best
							(with an address <= "addr", as close as possible to "addr"). */
							Dwarf_Line best = 0;
							Dwarf_Unsigned delta = static_cast<Dwarf_Unsigned>(-1);
							for(Dwarf_Signed i = 0; i < line_count; ++i) {
								auto& l = lines[i];
								Dwarf_Addr lineaddr;
								Dwarf_Bool endSequence = false;
								if(dwarf_lineendsequence(l, &endSequence, &error) == DW_DLV_OK && !endSequence &&
									dwarf_lineaddr(l, &lineaddr, &error) == DW_DLV_OK && addr >= lineaddr)
								{
									Dwarf_Unsigned d = addr - lineaddr;
									if(d < delta) {
										best = l;
										if(d == 0) // found a perfect match.
											break;
										delta = d;
									}
								}
							}

							if(best) {
								// get the source file behind this statement.
								char* linesrc;
								if(dwarf_linesrc(best, &linesrc, &error) == DW_DLV_OK) {
									file = linesrc;
									dwarf_dealloc(dbg, linesrc, DW_DLA_STRING);

									// get the line number inside that source file.
									Dwarf_Unsigned lineno;
									if(dwarf_lineno(best, &lineno, &error) == DW_DLV_OK) {
										line = lineno;

										// get the column number as well if available.
										Dwarf_Unsigned lineoff;
										if(dwarf_lineoff_b(best, &lineoff, &error) == DW_DLV_OK) {
											column = static_cast<int>(lineoff);
										}
									}
								}
							}

							dwarf_srclines_dealloc_b(line_context);
						}

						if(file.empty()) {
							/* could not get a precise statement within this CU; resort to showing
							the global name of this CU's DIE which, according to section 3.1 of the
							DWARF 2 spec, is almost what we want. */
							getName(dbg, cu_die, file, error);
						}

						// Function-level DIE traversal is intentionally skipped here. On some
						// MinGW DWARF images this walk can fault inside the crash handler,
						// preventing any stack trace output. File/line data is still reported.

						dwarf_dealloc(dbg, cu_die, DW_DLA_DIE);
					}

				}

			}

			for(Dwarf_Signed i = 0; i < arange_count; ++i) {
				dwarf_dealloc(dbg, aranges[i], DW_DLA_ARANGE);
			}
			dwarf_dealloc(dbg, aranges, DW_DLA_LIST);
		}

		if(error) {
			fprintf(f, "[libdwarf error: %s] ", dwarf_errmsg(error));
			dwarf_dealloc_error(dbg, error);
			error = nullptr;
		}
		dwarf_finish(dbg);
	}

	if(error) {
		fprintf(f, "[libdwarf error: %s] ", dwarf_errmsg(error));
		dwarf_dealloc_error(nullptr, error);
	}
}

/* although the 64-bit functions should just map to the 32-bit ones on a 32-bit OS, this seems to
fail when compiling the x86 build. */
#ifndef _WIN64
#define DWORD64 DWORD
#define IMAGEHLP_LINE64 IMAGEHLP_LINE
#define IMAGEHLP_MODULE64 IMAGEHLP_MODULE
#define IMAGEHLP_SYMBOL64 IMAGEHLP_SYMBOL
#define STACKFRAME64 STACKFRAME
#define StackWalk64 StackWalk
#define SymFunctionTableAccess64 SymFunctionTableAccess
#define SymGetLineFromAddr64 SymGetLineFromAddr
#define SymGetModuleBase64 SymGetModuleBase
#define SymGetModuleInfo64 SymGetModuleInfo
#define SymGetSymFromAddr64 SymGetSymFromAddr
#endif

#elif defined(_MSC_VER)

#include <dbghelp.h>

// MSVC uses SEH. Nothing special to add besides that include file.

#else

#define NO_BACKTRACE

#endif

inline void writeAppInfo() {
	fputs(Util::formatTime(APPNAME " has crashed on %Y-%m-%d at %H:%M:%S.\n", time(0)).c_str(), f);
	fputs("Please report this data to the " APPNAME " team for further investigation.\n\n", f);

	fprintf(f, APPNAME " version: %s\n", fullVersionString.c_str());
	fprintf(f, "TTH: %S\n", WinUtil::tth.c_str());

	// see also AboutDlg.cpp for similar tests.
#ifdef __MINGW64_VERSION_MAJOR
	fputs("Compiled with MinGW-w64's GCC " __VERSION__, f);
#elif defined(_MSC_VER)
	fprintf(f, "Compiled with MS Visual Studio %d", _MSC_VER);
#else
	fputs(f, "Compiled with an unknown compiler");
#endif
#ifdef _DEBUG
	fputs(" (debug)", f);
#endif
#ifdef _WIN64
	fputs(" (x64)", f);
#endif
	fputs("\n", f);
}

inline void writePlatformInfo() {
	OSVERSIONINFOEX ver = { sizeof(OSVERSIONINFOEX) };
	if(!::GetVersionEx(reinterpret_cast<LPOSVERSIONINFO>(&ver)))
		ver.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);

	if(::GetVersionEx(reinterpret_cast<LPOSVERSIONINFO>(&ver))) {
		fprintf(f, "Windows version: major = %lu, minor = %lu, build = %lu, SP = %u, type = %u\n",
			ver.dwMajorVersion, ver.dwMinorVersion, ver.dwBuildNumber, ver.wServicePackMajor, ver.wProductType);

	} else {
		fputs("Windows version: unknown\n", f);
	}

	SYSTEM_INFO info;
	::GetNativeSystemInfo(&info);
	fprintf(f, "Processors: %lu * %s\n", info.dwNumberOfProcessors,
		(info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) ? "x64" :
		(info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL) ? "x86" :
		(info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_IA64) ? "ia64" :
		"[unknown architecture]");

	MEMORYSTATUSEX memoryStatusEx = { sizeof(MEMORYSTATUSEX) };
	::GlobalMemoryStatusEx(&memoryStatusEx);
	fprintf(f, "System memory installed: %s", Util::formatBytes(memoryStatusEx.ullTotalPhys).c_str());
}

#ifndef NO_BACKTRACE

inline DWORD64 getPreferredImageBase(const char* path) {
	std::ifstream file(path, std::ios::binary);
	if(!file) {
		return 0;
	}

	IMAGE_DOS_HEADER dosHeader = {};
	if(!file.read(reinterpret_cast<char*>(&dosHeader), sizeof(dosHeader)) ||
		dosHeader.e_magic != IMAGE_DOS_SIGNATURE || dosHeader.e_lfanew < 0)
	{
		return 0;
	}

	file.seekg(dosHeader.e_lfanew, std::ios::beg);
	DWORD signature = 0;
	IMAGE_FILE_HEADER fileHeader = {};
	if(!file.read(reinterpret_cast<char*>(&signature), sizeof(signature)) ||
		signature != IMAGE_NT_SIGNATURE ||
		!file.read(reinterpret_cast<char*>(&fileHeader), sizeof(fileHeader)))
	{
		return 0;
	}

	const auto optionalHeaderOffset = file.tellg();
	WORD magic = 0;
	if(!file.read(reinterpret_cast<char*>(&magic), sizeof(magic))) {
		return 0;
	}
	file.seekg(optionalHeaderOffset, std::ios::beg);

	if(magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC && fileHeader.SizeOfOptionalHeader >= sizeof(IMAGE_OPTIONAL_HEADER32)) {
		IMAGE_OPTIONAL_HEADER32 optionalHeader = {};
		if(file.read(reinterpret_cast<char*>(&optionalHeader), sizeof(optionalHeader))) {
			return optionalHeader.ImageBase;
		}
	} else if(magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC && fileHeader.SizeOfOptionalHeader >= sizeof(IMAGE_OPTIONAL_HEADER64)) {
		IMAGE_OPTIONAL_HEADER64 optionalHeader = {};
		if(file.read(reinterpret_cast<char*>(&optionalHeader), sizeof(optionalHeader))) {
			return optionalHeader.ImageBase;
		}
	}
	return 0;
}

inline void writeBacktrace(LPCONTEXT context) {
	HANDLE const process = GetCurrentProcess();
	HANDLE const thread = GetCurrentThread();
	CONTEXT walkContext = *context;

#if defined(__MINGW32__)
	SymSetOptions(SYMOPT_DEFERRED_LOADS);
#else
	SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
#endif

	if(!SymInitialize(process, 0, TRUE)) {
		fprintf(f, "Failed to initialize the symbol handler (error: %lu)\n", GetLastError());
		return;
	}

	STACKFRAME64 frame;
	memset(&frame, 0, sizeof(frame));

	frame.AddrPC.Mode = AddrModeFlat;
	frame.AddrFrame.Mode = AddrModeFlat;
	frame.AddrStack.Mode = AddrModeFlat;

#ifdef _WIN64
	frame.AddrPC.Offset = walkContext.Rip;
	frame.AddrFrame.Offset = walkContext.Rbp;
	frame.AddrStack.Offset = walkContext.Rsp;

#define WALK_ARCH IMAGE_FILE_MACHINE_AMD64

#else
	frame.AddrPC.Offset = walkContext.Eip;
	frame.AddrFrame.Offset = walkContext.Ebp;
	frame.AddrStack.Offset = walkContext.Esp;

#define WALK_ARCH IMAGE_FILE_MACHINE_I386

#endif

	char symbolBuf[sizeof(IMAGEHLP_SYMBOL64) + 255];

	for(uint8_t step = 0; step < 128; ++step) { // 128 steps max to avoid too long traces
		const DWORD64 currentAddr = frame.AddrPC.Offset;
		if(!currentAddr) {
			break;
		}

		/* in case something unexpected happens when reading the next address, we want to at least
		record the information that has been gathered so far. */
		fflush(f);

		string file;
		int line = -1;
		int column = -1;
		string function;

		IMAGEHLP_MODULE64 module = { sizeof(IMAGEHLP_MODULE64) };
		const bool hasModule = SymGetModuleInfo64(process, currentAddr, &module) == TRUE;
		fprintf(f, "%s: ", hasModule ? module.ModuleName : "?");

#if defined(__MINGW32__)
		// @todo add check for pdb file format, skip DWARF read if we have a MS format symbols file (made by e.g. cv2pdb)
		// to avoid printing errors to the crashlog

		// read DWARF debugging info if available.
		if(hasModule && (module.LoadedImageName[0] ||
			// LoadedImageName is not always correctly filled in XP... @todo test whether we can safely remove this
			::GetModuleFileNameA(reinterpret_cast<HMODULE>(module.BaseOfImage), module.LoadedImageName, sizeof(module.LoadedImageName))))
		{
			const DWORD64 preferredImageBase = getPreferredImageBase(module.LoadedImageName);
			const bool hasModuleOffset = currentAddr >= module.BaseOfImage;
			const DWORD64 moduleOffset = hasModuleOffset ? (currentAddr - module.BaseOfImage) : 0;

			auto tryDwarf = [&](DWORD64 candidateAddress) {
				if(file.empty() && line < 0 && function.empty()) {
					getDebugInfo(module.LoadedImageName, static_cast<DWORD_PTR>(candidateAddress), file, line, column, function);
				}
			};

			if(preferredImageBase && hasModuleOffset) {
				tryDwarf(preferredImageBase + moduleOffset);
			}
			tryDwarf(currentAddr);
			if(hasModuleOffset) {
				tryDwarf(moduleOffset);
			}
		}
#endif

		/* this is the usual Windows PDB reading method. we try it on MinGW too if reading DWARF
		data has failed, just in case Windows can extract some information. */
		if(file.empty()) {
			IMAGEHLP_SYMBOL64* symbol = reinterpret_cast<IMAGEHLP_SYMBOL64*>(symbolBuf);
			symbol->SizeOfStruct = sizeof(IMAGEHLP_SYMBOL64);
			symbol->MaxNameLength = 254;
			if(SymGetSymFromAddr64(process, currentAddr, 0, symbol)) {
				file = symbol->Name;
			}

			IMAGEHLP_LINE64 info = { sizeof(IMAGEHLP_LINE64) };
			DWORD col;
			if(SymGetLineFromAddr64(process, currentAddr, &col, &info)) {
				function = file;
				file = info.FileName;
				line = info.LineNumber;
				column = col;
			}
		}

		// write the data collected about this frame to the file.
		fprintf(f, "%s", file.empty() ? "?" : file.c_str());
		if(line >= 0) {
			fprintf(f, " (%d", line);
			if(column >= 0) {
				fprintf(f, ":%d", column);
			}
			fputs(")", f);
		}
		if(!function.empty()) {
			fprintf(f, ", function: %s", function.c_str());
		}
		fputs("\n", f);

		if(!StackWalk64(WALK_ARCH, process, thread, &frame, &walkContext,
			0, SymFunctionTableAccess64, SymGetModuleBase64, 0)) { break; }
	}

	SymCleanup(process);
}

#endif // NO_BACKTRACE

LONG WINAPI exceptionFilter(LPEXCEPTION_POINTERS info) {
	if(f) {
		// Avoid re-entering the logger if symbolization faults; let normal crash handling continue.
		return EXCEPTION_CONTINUE_SEARCH;
	}

	f = _wfopen(CrashLogger::getPath().c_str(), L"w");
	if(f) {
		writeAppInfo();

		fprintf(f, "Exception code: %lx\n", info->ExceptionRecord->ExceptionCode);

		writePlatformInfo();

#ifdef NO_BACKTRACE
		fputs("\nStack trace unavailable: this program hasn't been compiled with backtrace support\n", f);
#else
		fputs("\nWriting the stack trace...\n\n", f);
		writeBacktrace(info->ContextRecord);
#endif

		fputs("\nInformation about the crash has been written.\n", f);
		fflush(f); //Make sure all the contents are written before the crash dialog appears
		fclose(f);
	}
	return EXCEPTION_CONTINUE_SEARCH;
}

LPTOP_LEVEL_EXCEPTION_FILTER prevFilter;

} // unnamed namespace

CrashLogger::CrashLogger() {
	prevFilter = SetUnhandledExceptionFilter(exceptionFilter);
}

CrashLogger::~CrashLogger() {
	SetUnhandledExceptionFilter(prevFilter);
}

tstring CrashLogger::getPath() {
	return Text::toT(Util::getPath(Util::PATH_USER_LOCAL)) + _T("CrashLog.txt");
}

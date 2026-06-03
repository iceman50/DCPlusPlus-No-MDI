/*
  DC++ Widget Toolkit

  Copyright (c) 2007-2026, Jacek Sieka

  All rights reserved.

  Redistribution and use in source and binary forms, with or without modification,
  are permitted provided that the following conditions are met:

      * Redistributions of source code must retain the above copyright notice,
        this list of conditions and the following disclaimer.
      * Redistributions in binary form must reproduce the above copyright notice,
        this list of conditions and the following disclaimer in the documentation
        and/or other materials provided with the distribution.
      * Neither the name of the DWT nor SmartWin++ nor the names of its contributors
        may be used to endorse or promote products derived from this software
        without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
  INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <dwt/util/win32/Version.h>

#include <dwt/WindowsHeaders.h>

namespace dwt { namespace util { namespace win32 {

bool ensureVersion(Version version) {
	static DWORD major = 0;
	static DWORD minor = 0;
	static DWORD build = 0;

	// Initialize version info once
	if (major == 0) {
		OSVERSIONINFOEX ver = { sizeof(OSVERSIONINFOEX) };
#pragma warning(push)
#pragma warning(disable: 4996) // GetVersionEx deprecated; no VerifyVersionInfo alternative for exact version
		if (!::GetVersionEx(reinterpret_cast<LPOSVERSIONINFO>(&ver)))
			ver.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);

		if (::GetVersionEx(reinterpret_cast<LPOSVERSIONINFO>(&ver))) {
			major = ver.dwMajorVersion;
			minor = ver.dwMinorVersion;
			build = ver.dwBuildNumber;
		}
#pragma warning(pop)
	}

	// Version requirement lookup table: { requiredMajor, requiredMinor, requiredBuild }
	struct VersionInfo {
		DWORD reqMajor;
		DWORD reqMinor;
		DWORD reqBuild;
	};

	static const VersionInfo requirements[] = {
		/* XP */           { 5, 1, 0 },
		/* VISTA */        { 6, 0, 0 },
		/* SEVEN */        { 6, 1, 0 },
		/* EIGHT */        { 6, 2, 0 },
		/* EIGHT_ONE */    { 6, 3, 0 },
		/* TEN */          { 10, 0, 0 },
		/* TEN_1909 */     { 10, 0, 18363 },
		/* TEN_2004 */     { 10, 0, 19041 },
		/* TEN_20H2 */     { 10, 0, 19042 },
		/* TEN_21H1 */     { 10, 0, 19043 },
		/* TEN_21H2 */     { 10, 0, 19044 },
		/* TEN_22H2 */     { 10, 0, 19045 },
		/* ELEVEN */       { 10, 0, 22000 },
		/* ELEVEN_21H2 */  { 10, 0, 22000 },
		/* ELEVEN_22H2 */  { 10, 0, 22621 },
		/* ELEVEN_23H2 */  { 10, 0, 22631 },
		/* ELEVEN_24H2 */  { 10, 0, 26100 },
		/* ELEVEN_25H2 */  { 10, 0, 27700 },
		/* ELEVEN_26H2 */  { 10, 0, 28600 },
	};

	// Bounds check
	if (version < 0 || version >= static_cast<int>(_countof(requirements)))
		return false;

	// Compare against requirement: system version >= required version
	const VersionInfo& req = requirements[version];
	return major > req.reqMajor ||
		(major == req.reqMajor && minor > req.reqMinor) ||
		(major == req.reqMajor && minor == req.reqMinor && build >= req.reqBuild);
}

} } }

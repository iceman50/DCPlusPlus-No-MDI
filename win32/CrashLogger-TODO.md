# CrashLogger Repair TODO

Audit date: 2026-06-15

This file tracks follow-up work identified while reviewing `CrashLogger.cpp`
and bundled libdwarf 2.3.2 for MinGW-w64 Windows builds.

## High Priority

- [ ] Make exception-handler re-entry safe.
  - Replace the global `FILE*` re-entry check with an atomic logging state.
  - Return `EXCEPTION_CONTINUE_SEARCH` for recursive or concurrent exceptions.
  - Clear or close state consistently if opening or writing the log fails.
  - Never return `EXCEPTION_CONTINUE_EXECUTION` for an unhandled crash.

- [x] Replace recursive DIE sibling traversal.
  - `browseDIE` currently consumes one C++ stack frame per sibling.
  - A tested DWARF 5 image contained a CU with 3,821 direct children.
  - Traverse siblings iteratively and reserve recursion only for child depth,
    or use an explicit work stack with a defensive entry limit.
  - Ensure every acquired `Dwarf_Die` is deallocated exactly once.

## Medium Priority

- [ ] Preserve the original exception context.
  - Copy `*info->ContextRecord` before calling `StackWalk64`.
  - Pass the local copy because stack walking may modify the context.
  - Leave the original context intact for later exception handlers and Windows
    Error Reporting.

- [x] Improve optimized and inline function resolution.
  - Search child DIEs before returning an enclosing `DW_TAG_subprogram`.
  - Select the most deeply nested matching `DW_TAG_inlined_subroutine`.
  - Follow `DW_AT_abstract_origin` as well as `DW_AT_specification`.
  - Add cycle and depth protection while following DIE references.

- [ ] Support Unicode module and debug-file paths.
  - Replace `GetModuleFileNameA` and narrow-path file access with wide paths.
  - Open the GNU debug companion with `_wopen`.
  - Initialize libdwarf from the resulting file descriptor with `dwarf_init_b`.
  - Test installation paths containing characters outside the active ANSI
    code page.

- [ ] Make DbgHelp use safe across threads.
  - Serialize `SymInitialize`, stack walking, symbol queries, and `SymCleanup`.
  - DbgHelp APIs are documented as single-threaded.
  - Coordinate this with the exception-handler re-entry guard to avoid waiting
    indefinitely if another crashing thread owns the lock.

## Low Priority

- [x] Align the DbgHelp symbol buffer explicitly.
  - Add `alignas(IMAGEHLP_SYMBOL64)` to the backing byte buffer.
  - Prefer `SYMBOL_INFO` and `SymFromAddr` where supported.

- [ ] Correct DbgHelp line displacement reporting.
  - The value returned by `SymGetLineFromAddr64` is a byte displacement from
    the source line, not a source column.
  - Do not print it as `line:column`; label it as displacement or omit it.

- [ ] Include the faulting frame in the trace.
  - Verify whether the current loop prints only frames returned after the first
    `StackWalk64` call.
  - If so, symbolize the initial program counter before advancing the walk.

- [ ] Improve GNU debug-companion discovery.
  - Do not assume every companion is produced by replacing the module
    extension with `.pdb`.
  - Consider `.gnu_debuglink` and configurable symbol locations.
  - Continue rejecting native Microsoft PDB files before passing them to
    libdwarf.

- [ ] Reduce work performed inside the unhandled exception filter.
  - Avoid unnecessary C++ stream, heap, locale, and UI-dependent operations
    after process state may already be corrupted.
  - Consider writing a minimal crash record first, then symbolizing it in a
    helper process.

## Libdwarf Follow-Up

- [ ] Record the exact upstream libdwarf source revision.
  - The bundled headers identify the source as version 2.3.2.
  - Keep the upstream commit and any local patches documented in
    `dwarf/readme.txt`.

- [ ] Add a repeatable MinGW-w64 DWARF smoke test.
  - Build PE32 and PE32+ fixtures with DWARF 4 and DWARF 5.
  - Extract GNU debug companions with `strip --only-keep-debug`.
  - Verify `.debug_aranges`, CU DIE lookup, line tables, range lists, function
    names, ASLR address rebasing, and Unicode paths.
  - Run malformed and truncated PE fixtures through libdwarf as negative tests.

## Verified During Audit

- [x] Bundled libdwarf identifies itself as version 2.3.2.
- [x] PE32+ DWARF 5 debug data with 8-byte addresses loads successfully.
- [x] All 35 compilation units referenced by `.debug_aranges` resolved.
- [x] Line tables loaded for 34 applicable compilation units.
- [x] Twenty-four tested CU DIE offsets were not four-byte aligned.
- [x] Libdwarf uses checked unaligned reads and advances CUs by encoded length.
- [x] The deleted fixed four-byte CU-alignment patch should remain deleted.
- [x] Preferred-image-base rebasing is appropriate for ASLR-loaded modules.

## Repair Verification

Before considering this work complete:

- [ ] Build and test MinGW-w64 x86 and x64 configurations.
- [ ] Trigger access violations, stack overflows, and crashes from two threads.
- [ ] Test with ASLR enabled and with the module relocated.
- [ ] Test optimized code containing nested inline functions.
- [ ] Test missing, native PDB, malformed, and valid GNU `.pdb` companions.
- [ ] Confirm another installed exception handler receives an unchanged context.
- [ ] Confirm crash logging itself cannot cause an execution-resume loop.

# AGENTS.md

## Scope
- Applies to the entire repository.
- Goal: deliver safe, minimal, testable changes to `KNSoft.SlimDetours`.

## Quick Start (Read First)
- `README.md`
- `Source/KNSoft.SlimDetours/SlimDetours.h`
- `Source/KNSoft.SlimDetours/InlineHook.c`
- `Source/KNSoft.SlimDetours/Transaction.c`
- `Source/KNSoft.SlimDetours/Thread.c`
- `Source/KNSoft.SlimDetours/Memory.c`
- `Source/KNSoft.SlimDetours/Trampoline.c`
- `Source/Demo/Main.c`
- `.github/workflows/Build_Publish.yml`

## Hard Rules
- Follow `.editorconfig`.
- Preserve original file encoding and line-ending style when editing files.
- Keep diffs minimal; do not refactor unrelated code.
- `Source/Microsoft.Detours/` is bundled for NuGet packaging only; never modify it.
- Use concise, technical comments only when needed.

## Build and Validate
- Build:
  - `msbuild .\Source\KNSoft.SlimDetours.sln /restore /m /p:Configuration=Debug /p:Platform=x64 /p:RestorePackagesConfig=true`
- Run demo tests:
  - `.\Source\OutDir\x64\Debug\Demo.exe -Run`
  - `.\Source\OutDir\x64\Debug\Demo.exe -Run DeadLock -Engine=SlimDetours`

## Change Routing
- Hook transaction semantics: `Transaction.c`, `InlineHook.c`, `SlimDetours.h`
- Thread suspend/resume/context update: `Thread.c`
- Trampoline allocation/address-range logic: `Trampoline.c`, `Memory.c`
- Public API/signature changes: `SlimDetours.h`
- Packaging/consumption: `KNSoft.SlimDetours.nuspec`, `KNSoft.SlimDetours.targets`

## Before Finishing
- Verify both hook and unhook paths still work.
- Check platform/config impact (`x86`, `x64`, `ARM64`, `ARM64EC`) for touched logic.
- If tests were not run, state that explicitly.

# ext2 file system simulator

An ext2-style file system in C that runs on a simulated 256 MiB disk held in memory, with a small shell (`format`, `mount`, `mkdir`, `touch`, `fill`, `cat`, `ls`, `cd`, `rm`, `rmdir`) to drive it. It shows how a block file system is laid out: superblock, block groups, bitmaps, inodes and directory entries.

This is a 2026 rewrite of an open-source ext2 emulator (see [Credits](#credits)):

- **11 bugs found and fixed**, each with a regression test, from an allocator that filled the 256 MiB disk at 1.5 MiB to `rm` writing into another file's data.
- **Tested beyond unit tests**: a fuzzer that checks every operation against an in-memory model, fault injection at each disk operation, and an `e2fsck`-style consistency check after every test.
- **Modern C23 and CMake**: split into small modules, `-Werror` with `-Wconversion`, sanitizers, OpenSSF hardening flags, and CI with GCC and Clang, `-fanalyzer`, clang-tidy and coverage.

It follows ext2's structure but is not compatible with it on disk: directory entries are fixed 32-byte records with 8.3 names, and files use direct blocks only.

## Bugs found and fixed

- Allocation scanned only the first 2,048 bits of group 0's bitmaps, so the 256 MiB disk was full after about 1,500 blocks. It now searches every group, and running out is reported instead of returning block 0.
- `rm` and `rmdir` located the block bitmap through a macro without outer parentheses and flipped a bit inside a data block. The freed block was never released, while the superblock counted it as free.
- Bits were cleared with XOR, so freeing a block twice marked it used again. Freeing now refuses a block that is already free.
- A directory that filled its first block got a new block that was never linked to its inode, so entries past the 29th disappeared from `ls` and `lookup`.
- `cat` crashed: the read path passed the file system where the disk was expected. Reads and writes now handle any offset and length, and `fill` stores the bytes it is given.
- The superblock struct was 1,020 bytes but copied as 1,024, overwriting the next field. Struct sizes are now checked at compile time.
- The root inode was built from uninitialized stack memory, format wrote a single inode bitmap at a group picked by an uninitialized variable, and names longer than 8.3 overflowed a buffer.
- `exit` called `_exit`, losing buffered output, and end of input made the shell repeat the last command forever.

## Layout

The disk is 262,145 blocks of 1 KiB, split into 32 groups of 8,192 blocks. A group is 8,192 blocks because one 1 KiB bitmap block tracks 8 × 1024 blocks. Every group starts the same way:

| Block in group | Contents |
|---|---|
| 0 | Boot block (unused) |
| 1 | Superblock (copy; group 0 holds the primary) |
| 2 | Group descriptor table (copy) |
| 3 | Block bitmap |
| 4 | Inode bitmap |
| 5–516 | Inode table: 4,096 inodes × 128 bytes |
| 517–8191 | Data blocks |

The root directory is inode 2; inodes 1–10 are reserved, as in ext2. Each inode has 12 direct block pointers, so a file holds at most 12 KiB, and a directory at most 382 entries (12 blocks × 32 slots, minus `.` and `..`).

## Source layout

| Path | Contents |
|---|---|
| `src/disk/` | The simulated disk: sectors in memory behind a read/write interface |
| `src/ext2/layout.h` | On-disk format: geometry, group layout, superblock, inode and entry structs |
| `src/ext2/ext2.h` | Public API used by the shell and the tests |
| `src/ext2/*.c` | `block` I/O, `alloc`ation and free counts, `inode`s, `dir`ectory entries, 8.3 `name`s, create/remove (`node`), read/write (`file`), `format` and mount |
| `src/shell/` | The interactive shell and its adapter to the ext2 API |
| `tests/` | Unit tests, fault injection, the fuzzer, a scripted shell session and an `e2fsck`-style checker |

## Build and test

Needs CMake 3.25+, Ninja and a C23 compiler (GCC 14+ or Clang 18+).

```sh
cmake --preset debug          # Debug with AddressSanitizer and UBSan
cmake --build --preset debug
ctest --preset debug

./build/debug/ext2-shell
```

| Preset | For |
|---|---|
| `debug` | Development: AddressSanitizer and UndefinedBehaviorSanitizer |
| `release` | Optimized build with the hardening flags below |
| `coverage` | Clang source-based coverage (gcov with GCC) |
| `fuzz` | libFuzzer target; needs Clang with compiler-rt (`CC=clang`, on Linux) |

Warnings are errors (`-Wall -Wextra -Wpedantic -Wconversion` and more); pass `-DEXT2SIM_WERROR=OFF` to relax that. Functions that return a status are `[[nodiscard]]`.

### Tests

- `tests/test_ext2.c`: the library, with a regression test for each fixed bug. Every test ends by recounting all bitmaps and comparing them with the superblock and group descriptors (what `e2fsck` does in pass 5; see `tests/fsck.c`).
- `tests/test_faults.c`: fault injection. The disk fails at each of a scenario's sector operations in turn, and every call must report the error without crashing.
- `tests/fuzz_ext2.c`: reads its input as a sequence of operations, compares every result with an in-memory model and checks consistency after each step. Under ctest it replays `tests/corpus/` and 200 random inputs; with the `fuzz` preset it is a libFuzzer target:

  ```sh
  CC=clang cmake --preset fuzz && cmake --build --preset fuzz
  ./build/fuzz/fuzz_ext2 tests/corpus -max_total_time=60
  ```

- `tests/shell_session.txt`: a scripted shell session whose output must match `tests/shell_session.expected`.

Coverage report (macOS shown; on Linux use `llvm-profdata-18` and `llvm-cov-18`):

```sh
cmake --preset coverage && cmake --build --preset coverage && ctest --preset coverage
cd build/coverage
xcrun llvm-profdata merge -sparse profiles/*.profraw -o all.profdata
xcrun llvm-cov report ./ext2-shell -object ./test_ext2 -object ./test_faults -object ./fuzz_ext2 \
  -instr-profile=all.profdata -ignore-filename-regex='tests/'
```

### Static analysis and hardening

CI also runs GCC's `-fanalyzer`, `clang-tidy` (checks in `.clang-tidy`) and `clang-format`. Builds add the flags from the [OpenSSF Compiler Options Hardening Guide](https://best.openssf.org/Compiler-Hardening-Guides/Compiler-Options-Hardening-Guide-for-C-and-C++.html) that the toolchain supports: `-D_FORTIFY_SOURCE=3` (optimized builds), `-fstack-protector-strong`, `-fstack-clash-protection`, `-ftrivial-auto-var-init=zero`, `-fcf-protection=full` or `-mbranch-protection=standard`, `-fstrict-flex-arrays=3`, PIE and full RELRO.

## Shell

```text
ext2 : [/]# format
ext2 : [/]# mount
ext2 : [/]# mkdir docs
ext2 : [/]# fill big.txt 3000
ext2 : [/]# ls
[File names] [D] [File sizes]
DOCS          1          1024
BIG.TXT       0          3000
```

| Command | Does |
|---|---|
| `format`, `mount`, `umount` | Format the disk, mount or unmount it |
| `ls`, `cd [dir]` | List or change the current directory |
| `mkdir name`, `rmdir name` | Create or remove an empty directory |
| `touch names...`, `rm names...` | Create empty files or remove files |
| `fill name size` | Create a file of `size` bytes of repeated text |
| `cat name` | Print a file |
| `exit` | Quit (end of input also quits) |

Names are 8.3: up to 8 letters or digits, optionally a dot and up to 3 more, stored in upper case.

## Credits

- The shell, disk simulator, entry list and 8.3 directory entries come from Kyoungmoon Sun's FAT12/16 teaching framework (Dankook University, 2008), the example code for chapter 10 of 「리눅스 커널 내부구조」 ([archived copy](https://github.com/JSYoo5B/TIL/tree/master/Kernel/LKI/chap10/example/4_FAT)).
- The ext2 implementation this rewrite started from is Yoonje Choi's [ext2-linux-file-system](https://github.com/yoonje/ext2-linux-file-system) (2019); the first commit here is a copy of it.

The on-disk layout, structures and API follow these works.

## License

No license is granted: the original works carry none, so their authors keep all rights. Shared for reference.

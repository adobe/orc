# ORC 

[![ORC Build and Test](https://github.com/adobe/orc/actions/workflows/build-and-test.yml/badge.svg?event=push)](https://github.com/adobe/orc/actions/workflows/build-and-test.yml)

ORC is a tool for finding violations of C++'s One Definition Rule on the OSX toolchain.

ORC is a play on [DWARF](http://dwarfstd.org/) which is a play on [ELF](https://en.wikipedia.org/wiki/Executable_and_Linkable_Format). ORC is an acronym; while the _O_ stands for ODR, in a bout of irony the _R_ and _C_ represent multiple (possibly conflicting) words.

# The One Definiton Rule

## What is it?

There are [many](https://en.wikipedia.org/wiki/One_Definition_Rule) [writeups](https://en.cppreference.com/w/cpp/language/definition) about the One Definition Rule (ODR), including the [C++ Standard itself](https://eel.is/c++draft/basic.def.odr). The gist of the rule is that if a symbol is defined in a program, it is only allowed to be defined once. Some symbols are granted an exception to this rule, and are allowed to be defined multiple times. However, those symbols must be defined by _identical_ token sequences.

Note that some compiler settings can also affect token sequences - for example, RTTI being enabled or disabled may alter the definition of a symbol (in this case, a class' vtable.)

## What is an ODR violation?

Any symbol that breaks the above rule is an ODR violation (ODRV). In some instances, the linker may catch the duplicate symbol definition and emit a warning or error. However, the Standard states a linker is not required to do so. [Andy G](https://gieseanw.wordpress.com/) describes it well:

> for performance reasons, the C++ Standard dictates that if you violate the One Definition Rule with respect to templates, the behavior is simply undefined. Since the linker doesn't care, violations of this rule are silent. [source](https://gieseanw.wordpress.com/2018/10/30/oops-i-violated-odr-again/)

Non-template ODRVs are possible, and the linker may be equally silent about them, too.

## Why are ODRVs bad?

An ODRV usually means you have a symbol whose binary layout differs depending on the compilation unit that built it. Because of the rule, however, when a linker encounters multiple definitions, it is free to pick _any_ of them and use it as the binary layout for a symbol. When the picked layout doesn't match the internal binary layout of the symbol in a compilation unit, the behavior is undefined.

Oftentimes the debugger is useless in these scenarios. It, too, will be using a single definition of a symbol for the entire program, and when you try to debug an ODRV the debugger may give you bad data, or point to a location in a file that doesn't seem correct. In the end, the debugger will appear to be lying to you, yet silently offer no clues as to what the underlying issue is.

## Why should you fix an ODR?

Like all bugs, ODRVs take time to fix, so why should you fix an ODR violation in tested (and presumably working) code?

* It can be difficult to know if an ODRV is causing a crash. The impact of an ODRV sometimes isn’t local to the location of the ODRV code. Stack corruption is a common symptom of ODRVs, and that can happen later and far away from the actual incorrect code.
* The code actually generated is dependent on the inputs to the linker. Changing the linker inputs can cause different behaviors. And linker input changes can be caused by intentional reordering by a programmer, the output of a project generator changing, or as a simple by-product of adding files to your project.

# How ORC works

ORC is a tool that performs the following:

* Reads in a set of object and archive files (including libraries and frameworks)
* Scans the object files for DWARF debug data, registering every type used by the component being built.
* Detects and reports inconsistencies that are classified as ODRVs

Barring a bug in the tool, ORC does not generate false positives. Anything it reports is an ODRV.

At this time, ORC does not detect all possible violations of the One Definition Rule. We hope to expand and improve on what it can catch over time. Until then, this means that while ORC is a valuable check, a clean scan does not guarantee a program is free of ODRVs.

ORC can find:

* structures / classes that aren't the same size
* members of structures / classes that aren't at the same location
* mis-matched vtables

A note on vtables: ORC will detect virtual methods that are in different slots. (Which is a nastly sort of corrupt program.) At this point, it won't detect a class that has a virtual methods that are a "superset" of a ODR violating duplicate class. 

# The ORC project

In addition to the main ORC sources, we try to provide a bevy of example applications that contain ODRVs that the tool should catch.

ORC was originally conceived on macOS. While its current implementation is focused there, it does not have to be constrained to that toolchain.

## Building ORC

ORC is managed by cmake, and is built using the typical build conventions of a CMake-managed project:

1. clone the repository
2. within the repository folder:
   1. `mkdir build`
   2. `cd build`
   3. `cmake -GXcode ..`
3. Open the generated Xcode project, build, and you're all set.

There are a handful of sample applications that ORC is integrated into for the purposes of testing. Those can be selected via the targets popup in Xcode.

## Calling ORC

ORC can be called directly from the command line, or inserted into the tool chain in the linker step. The output is unchanged; it's simply a matter
of convenience in your workflow.

### Command Line

#### Linker arguments

This mode is useful if you have the linker command and its arguments, and want to search for ODRVs seperate from the actual build.

Config file (see below)
* `'forward_to_linker' = false`
* `'standalone_mode' = false`

You need the `ld` command line arguments from XCode. Build with Xcode, (if you can't link, ORC can't help), copy the link command, and paste it after the ORC invocation. Something like:

`/path/to/orc /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang++ -target ... Debug/lem_mac`

(It's a huge command line, abbreviated here.)

ORC will execute, and log ODR violations to the console.

#### List of Library files

If you have a list of library files for ORC to process, it can do that as well.

Config file (see below)
* `'forward_to_linker' = false`
* `'standalone_mode' = true`

In this mode, simply pass a list of libary files to ORC to process. 

### Linker

Config file (see below)
* `'forward_to_linker' = true`
* `'standalone_mode' = false`

To use ORC within your Xcode build project, override the following variables with a fully-qualified path to the ORC scripts:

```
"LIBTOOL": "/absolute/path/to/orc",
"LDTOOL": "/absolute/path/to/orc",
"ALTERNATE_LINKER": "/absolute/path/to/orc",
```

With those settings in place, Xcode should use ORC as the tool for both `libtool` and `ld` phases of the project build. Because of the `forward_to_linker` setting, ORC will invoke the proper link tool to produce a binary file. Once that completes, ORC will start its scan.

Among other settings, ORC can be configured to exit or merely warn when an ODRV is detected.

## Config file

ORC will walk the current directory up, looking for a config file named either:

- `.orc-config`, or
- `_orc-config`

If found, many switches can control ORC's logic. Please see the `_orc_config`
in the repository for examples. ORC will prefer `.orc-config` so it's simple
to copy the original `_orc_config` and change values locally in the `.orc-config`.

## Output

For example:

```
error: ODRV (structure:byte_size); conflict in `object`
    compilation unit: a.o:
        definition location: /Volumes/src/orc/extras/struct0/src/a.cpp:3
        calling_convention: pass by value; 5 (0x5)
        name: object
        byte_size: 4 (0x4)
    compilation unit: main.o:
        definition location: /Volumes/src/orc/extras/struct0/src/main.cpp:3
        calling_convention: pass by value; 5 (0x5)
        name: object
        byte_size: 1 (0x1)
```

`structure:byte_size` is known as the ODRV category, and details exactly what kind of violation this error represents. The two compilation units that are in conflict are then output, along with the DWARF information that resulted in the collision.

```
struct object { ... }
```

`In a.o:` and `In main.o` are the 2 object files or archives that are mis-matched. The ODR is likely caused by mis-matched compile or #define settings in compiling these archives. `byte_size` is the actual value causing an error.

```    
definition location: /Volumes/src/orc/extras/struct0/src/a.cpp:3
```

What line and file the object was declared in. So line 3 of `a.cpp` in this example.

# The ORC Test App (`orc_test`)

A unit test application is provided to ensure that ORC is catching what is purports to catch. `orc_test` introduces a miniature "build system" to generate object files from known sources to produce known ODR violations. It then processes the object files using the same engine as the ORC command line tool, and compares the results against an expected ODRV report list.

## The Test Battery Structure

Every unit test in the battery is discrete, and contains:

1. A set of source files
2. `odrv_test.toml`, a high level TOML file describing the parameters of the test

In general, a single test should elicit a single ODR violation, but this may not be possible in all cases. 

### The Source File(s)

These files are standard C++ source files. Their quantity and size should be very small - only big enough as needed to cause the intended ODRV.

### The `odrv_test.toml` File

The settings file describes to the test application what source(s) need to be compiled, what compilation flags should be used for the test, and what ODRVs the system needs to be on the lookout as a result of linking the generated object file(s) together.

#### Specifying Sources

Test sources are specified with a `[[source]]` directive:

```toml
[[source]]
    path = "one.cpp"
    obj = "one"
    flags = [
        "-Dfoo=1"
    ]
```

The `path` field describes a path to the file relative to `odrv_test.toml`. It is the only required field.

The `obj` field specifies the name of the (temporary) object file to be created. If this name is omitted, a pseudo-random name will be used.

The `flags` field specifies compilation flags that will be used specifically for this compilation unit. Using this field, it is possible to reuse the same source file with different compilation flags to elicit an ODRV.

#### Specifying ODRVs

ODRVs are specified with the `[[odrv]]` directive:


```toml
[[odrv]]
    category = "subprogram:vtable_elem_location"
    linkage_name = "_ZNK6object3apiEv"
```

The `category ` field describes the specific type of ODR violation the test app should expect to find.

The `linkage_name` field describes the specific symbol that caused the ODRV. It is currently unused, but will be enforced as the test app matures.

#### Fields In Development

The following flags are not currently in use or will undergo heavy changes as the unit test app continues to mature.

- `[compile_flags]`: A series of compilation flags that should be applied to every source file in the unit test.

- `[orc_test_flags]`: A series of runtime settings to pass to the test app for this test.

- `[orc_flags]`: A series of runtime settings to pass to the ORC engine for this test.

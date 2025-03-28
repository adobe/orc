# For documentation on `just`, see https://github.com/casey/just
# and the online manual https://just.systems/man/en/
# This set of recipes have only been tested on macOS.

set shell := ["bash", "-uc"]

# Self-help
[private] # Don't print `default` as one of the recipes
default:
  @just --list --justfile {{justfile()}} --list-heading $'Usage: `just recipe` where `recipe` is one of:\n'

# Generate the cmake project (Tracy disabled)
gen:
    cmake -B build -GXcode -DTRACY_ENABLE=OFF

# Erase and rebuild `build/` folder
[confirm("Are you sure you want to delete `build/`? (y/N)")]
nuke: && gen
    rm -rf build/

# Generate the cmake project (Tracy enabled)
tracy:
    cmake -B build -GXcode -DTRACY_ENABLE=ON

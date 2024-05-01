set shell := ["bash", "-uc"]

alias g := generate

# Self-help
default:
  @just --list --justfile {{justfile()}}

build_instruction := if path_exists("build") == "true" { "echo '`build/`' exists" } else { "mkdir build" }

# make the `build` directory where cmake files will go
mkdir:
    @{{build_instruction}}

# Generate the cmake project (Tracy disabled by default)
generate: mkdir
    cd build && cmake .. -GXcode -DTRACY_ENABLE=OFF

# Erase and rebuild the `build` folder
nuke:
    rm -rf build/
    just --justfile {{justfile()}} generate

# Enable Tracy support
tracy:
    cd build && cmake .. -GXcode -DTRACY_ENABLE=ON

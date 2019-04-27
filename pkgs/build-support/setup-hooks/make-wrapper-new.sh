# Assert that FILE exists and is executable
#
# assertExecutableNew FILE
assertExecutableNew() {
    local file="$1"
    [[ -f "$file" && -x "$file" ]] || \
        die "Cannot wrap '$file' because it is not an executable file"
}

# construct an executable file that wraps the actual executable
# makeWrapperNew EXECUTABLE OUT_PATH ARGS
#
# ARGS:
# --argv0       NAME     : set argv[0] of executed process to NAME
#                          (by default it's as with --argv0-wrapped)
# --argv0-wrapper        : set argv[0] of executed process to the path used to
#                          invoke the wrapper
# --argv0-wrapped        : set argv[0] of executed process to the absolute path
#                          of the wrapped program (not the wrapper)
# --set         VAR VAL  : set environment variable VAR to value VAL
# --set-default VAR VAL  : like --set, but only sets VAR if not already set
# --unset       VAR      : remove VAR from the environment
# --arg         ARG      : add ARG to invocation of executable
# --args        N ARG... : add N ARGs to invocation of executable
# --prefix      ENV SEP VAL : add elements in VAL to the list-type environment
# --suffix                    variable ENV (separated by SEP), new values are
#                             added before/after the original ones and values
#                             already present are removed from the original
#                             part.
makeWrapperNew() {
    local original="$1"
    local wrapper="$2"
    shift 2

    assertExecutableNew "$original"
    mkdir -p "$(dirname "$wrapper")"
    @wrapperNativeBinary@ --make-wrapper "$wrapper" "@wrapperBinary@" "$original" "$@"
    chmod +x "$wrapper"
}

# Syntax: wrapProgramNew <PROGRAM> <MAKE-WRAPPER FLAGS...>
# This also adds --argv0-wrapper automatically.
wrapProgramNew() {
    local prog="$1"
    shift
    local hidden

    assertExecutableNew "$prog"

    hidden="$(dirname "$prog")/.$(basename "$prog")"-wrapped
    while [ -e "$hidden" ]; do
      hidden="${hidden}_"
    done
    mv "$prog" "$hidden"

    makeWrapperNew "$hidden" "$prog" --argv0-wrapper "$@"
}

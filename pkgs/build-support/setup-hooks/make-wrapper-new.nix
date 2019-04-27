{ lib, runCommand, gcc, binutils-unwrapped, makeSetupHook, dieHook }:
let wrapperBinary = runCommand "nix-program-wrapper" {} ''
    PATH=${lib.makeBinPath [gcc binutils-unwrapped]}
    c++ -std=c++14 -O2 -Wall -Wextra ${./new-wrapper.cpp} -o "$out"
    strip --strip-unneeded "$out"
'';
in
makeSetupHook { deps = [ dieHook ]; substitutions = { inherit wrapperBinary;
    wrapperNativeBinary = wrapperBinary.nativeDrv or wrapperBinary; }; }
    ./make-wrapper-new.sh

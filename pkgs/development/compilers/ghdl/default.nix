{ stdenv, fetchFromGitHub, gnat, zlib, llvm_39, ncurses, clang, flavour ? "mcode" }:

# mcode only works on x86, while the llvm flavour works on both x86 and x86_64.


assert flavour == "llvm" || flavour == "mcode";

let
  inherit (stdenv.lib) optional;
  inherit (stdenv.lib) optionals;
  version = "0.34";
in
stdenv.mkDerivation rec {
  name = "ghdl-${flavour}-${version}";

  src = fetchFromGitHub {
    owner = "tgingold";
    repo = "ghdl";
    rev = "v${version}";
    sha256 = "1hjzxba96s5vzq26zp67gq51qqyr4vhak5s7a1j721f331qrvqkg";
  };

  buildInputs = [ gnat zlib ] ++ optionals (flavour == "llvm") [ clang ncurses ];

  configureFlags = optional (flavour == "llvm") "--with-llvm=${llvm_39}";

  patchPhase = ''
    # Disable warnings-as-errors, because there are warnings (unused things)
    sed -i s/-gnatwae/-gnatwa/ Makefile.in ghdl.gpr.in
  '';

  hardeningDisable = [ "all" ];

  enableParallelBuilding = true;

  meta = {
    homepage = http://sourceforge.net/p/ghdl-updates/wiki/Home/;
    description = "Free VHDL simulator";
    maintainers = with stdenv.lib.maintainers; [viric];
    platforms = with stdenv.lib.platforms; [ "i686-linux" "x86_64-linux" ];
    license = stdenv.lib.licenses.gpl2Plus;
  };
}

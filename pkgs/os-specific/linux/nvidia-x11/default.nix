{ lib, callPackage, fetchurl }:

let
  generic = args: callPackage (import ./generic.nix args) { };
  kernel = callPackage # a hacky way of extracting parameters from callPackage
    ({ kernel, libsOnly ? false }: if libsOnly then { } else kernel) { };

  maybePatch_drm_legacy =
    lib.optional (lib.versionOlder "4.14" (kernel.version or "0"))
      (fetchurl {
        url = "https://raw.githubusercontent.com/MilhouseVH/LibreELEC.tv/b5d2d6a1"
            + "/packages/x11/driver/xf86-video-nvidia-legacy/patches/"
            + "xf86-video-nvidia-legacy-0010-kernel-4.14.patch";
        sha256 = "18clfpw03g8dxm61bmdkmccyaxir3gnq451z6xqa2ilm3j820aa5";
      });
in
rec {
  # Policy: use the highest stable version as the default (on our master).
  stable = generic {
    version = "410.78";
    sha256_64bit = "1ciabnmvh95gsfiaakq158x2yws3m9zxvnxws3p32lz9riblpdjx";
    settingsSha256 = "1677g7rcjbcs5fja1s4p0syhhz46g9x2qqzyn3wwwrjsj7rwaz77";
    persistencedSha256 = "01kvd3zp056i4n8vazj7gx1xw0h4yjdlpazmspnsmwg24ijb82x4";

    patches = lib.optional (kernel.meta.branch == "4.19") ./drm_mode_connector.patch;
  };

  beta = generic {
    version = "418.43";
    sha256_64bit = "0zrjd8wfc8cknfwiyy3llsnn1mympfxxszz7gzakncp3vs1jrghq";
    settingsSha256 = "1rzxxn7nrhwndl2lgjakxi0rblfkqjyl1n70fl7rcs6g75mi1qsq";
    persistencedSha256 = "14m4x5r0yc23sg4xx523ccfpxsr6dgm2n3xy9zdibl1cpzz50ikc";
  };

  legacy_340 = generic {
    version = "340.107";
    sha256_32bit = "0mh83affz6bim26ws7kkwwcfj2s6vkdy4d45hifsbshr82qd52wd";
    sha256_64bit = "0pv9yv3x0kg9hfkmc50xb54ahxkbnyy2vyy4hj2h0s6m9sb5kqz3";
    settingsSha256 = "1rgaa24acdyqa1rqrx56293vxpskr792njqqpigqmps04llsx703";
    persistencedSha256 = "0nwv6kh4gxgy80x1zs6gcg5hy3amg25xhsfa2v4mwqa36sblxz6l";
    useGLVND = false;

    patches = [ ./vm_operations_struct-fault.patch ];
  };

  legacy_304 = generic {
    version = "304.137";
    sha256_32bit = "1y34c2gvmmacxk2c72d4hsysszncgfndc4s1nzldy2q9qagkg66a";
    sha256_64bit = "1qp3jv6279k83k3z96p6vg3dd35y9bhmlyyyrkii7sib7bdmc7zb";
    settingsSha256 = "0i5znfq6jkabgi8xpcy12pdpww6a67i8mq60z1kjq36mmnb25pmi";
    persistencedSha256 = null;
    useGLVND = false;
    useProfiles = false;
    settings32Bit = true;

    prePatch = let
      debPatches = fetchurl {
        url = "mirror://debian/pool/non-free/n/nvidia-graphics-drivers-legacy-304xx/"
            + "nvidia-graphics-drivers-legacy-304xx_304.137-5.debian.tar.xz";
        sha256 = "0n8512mfcnvklfbg8gv4lzbkm3z6nncwj6ix2b8ngdkmc04f3b6l";
      };
      prefix = "debian/module/debian/patches";
      applyPatches = pnames: if pnames == [] then null else
        ''
          tar xf '${debPatches}'
          sed 's|^\([+-]\{3\} [ab]\)/|\1/kernel/|' -i ${prefix}/*.patch
          patches="$patches ${lib.concatMapStringsSep " " (pname: "${prefix}/${pname}.patch") pnames}"
        '';
    in applyPatches [ "fix-typos" ];
    patches = maybePatch_drm_legacy;
    broken = lib.versionAtLeast kernel.version "4.18";
  };
}

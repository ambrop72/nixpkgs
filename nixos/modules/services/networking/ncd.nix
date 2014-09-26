{ config, lib, pkgs, ... }:

with lib;

{
  options.networking.ncd = {
    enable = mkOption {
      type = types.bool;
      default = false;
      description = ''
        If enabled, the Network Configuration Daemon (NCD) will be started.
        The user is responsible for providing the NCD scripts.
        Enabling this disables automatic DHCP and firewall:
        networking.useDHCP = false;
        networking.firewall.enable = false;
      '';
    };

    ncdConfDir = mkOption {
      type = types.path;
      description = ''
        The path where NCD scripts are located.
        E.g. you can put the scripts into /etc/nixos/ncd, and specify
        ncdConfDir=./ncd in /etc/nixos/configuration.nix .
      '';
    };

    scripts = mkOption {
      type = types.listOf types.str;
      description = ''
        File names of NCD scripts. The scripts are copied (linked)
        to a single directory, so they can refer to each other.
        You should always have a main script (main.ncd or main.ncd.nix), because
        this is what the systemd service will use.
        If a file name ends with .nix, it is imported as a Nix expression, which
        should be something like the following: { pkgs }: ''' code... '''
        In this case the .nix extension is stripped.
      '';
    };

    utils = mkOption {
      type = types.listOf types.str;
      default = [];
      description = ''
        File names of NCD scripts to be made executable and available in PATH.
        Note that for this to work, the scripts will need to define a correct
        shebang, presumbaly like this: #!''${pkgs.badvpn}/bin/badvpn-ncd .
        This implies that the scripts need to be Nix expressions.
      '';
    };

    extraPackages = mkOption {
      type = types.listOf types.package;
      default = [ pkgs.iproute pkgs.iptables ];
      description = ''
        List of extra packages available in PATH.
        The default is iproute and iptables, so if you set this
        consider that they will not be added automatically.
      '';
    };
  };

  config = let
    badvpn = pkgs.badvpn;
    
    readPlainOrNix = sourcePath: rec {
      sourceName = baseNameOf (toString sourcePath);
      isNix = (hasSuffix ".nix" sourceName);
      text = if isNix then ((import sourcePath) { inherit pkgs; }) else (builtins.readFile sourcePath);
      name = removeSuffix ".nix" sourceName;
    };

    myconf = config.networking.ncd;

    scriptResults = map (
      sourceName: let
        file = readPlainOrNix ( myconf.ncdConfDir + "/${sourceName}" );
      in rec {
        name = file.name;
        isUtil = builtins.elem sourceName myconf.utils;
        textFile = pkgs.writeTextFile {
          name = file.name;
          destination = "/${file.name}";
          text = file.text;
          executable = isUtil;
        };
      }
    ) (myconf.scripts ++ myconf.utils);

    ncdScripts = pkgs.buildEnv {
      name = "ncd_scripts";
      paths = map (x: x.textFile) scriptResults;
    };
    
    ncdScriptsChecked = pkgs.stdenv.mkDerivation {
      name = "ncd_scripts_checked";
      unpackPhase = "true";
      installPhase = ''
        ${badvpn}/bin/badvpn-ncd --syntax-only ${ncdScripts}/main.ncd
        ln -s ${ncdScripts} $out
      '';
      dontFixup = true;
    };

    ncdUtils = pkgs.stdenv.mkDerivation {
      name = "ncd_utils";
      unpackPhase = "true";
      installPhase = ''
        mkdir -p $out/bin
      '' + concatStrings (map (x: ''
        ln -s ${ncdScripts}/${x.name} $out/bin/${x.name}
      '') (builtins.filter (x: x.isUtil) scriptResults));
    };

  in mkIf myconf.enable {
    # Set up a systemd service.
    systemd.services.ncd = {
      description = "Network Configuration Daemon1";
      wantedBy = [ "multi-user.target" ];
      after = [ "syslog.target" "network-setup.service" ];
      path = myconf.extraPackages;
      serviceConfig = {
        ExecStart = "${badvpn}/bin/badvpn-ncd --logger syslog --syslog-ident ncd --loglevel warning --channel-loglevel ncd_log_msg info --signal-exit-code 0 ${ncdScriptsChecked}/main.ncd";
        Restart = "always";
      };
    };

    # Disable default networking that interferes with NCD.
    networking.useDHCP = false;
    networking.firewall.enable = false;
    networking.runResolvconfAtActivation = false;

    # Make utility programs available.
    environment.systemPackages = [ ncdUtils ];
  };
}

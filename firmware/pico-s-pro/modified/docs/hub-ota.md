# Updating a Pico S or Pico Pro from the Hub

Open the brewer's **Firmware** view, select the intended version, and choose **Install**. The Hub downloads and verifies the package for that machine. **Check for Updates** refreshes available versions immediately. **Download** saves the selected archive to your computer. Keep the idle brewer powered and reachable at its registered LAN address through both restarts. The view shows the last result after a Hub restart.

The feature is enabled in a normally started Hub. An online Pico running standalone 1.3.0 advertises `hub_ota_v1` after checking its installed loader, recovery image, boot record and readable application. The Hub also requires an enabled registration, a unique provisioned API key, a matching firmware-reported model, and ready, idle status. The package must support protocol 1. A stored package alone does not establish eligibility.

## One-time USB setup

A stock or 1.2.0 brewer cannot install its first OTA-capable system over Wi-Fi. The guarded USB installer currently has a verified backup and allocation profile for one Pico Pro. On that profile, follow the [USB installation guide](macos-usb-installation.md) to provision the key and install the 1.3.0 main application and website. In a fresh programming session, use the same original backup and key with:

```sh
python tools/pico_flash_install.py install-recovery --backup "$BACKUP" --key-file "$KEY_FILE" --output "$RECOVERY_REPORT"
```

Use a new absolute report path. This operation verifies and writes the packaged Hub recovery to the existing image 1 allocation, reads it back, and checks that the normal application remains selected. Reboot the brewer into the regular menu and reconnect from the Hub. For a Pico S or any other flash layout, initial provisioning still requires its own verified backup and reviewed installation profile. The Pro USB command is specific to its pinned appliance.

## Transfer and recovery

The main application authenticates the request, checks its own model, UID, operating state and current image digest, and selects recovery. Recovery runs separately with brewing outputs off and uses the saved Wi-Fi profile and API key. The Hub verifies its authenticated UID before transferring anything. Recovery writes image 2 and the five website files to fixed destinations, then reopens each file and checks its SHA-256 before selecting a trial boot. The Hub confirms the boot only after the expected brewer reports the requested application version, image digest and a ready, idle state.

If the connection fails, check the machine and reconnect at its registered address. Choose the same package to retry. The Hub rechecks the files already in recovery and transfers those still missing or different. It records completion only after the trial is confirmed. Keep a copy of the original USB backup. There is one application-and-website set, so an interrupted write can leave that set incomplete. The separate recovery image stays available during payload writes. A failed boot may require a manual reset or USB recovery. Power loss during boot-record changes, GPIO behavior, wireless transfers and recovery on actual appliances still need physical testing.

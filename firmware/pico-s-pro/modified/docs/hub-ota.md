# Updating a Pico S or Pico Pro from the Hub

Open the brewer's **Firmware** view, select the intended version, and choose **Install**. The Hub downloads and verifies the package for that machine. **Check for Updates** refreshes available versions immediately. **Download** saves the selected archive to your computer. Keep the idle brewer powered and reachable at its registered LAN address through both restarts. The view shows the last result after a Hub restart.

The feature is enabled in a normally started Hub. An online Pico running standalone 1.3.x advertises `hub_ota_v1` after checking its installed loader, recovery image, boot record and readable application. The Hub also requires an enabled registration, a unique provisioned API key, a matching firmware-reported model, and ready, idle status. Return the brewer to its top-level main menu before installation. Firmware 1.3.0 reports any open settings screen as busy. The package must support protocol 1. A stored package alone does not establish eligibility.

## One-time USB setup

A brewer running stock or standalone 1.0–1.2 needs a one-time USB upgrade. The guarded installer supports a pinned historical appliance and individual units with a qualified stock backup and matching live identity and allocation layout. See [USB setup and stock restoration](macos-usb-installation.md). The owner identified the brewer previously recorded under the Pico Pro backup filename as a Pico S. Both first-install commands accept an explicit model. The backup filename is historical and does not determine model selection.

Follow the [USB guide](macos-usb-installation.md) for power-on timing, the backup, a new private report path per session and the existing API key. For that Pico S, use the following commands from this component, one programming session at a time:

```sh
sudo "$PYTHON" tools/pico_flash_install.py install --model pico-s --backup "$BACKUP" --key-file "$KEY_FILE" --firmware firmware/standalone/pico-s/pico-s-standalone.bin --output "$MAIN_REPORT"
sudo "$PYTHON" tools/pico_flash_install.py install-recovery --model pico-s --backup "$BACKUP" --key-file "$KEY_FILE" --output "$RECOVERY_REPORT"
```

The application and recovery files passed full readback on this appliance during its direct USB upgrade from 1.0.0. The recovery operation retained the active application, boot selection and device key. The Pro target retains the existing CLI default. Other appliances require their own reviewed backup and layout. Once the two writes verify, power off, disconnect USB, boot normally and check the reported model, version and OTA capability.

## Transfer and recovery

The main application authenticates the request, checks its own model, UID, operating state and current image digest, and selects recovery. Recovery runs separately with brewing outputs off and uses the saved Wi-Fi profile and API key. The Hub verifies its authenticated UID before transferring anything. Recovery writes image 2 and the five website files to fixed destinations, then reopens each file and checks its SHA-256 before selecting a trial boot. The Hub confirms the boot only after the expected brewer reports the requested application version, image digest and a ready, idle state.

On versions with OLED update status, the outgoing application shows **Installing firmware** after it selects recovery. The separate recovery image does not redraw the screen or report a percentage on it. The incoming application shows **Verifying firmware** while it checks the trial image for the Hub and **Update confirmed** after it commits the verified boot selection. The Hub shows transfer progress, including when updating from older applications.

If the connection fails, check the machine and reconnect at its registered address. Choose the same package to retry. The Hub rechecks the files already in recovery and transfers those still missing or different. It records completion only after the trial is confirmed. Keep a copy of the original USB backup. There is one application-and-website set, so an interrupted write can leave that set incomplete. The separate recovery image stays available during payload writes. A failed boot may require a manual reset or USB recovery. Power loss during boot-record changes, GPIO behavior, wireless transfers and recovery on actual appliances still need physical testing.

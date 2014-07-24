#!/bin/sh
echo "Unloading from Launchd..."
sudo launchctl unload /Library/LaunchDaemons/me.rodionovd.RDInjectionWizard.injector.plist

echo "Cleaning up /Library/LaunchDaemons/"
sudo rm -f /Library/LaunchDaemons/me.rodionovd.RDInjectionWizard.injector.plist
sudo rm -f /Library/PrivilegedHelperTools/me.rodionovd.RDInjectionWizard.injector.plist

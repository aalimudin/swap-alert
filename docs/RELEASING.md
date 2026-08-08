# Releasing Swap Alert for macOS

This project includes a repeatable release pipeline that builds, tests, deploys Qt frameworks, creates a DMG, and optionally performs Developer ID signing and Apple notarization.

## Local package

Install the normal build prerequisites, then run:

```bash
./packaging/macos/package.sh
```

The script creates an ad-hoc signed DMG under `dist/`. Ad-hoc signing is suitable for local package testing, but it does not make the application trusted on another Mac.

Verify the result with:

```bash
./packaging/macos/verify-package.sh dist/Swap-Alert-0.1.0-macOS.dmg
```

The verifier mounts the image, validates the bundle signature and property list, and checks that no executable still links to a Homebrew Qt path.

If disk-image creation is unavailable in a restricted environment, the staged application can be checked directly:

```bash
./packaging/macos/verify-package.sh "dist/stage/Swap Alert.app"
```

## Developer ID signing

Distribution outside the Mac App Store requires a `Developer ID Application` certificate from the Apple Developer Program. List available identities with:

```bash
security find-identity -v -p codesigning
```

Then create a signed package:

```bash
SIGN_IDENTITY="Developer ID Application: Your Name (TEAMID)" \
  ./packaging/macos/package.sh
```

`macdeployqt` copies the Qt frameworks and signs the nested bundle components with the hardened runtime and a secure timestamp. The script signs the DMG separately.

## Notarization

Store credentials once in the login keychain. Apple documents the issuer ID, key ID, and private API key in the App Store Connect API workflow.

```bash
xcrun notarytool store-credentials "swap-alert-notary" \
  --key "/path/to/AuthKey_KEYID.p8" \
  --key-id "KEYID" \
  --issuer "ISSUER_UUID"
```

Build, submit, wait for acceptance, and staple the ticket with:

```bash
SIGN_IDENTITY="Developer ID Application: Your Name (TEAMID)" \
NOTARY_PROFILE="swap-alert-notary" \
NOTARIZE=1 \
  ./packaging/macos/package.sh
```

Real signing and notarization require an Apple Developer account, a Developer ID certificate, notarization credentials, and a working full Xcode installation. The local automated tests cannot substitute for Apple's notarization service.

For a signed and notarized build, also enable Gatekeeper verification:

```bash
VERIFY_GATEKEEPER=1 \
  ./packaging/macos/verify-package.sh dist/Swap-Alert-0.1.0-macOS.dmg
```

## Release checklist

1. Update the project version and bundle build number in `CMakeLists.txt`.
2. Run the unit and UI tests.
3. Build the signed, notarized DMG.
4. Run package verification on the DMG.
5. Install the DMG on a second Mac and confirm notifications and start at login.
6. Put the application in `/Applications` before enabling start at login.
7. Tag the exact source revision used for the package.

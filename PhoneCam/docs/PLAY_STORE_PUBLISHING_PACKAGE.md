# PhoneCam Google Play Publishing Package

Last updated: 2026-05-28

## Current Project Status

Project path:

`mobile-android`

Current Android package:

`com.phonecam`

Current app name:

`PhoneCam`

Current version:

`versionCode = 1`
`versionName = "1.0.0"`

Current SDK configuration:

`compileSdk = 35`
`targetSdk = 35`
`minSdk = 26`

Play Store target SDK status:

Google Play currently requires new apps and updates to target Android 15 / API 35 or higher. This project has been updated to target API 35.

Installed local SDK platforms found:

- `android-28`
- `android-31`
- `android-32`
- `android-33`
- `android-34`
- `android-35`
- `android-36`
- `android-36.1`

Current setting:

```kotlin
compileSdk = 35
targetSdk = 35
```

## Required Before Upload

1. Google Play Developer account
2. Completed developer identity verification
3. App package name confirmed permanently
4. Production-ready app icon
5. Feature graphic
6. Phone screenshots
7. Privacy policy URL
8. Data safety form
9. App content declarations
10. Content rating questionnaire
11. Target audience selection
12. Closed test, if required for your account
13. Signed Android App Bundle (`.aab`)

## Release Build Requirements

Google Play expects Android App Bundle format for new apps.

Generate an AAB from Android Studio:

`Build > Generate Signed Bundle / APK > Android App Bundle`

Or from terminal after signing is configured:

```powershell
cd "C:\Users\tittu\Videos\webcam - Copy (5)\PhoneCam\mobile-android"
.\gradlew.bat bundleRelease
```

Expected output:

`mobile-android\app\build\outputs\bundle\release\app-release.aab`

This repository currently does not include a release keystore or Gradle signing config. Create a private upload key in Android Studio and keep it outside version control.

## Signing Checklist

Create a keystore:

- Key store path: outside the repo, for example `C:\Users\tittu\Documents\keys\phonecam-upload.jks`
- Key alias: `phonecam_upload`
- Validity: 25+ years
- Store password: save securely
- Key password: save securely

Never commit:

- `.jks`
- `.keystore`
- passwords
- `key.properties`

Use Google Play App Signing in Play Console. Upload your signed AAB using the upload key.

## Store Listing Copy

App name:

PhoneCam

Short description:

Use your Android phone as a webcam and microphone for your Windows PC.

Full description:

PhoneCam turns your Android phone into a webcam and microphone for your Windows PC. Connect over USB, Wi-Fi, or Bluetooth, then use the PhoneCam Virtual Camera in apps such as Zoom, Teams, OBS, and other video tools.

Features:

- Live camera preview
- Phone camera streaming to PC
- Phone microphone streaming to PC
- USB, Wi-Fi, and Bluetooth connection modes
- Front/back camera switching
- Rotation, flash, focus, zoom, and resolution controls
- Foreground streaming service with visible notification

PhoneCam is designed for users who want to reuse their phone camera for video calls, recordings, and desktop camera workflows.

Notes:

- A companion Windows client is required.
- USB mode may require USB debugging and ADB port forwarding.
- Wi-Fi mode requires the phone and PC to be on the same network.
- Bluetooth mode requires the phone and PC to be paired.

## Graphic Assets Required

Minimum recommended assets:

- High-resolution app icon: 512 x 512 PNG
- Feature graphic: 1024 x 500 PNG or JPEG
- Phone screenshots: at least 2, up to 8
- Optional 7-inch tablet screenshots
- Optional 10-inch tablet screenshots
- Optional promo video YouTube URL

Screenshot requirements:

- JPEG or 24-bit PNG, no alpha
- Minimum side: 320 px
- Maximum side: 3840 px
- Long side cannot be more than 2x the short side

Suggested PhoneCam screenshot set:

1. Main connection screen with camera preview
2. USB mode waiting for PC
3. Wi-Fi mode showing phone IP and port
4. Bluetooth mode pairing/connection screen
5. Streaming connected status
6. Camera controls: flip, rotate, mic, flash
7. Windows companion client connected
8. Zoom/Teams showing PhoneCam Virtual Camera

## Privacy Policy Draft

Publish this as a web page before submission. Replace placeholders before use.

```text
Privacy Policy for PhoneCam

Effective date: [DATE]

PhoneCam is an Android application that lets users stream camera and microphone output from their Android device to their own Windows PC for webcam and audio use.

Data Access

PhoneCam requests access to the device camera and microphone so it can provide live video and audio streaming functionality. PhoneCam may also access network, Wi-Fi, Bluetooth, and foreground service capabilities to connect the phone to the user's PC.

Data Collection

PhoneCam does not collect, sell, or share personal data with the developer or third parties. Camera and microphone data is processed on the user's device and transmitted only to the PC selected by the user through USB, Wi-Fi, or Bluetooth connection modes.

Camera and Microphone

Camera and microphone access is used only when the user grants permission and starts or uses the app's streaming functionality. The app displays a foreground notification while streaming service functionality is active.

Network and Bluetooth

PhoneCam uses local network, USB/ADB port forwarding, or Bluetooth connections to communicate with the user's PC. The app does not upload camera or microphone streams to cloud servers.

Data Retention

PhoneCam does not store camera recordings, microphone recordings, or streamed media on developer-controlled servers.

Children

PhoneCam is not directed at children. Users should only use the app in compliance with local privacy and recording laws.

Contact

For privacy questions, contact:

[DEVELOPER NAME]
[SUPPORT EMAIL]
[SUPPORT WEBSITE OR ADDRESS]
```

## Data Safety Form Draft

Use this only if the app behavior remains as currently implemented.

Data collected:

- No data collected by the developer

Data shared:

- No data shared with third parties by the developer

Security practices:

- Data is not sent to developer servers
- Users can stop streaming by disconnecting/stopping the app
- No account creation required

Data types involved in app functionality:

- Audio: microphone stream used for webcam/microphone functionality
- Photos and videos: camera stream used for webcam functionality
- Device or other IDs: Bluetooth device address/name may be displayed locally for pairing/connection

Collection/sharing explanation:

The app accesses camera, microphone, Wi-Fi/network, and Bluetooth only to provide the user-requested local streaming feature between the user's phone and PC. The developer does not collect this data on servers.

## App Permissions Explanation

Declared permissions:

- `CAMERA`: required for phone camera preview and video stream
- `RECORD_AUDIO`: required for phone microphone stream
- `MODIFY_AUDIO_SETTINGS`: required for audio behavior
- `INTERNET`: required for local TCP streaming over Wi-Fi/USB forwarding
- `ACCESS_WIFI_STATE`: required to display local Wi-Fi connection details
- `ACCESS_NETWORK_STATE`: required to detect network state
- `BLUETOOTH`, `BLUETOOTH_ADMIN`: legacy Bluetooth support
- `BLUETOOTH_CONNECT`, `BLUETOOTH_ADVERTISE`: Android 12+ Bluetooth connection support
- `FOREGROUND_SERVICE`: keeps streaming alive with a visible notification
- `FOREGROUND_SERVICE_CAMERA`: foreground camera streaming
- `FOREGROUND_SERVICE_MICROPHONE`: foreground microphone streaming
- `FOREGROUND_SERVICE_CONNECTED_DEVICE`: foreground connected-device streaming
- `WAKE_LOCK`: keeps device awake during streaming

Play Console permission declaration may be required for foreground service types. Explain that the service runs only to stream the user's camera and microphone to the user's selected PC while the app is active/connected, with a visible notification.

## App Content Answers

Content rating:

- Category: Utility / Tools
- Violence: No
- Sexual content: No
- Profanity: No
- Controlled substances: No
- User-generated content: No
- Location sharing: No
- Personal information sharing: No account or profile feature
- Digital purchases/gambling: No

Target audience:

- Recommended: 13+ or adult/general audience
- Not specifically designed for children

Ads:

- No ads, unless added later

In-app purchases:

- No, unless added later

Government apps:

- No

News apps:

- No

Health apps:

- No

Financial features:

- No

## Testing Requirements

Run at least:

- Internal test for quick smoke testing
- Closed test for real user feedback

If this is a personal developer account created after 2023-11-13, Google Play requires a closed test with at least 12 opted-in testers for at least 14 continuous days before applying for production access.

Test cases to complete:

- First launch permission prompts
- Camera preview appears
- USB connection to Windows client
- Wi-Fi connection to Windows client
- Bluetooth connection to Windows client
- Start/stop streaming repeatedly
- Rotate camera
- Switch front/back camera
- Toggle microphone
- Toggle flash
- Put app in background while streaming
- Lock/unlock phone while streaming
- Disconnect PC during stream
- Reconnect after disconnect
- Test on Android 8+ if possible because `minSdk = 26`
- Test on Android 14/15/16 devices or emulators

## Pre-Submission Technical Fixes

Required:

- Generate a signed release AAB.
- Create and host privacy policy.
- Confirm app icon is not the default Android launcher icon.
- Create real screenshots and feature graphic.

Recommended:

- Replace hardcoded display strings with string resources.
- Add Play-ready app icon assets at all densities.
- Review Bluetooth MAC address display against privacy expectations.
- Ensure foreground notification clearly states when camera/mic streaming is active.
- Run release build after ProGuard/R8 minification to catch missing keep rules.
- Test release build, not only debug build.

## Play Console Submission Order

1. Create app in Play Console.
2. Set app name, language, app/game, free/paid.
3. Complete Store settings.
4. Add privacy policy URL.
5. Complete Data safety.
6. Complete App access.
7. Complete Ads declaration.
8. Complete Content rating.
9. Complete Target audience and content.
10. Complete News apps / Government apps declarations if prompted.
11. Add main store listing text and graphics.
12. Upload signed AAB to internal testing.
13. Fix pre-launch report issues.
14. Run closed test if required.
15. Apply for production access if required.
16. Create production release.
17. Roll out gradually.

## Commands

Debug build:

```powershell
cd "C:\Users\tittu\Videos\webcam - Copy (5)\PhoneCam\mobile-android"
.\gradlew.bat assembleDebug
```

Release bundle:

```powershell
cd "C:\Users\tittu\Videos\webcam - Copy (5)\PhoneCam\mobile-android"
.\gradlew.bat bundleRelease
```

Install debug to connected device:

```powershell
cd "C:\Users\tittu\Videos\webcam - Copy (5)\PhoneCam\mobile-android"
.\gradlew.bat installDebug
```

## Final Readiness Checklist

- [x] `targetSdk >= 35`
- [ ] Release AAB generated
- [ ] Release AAB signed with upload key
- [ ] App launches from release build
- [ ] Camera permission works
- [ ] Microphone permission works
- [ ] Bluetooth permission works on Android 12+
- [ ] Foreground notification appears during service use
- [ ] No debug-only wording in UI
- [ ] Privacy policy hosted and matches app behavior
- [ ] Data safety answers match app behavior
- [ ] Store screenshots prepared
- [ ] Feature graphic prepared
- [ ] Hi-res icon prepared
- [ ] Content rating completed
- [ ] Closed test completed if required
- [ ] Pre-launch report reviewed
- [ ] Production release notes written

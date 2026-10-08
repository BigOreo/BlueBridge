# GlideKVM for Android (preview)

Lets an Android phone or tablet be one of the computers on your desk: move the
main computer's mouse off the edge of its screen and it appears on the device,
where it clicks, scrolls and types. Text copied on the main computer can be
pasted on the device.

## Using it

1. Install the app (the APK is built by CI as the `glidekvm-android` artifact).
2. Install [Shizuku](https://shizuku.rikka.app/) and start it. On Android 11
   and newer it starts through Wireless debugging; after a restart, open
   Shizuku and tap Start again. Android only lets the system and the shell
   user control other apps, and Shizuku runs GlideKVM's input service as the
   shell user, without rooting the device.
3. In GlideKVM, allow it to use Shizuku and to appear on top (Android shows no
   pointer for a shared mouse, so the app draws one).
4. On the main computer, open **Arrange screens**, click **Add a computer...**,
   use the name shown in the app, and place it where the device sits.
5. In the app, enter the main computer's address and tap **Connect**. The
   first time, compare the fingerprint the app shows with the one under
   **Security** on the main computer, and the device's fingerprint with the
   one the main computer asks you to confirm.

## How it's built

- `core` is plain Kotlin: the wire protocol, TLS with fingerprint checks, and
  the key mapping. It builds and tests anywhere with a JDK. `core:probeJar`
  builds a command line client that `src/test/smoke/android_client.py` runs
  against a real server.
- `app` is the Android app. It's only part of the build when the Android SDK
  is installed (`ANDROID_HOME`, `ANDROID_SDK_ROOT` or `local.properties`).

```
./gradlew :core:test            # anywhere
./gradlew :app:assembleDebug    # with the Android SDK
```

## Not yet

Bluetooth, sending the device's clipboard to the main computer, file
transfer, and starting with the device.

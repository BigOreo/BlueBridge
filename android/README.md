# GlideKVM for Android (preview)

Lets an Android phone or tablet be one of the computers on your desk: move the
main computer's mouse off the edge of its screen and it appears on the device,
where it clicks, scrolls and types. Text copied on the main computer can be
pasted on the device.

## Using it

1. Install the app (the APK is built by CI as the `glidekvm-android` artifact).
2. In the app's setup list:
   - **Let the mouse control it**: turn on *GlideKVM mouse* under
     Accessibility. It plays the main computer's mouse as touch gestures: a
     click is a tap, a drag is a swipe, the right button is a long press and
     the wheel scrolls. It also draws the pointer. It reads nothing on the
     screen.
   - **Let the keyboard type**: turn on *GlideKVM keyboard*. It types into
     the focused field, including shortcuts such as Ctrl+C and Ctrl+V, and is
     only in use while the mouse is on the device: when the mouse leaves or
     the connection ends, your usual keyboard comes back by itself. On
     Android 13 and newer it also takes over by itself when the mouse
     arrives; on older versions, the first time you type from the main
     computer the list of keyboards appears so you can pick it.
   - **Full control (optional)**: with [Shizuku](https://shizuku.rikka.app/)
     running, the mouse and keyboard work like real ones instead: hover,
     right-click, real scrolling and shortcuts in every app, with no need to
     change keyboard. Shizuku starts through Wireless debugging, and needs a
     tap again after each restart.
3. On the main computer, open **Arrange screens**, click **Add a computer...**,
   use the name shown in the app, and place it where the device sits.
4. In the app, tap **Find** to look for the main computer on the same Wi-Fi
   (or type the address from its Home screen), then tap **Connect**. The
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
transfer, and starting with the device. Without Shizuku, a drag plays back as a
swipe when the button is let go rather than following the mouse live.

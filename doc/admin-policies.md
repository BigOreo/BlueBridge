# Managing InputLeap with policies

Administrators can enforce InputLeap settings on the computers they manage.
A policy applies to every user of the computer. Users cannot override it: on
Windows it is stored where only administrators can write, and it is enforced by
the InputLeap server and client themselves, not only by the settings window.
Anything a policy does not set stays the user's choice.

When a policy applies, the main window shows "Managed by your organization"
(hover it to see what is enforced) and the affected options are greyed out.
The server and client log each enforced setting when they start.

## Available policies

| Policy | Registry value (DWORD) | Values | Effect |
|---|---|---|---|
| Allow clipboard sharing | `ClipboardSharing` | `0` off, `1` users decide | Off: clipboard contents are never sent or received, whatever the server configuration says. |
| Allow file transfer | `FileTransfer` | `0` off, `1` users decide | Off: drag and drop file transfer cannot be turned on. |
| Require encryption | `RequireEncryption` | `1` required, `0` users decide | Required: TLS is always used; `--disable-crypto` and the SSL checkbox are ignored. |
| Allowed connection modes | `AllowedConnectionModes` | `1` network, `2` Bluetooth, `3` both | Connections over a mode that is not allowed are refused. |
| Prevent changes to settings | `LockSettings` | `1` locked, `0` users decide | The settings window is read-only. |

On Windows the values live under
`HKEY_LOCAL_MACHINE\SOFTWARE\Policies\InputLeap`. Values outside the listed
range are ignored.

## Group Policy (Active Directory)

1. Copy `dist/policy/admx/InputLeap.admx` to the `PolicyDefinitions` folder of
   your central store (`\\<domain>\SYSVOL\<domain>\Policies\PolicyDefinitions`)
   or `C:\Windows\PolicyDefinitions` for a single computer, and
   `en-US\InputLeap.adml` to the `en-US` folder beside it.
2. In the Group Policy Management Editor, open **Computer Configuration >
   Policies > Administrative Templates > InputLeap** and configure the
   policies.

## Microsoft Intune

1. In the Intune admin center, open **Devices > Configuration** and use the
   **Import ADMX** tab to import `InputLeap.admx` with `en-US\InputLeap.adml`.
   The template has no dependencies on other ADMX files.
2. Create a configuration profile for **Windows 10 and later** from
   **Templates > Imported Administrative templates**, pick the InputLeap
   policies and assign the profile to your devices.

## Other MDM tools and scripts

Any tool that can import ADMX templates or set registry values works. For
example, to disable clipboard sharing and allow Bluetooth only from an elevated
command prompt:

```
reg add "HKLM\SOFTWARE\Policies\InputLeap" /v ClipboardSharing /t REG_DWORD /d 0 /f
reg add "HKLM\SOFTWARE\Policies\InputLeap" /v AllowedConnectionModes /t REG_DWORD /d 2 /f
```

Policies are read when the InputLeap server or client starts; restart
InputLeap (or the computer) after changing them.

## Linux

Put the same names in `/etc/input-leap/policy.conf`, one `Name=value` per
line; `#` starts a comment. The file should be owned by root and not writable
by users.

```
ClipboardSharing=0
RequireEncryption=1
```

## macOS

Not supported yet.

# BlueBridge

One keyboard and mouse for all your computers.

BlueBridge lets you control several computers from the one in front of you.
Move the mouse off the edge of your screen and it continues onto the next
computer, with the keyboard following along. Copy on one computer and paste on
the other.

It works over your network, or over Bluetooth when the network isn't an option,
for example when a VPN on your work laptop blocks local network access.

## Features

- **Seamless mouse and keyboard sharing** between two or more computers.
- **Shared clipboard** for text and images.
- **Network or Bluetooth.** Pick how the computers connect with one switch in
  the main window.
- **Encrypted.** Connections use TLS, and Bluetooth links must be paired and
  encrypted.
- **Ready for company IT.** Clipboard sharing, file transfer, encryption and
  connection modes can be enforced with Group Policy, Intune or other MDM
  tools.

## Supported systems

- Windows 10 and 11 (64-bit): network and Bluetooth
- macOS 10.12 and newer: network (Bluetooth coming)
- Linux: network (Bluetooth coming). Clipboard sharing is not yet supported on
  Wayland.

## Getting started

1. Install BlueBridge on each computer.
2. On the computer with the keyboard and mouse, choose **Server**.
3. Click **Configure server** and drag a screen onto the grid for each other
   computer, next to your own screen on the side where it sits on your desk.
   Give each one the exact screen name (case-sensitive) shown in that
   computer's BlueBridge window.
4. On the other computers, choose **Client** and enter the server's IP address.
5. Click **Start** on both.

Once both show that BlueBridge is running, move the mouse off the edge of your
screen toward the other computer.

If Scroll Lock is on, the mouse stays on the current screen.

## Connecting over Bluetooth (Windows)

A client can connect to the server directly over Bluetooth instead of the
network. Both computers need Bluetooth and must first be paired in Windows'
Bluetooth settings.

The server accepts network and Bluetooth connections at the same time, so each
client can connect whichever way works for it. For example, a laptop on a VPN
can use Bluetooth while another computer uses the network.

1. On the server, click **Start**. The main window shows both its IP address
   and its Bluetooth address.
2. On the client, set **Connect over** to **Bluetooth**, choose the server from
   the **Server** list of paired computers (it shows "ready" once the server is
   running) and click **Start**. If it isn't listed, choose **Enter address
   manually** and type the address shown on the server, e.g.
   `00:1A:7D:DA:71:13`.

This is a direct Bluetooth connection (RFCOMM), not Bluetooth networking, so no
network adapter or IP address is involved. The link must be authenticated and
encrypted, which is why the computers have to be paired; unpaired connections
are refused. Keep SSL turned on so the data is also encrypted end to end with
TLS.

## Tips

**Load a saved configuration at startup:** start the server with
`--config <path_to_saved_configuration>`.

**The client doesn't fill in the server address from a saved configuration:**
add it to the configuration file:

```
section: options
    serverhostname=<AAA.BBB.CCC.DDD>
end
```

## Support

Report problems and request features in the
[issue tracker](https://github.com/BigOreo/BlueBridge/issues).

## License

BlueBridge is free software released under the GNU General Public License,
version 2. See [LICENSE](LICENSE). It is built on the open source InputLeap
project; the original copyright notices are kept in the source files.

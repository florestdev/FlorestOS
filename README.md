# FlorestOS 1.0

A tiny 32-bit x86 hobby operating system by **FlorestDevelopment
(FlorestDev)**.

FlorestOS boots from a 512-byte MBR and runs a protected-mode kernel
with a minimal TCP/IP stack, a VGA text-mode interface, and its own
small scripting language: **FlorestNET++**.

> "Be kind to packets."

## Features

-   32-bit x86 protected-mode kernel
-   512-byte MBR bootloader
-   VGA text-mode shell and 80×25 text GUI
-   Minimal Ethernet, ARP, IPv4, UDP, TCP, and DNS support
-   Drivers for Intel e1000, Realtek RTL8139, and legacy VirtIO-net
-   HTTP requests, Minecraft server status checks, and basic port
    scanning
-   FlorestNET++ scripting language with a built-in editor
-   ATA PIO disk access and an installer for IDE disks

## Build and run

Build and launch the OS with `make`.

### Run from a virtual hard disk (IDE)

``` sh
make run
```

### Run from an ISO image (CD-ROM)

``` sh
make run-iso
```

### Build requirements

Install the following tools and ensure that GCC supports 32-bit
compilation:

-   `make`
-   `nasm`
-   `gcc` with `-m32` support
-   `ld`
-   `objcopy`
-   `qemu-system-i386`
-   `genisoimage` or `xorriso` for ISO creation

Exact package names and installation commands depend on your Linux
distribution.

## Console shell

The shell prompt is:

``` text
florest>
```

### Commands

  -----------------------------------------------------------------------
  Command                             Description
  ----------------------------------- -----------------------------------
  `help`                              Print the command reference.

  `status`                            Show the NIC name, IP address,
                                      gateway, and link state.

  `clear`                             Clear the screen.

  `dns <host> [dns_ip]`               Resolve a DNS A record. The default
                                      DNS server is `10.0.2.3`.

  `udp <ip> <port> <data...>`         Send a UDP datagram and print any
                                      reply. The payload parser
                                      understands `\r` and `\n`.

  `tcp <ip> <port> <data...>`         Open a minimal TCP connection, send
                                      data, and read the response.

  `http <host\|ip> [port] [path]`     Send an HTTP/1.0 GET request and
                                      print the response body.

  `tls <ip\|host> [port]`             Probe TCP connectivity only; TLS
                                      encryption is **not** implemented.

  `scan <ip\|host> <port> [end]`      Scan a port or a range of ports
                                      using SYN probes. A scan is limited
                                      to 128 ports.

  `mcstatus <ip\|host> [port]`        Query Minecraft server status using
                                      modern JSON, legacy status, or the
                                      UDP Query protocol.

  `push <ip\|host> <port> [path]`     Send the results log to an HTTP
                                      endpoint as JSON using POST.

  `results`                           Display the in-memory results log.

  `results clear`                     Clear the in-memory results log.

  `edit`                              Open the FlorestNET++ editor.

  `run`                               Run the current script.

  `script` / `fln <code>`             Dump or evaluate FlorestNET++ code.
                                      Exact behavior depends on the
                                      command form.

  `install`                           Write the bootloader and kernel to
                                      detected IDE disks. Use with care;
                                      this can overwrite disk data.

  `stop` / `shutdown`                 Show the farewell screen and
                                      request power-off through
                                      ACPI/QEMU/VBox support.
  -----------------------------------------------------------------------

## Text-mode GUI

The `gui_run` interface uses an 80×25 VGA text-mode layout with:

-   A top status bar
-   A command list with documentation
-   A FlorestNET++ editor with syntax coloring
-   A bottom button bar
-   A PS/2 mouse cursor represented by `X`

Click the command list to select a command and click the editor to type.
Press `Esc` to leave edit mode.

While editing, the interface uses `keyboard_getchar()` for text input so
mouse bytes are not mixed into the keyboard input stream.

## FlorestNET++

FlorestNET++ is a small Kotlin-DSL-inspired scripting language with a
built-in console editor.

### Example script

``` kotlin
// Comments begin with two slashes.
val x = 2 + 3
var s = "Florest" + "OS"

println("x = " + x)
dns("example.com")
mcstatus("play.example.com", 25565)
scan("1.1.1.1", 80, 90)
http("example.com", 80, "/")
push("10.0.2.2", 8080, "/")
results()
resultsClear()
status()
udp("1.1.1.1", 53, "hi")
tcp("1.1.1.1", 80, "GET /")
tls("1.1.1.1", 443)
help()
```

### Language features

-   `val` and `var` declarations
-   Variable assignment
-   Numbers and strings
-   String escapes: `\n`, `\t`, and `\"`
-   Arithmetic: `+`, `-`, `*`, and `/`
-   Parentheses
-   String concatenation with `+`
-   `print` and `println`
-   Built-in functions for DNS, HTTP, TCP, UDP, TLS probing, Minecraft
    status, scanning, results, and system status

Limits: variables are reset on each run; the interpreter supports up to
32 variables, 24 built-ins, and a 4 KiB script buffer.

## Network stack

The network implementation is in `netstack.cpp` and `dns.cpp`.

-   Ethernet II framing
-   ARP with an 8-entry cache and gateway fallback
-   IPv4 with header checksum
-   UDP with pseudo-header checksum
-   Minimal TCP handshake and data exchange:
    -   SYN → SYN-ACK → ACK
    -   PSH+ACK data
    -   FIN/RST handling
-   DNS A-record lookups

### Virtual network configuration

The default configuration targets the QEMU/VBox user-mode NAT network:

  Setting      Default
  ------------ -------------
  Guest IP     `10.0.2.15`
  Gateway      `10.0.2.2`
  DNS server   `10.0.2.3`

These values are hard-coded; DHCP is not implemented.

## Supported network adapters

  Adapter                 Driver details
  ----------------------- ------------------------------
  Intel e1000 (82540EM)   MMIO; RX/TX descriptor rings
  Realtek RTL8139         I/O BAR; RX ring
  Legacy VirtIO-net       I/O BAR; split virtqueues

Support depends on the virtual machine configuration and the device
being presented to the guest.

## Disk access and installation

`ata.cpp` implements ATA PIO access using LBA28 addressing.

The `install` command writes:

-   The bootloader to LBA 0
-   The kernel to LBAs 1--256

The installer checks for the `0x55AA` boot signature. **This operation
can overwrite data on the selected IDE disk. Use a disposable virtual
disk and back up important data first.**

The current `ata_sector_count()` implementation returns `0`.

## Known limitations

-   **No IDT or interrupt-driven input:** the PIC is fully masked and
    input is polled.
-   **No TLS cryptography:** `tls` only checks TCP connectivity; it does
    not establish a secure TLS session.
-   **Minimal TCP implementation:** no retransmission, a fixed window,
    and only one connection at a time.
-   **Limited DNS:** A records only; no CNAME chasing or EDNS support.
-   **Fixed network configuration:** no DHCP; the default addresses are
    intended for QEMU/VBox user-mode NAT.
-   **ATA limitations:** LBA28 only; `ata_sector_count()` currently
    returns `0`.
-   **Text-mode GUI:** redraws are not optimized and there is no double
    buffering.
-   **Long splash screen:** startup intentionally waits approximately 15
    seconds.

## Source map

  -----------------------------------------------------------------------
  File                                Purpose
  ----------------------------------- -----------------------------------
  `bootloader.asm`                    512-byte MBR bootloader; LBA/CHS
                                      loading, A20 setup, GDT, and
                                      protected-mode transition.

  `kernel_entry.asm`                  32-bit entry point; masks PICs,
                                      sets up the stack, and calls
                                      `kmain`.

  `kernel.cpp`                        Splash screen, mode chooser, shell,
                                      shell commands, installer, and
                                      shutdown handling.

  `vga.cpp`, `keyboard.cpp`,          Basic VGA and input/output support.
  `mouse.cpp`, `timer.cpp`            

  `pci.cpp`, `nic.cpp`, `e1000.cpp`,  PCI discovery and network adapter
  `rtl8139.cpp`, `virtio_net.cpp`     drivers.

  `netstack.cpp`, `dns.cpp`           Network stack and DNS resolver.

  `ata.cpp`                           ATA PIO disk driver.

  `fln.cpp`, `gui.cpp`                FlorestNET++ interpreter and
                                      text-mode GUI.

  `results.cpp`, `string.cpp`,        Results log, string utilities, I/O
  `io.h`, `types.h`                   helpers, and shared types.
  -----------------------------------------------------------------------

------------------------------------------------------------------------

Made by **FlorestDev** --- have fun, and be kind to packets :3

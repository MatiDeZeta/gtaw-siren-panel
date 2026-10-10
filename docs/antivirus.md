# Why your antivirus may flag it

Expect a handful of engines on VirusTotal to flag a release — typically Microsoft as
`Program:Win32/Wacapew.C!ml` and one or two generic machine-learning scorers such as Cynet's
`Malicious (score: 100)`. Here is the honest account of why, including the part that is
genuinely awkward.

## Reading those names

**`Program:Win32/Wacapew.C!ml`** — the prefix carries the meaning. `Program:` is Microsoft's
*potentially unwanted application* class, their lowest tier, and explicitly not malware. Compare
`Trojan:`, `Backdoor:` or `VirTool:`, none of which this gets. The `!ml` suffix means a machine
learning model guessed, rather than a signature matching anything known. Wacapew is one of
Microsoft's best known generic buckets and lands constantly on unsigned installers, developer
tools and game mods.

**`Cynet Malicious (score: 100)`** — a generic machine learning scorer with a high false positive
rate on small unsigned binaries. The 100 is its confidence in its own guess, not a severity.

A couple of generic machine learning hits, no signature detections, on an unsigned native DLL that
almost nobody has run yet, is close to the floor for a file of this kind. That is not proof it is clean.
It is not evidence that it is dirty either. The checks further down are worth more than the count.

## The awkward part

Most mods get flagged simply for being unsigned and new. This one has a real behavioural reason on
top of that, and it is better stated than glossed over:

**Attaching to a browser's remote debugging port is a documented technique for stealing
credentials.** Infostealer malware does exactly this to lift cookies and session tokens out of
Chrome. A model looking at this binary sees WinHTTP WebSocket calls, the strings
`Page.createIsolatedWorld` and `Runtime.evaluate`, a loopback address and a port number, and finds
a partial match to that family. That is a fair thing for a scanner to notice.

What separates this from that family is what it does with the connection, all of which is readable
in the source:

- It connects to loopback only, and refuses any endpoint that is not on loopback, checked in
  `src/cdp/client.cpp` and covered by `tests/wsurl_test.cpp`.
- It attaches to FiveM's own interface and reads the HUD messages already being delivered to it.
- It makes no outbound connection, downloads nothing, and has no update check.
- It never sends a message, never invokes an interface callback, never calls a game native, and
  never reads or writes game memory.
- It touches no browser profile, no cookie store and no credential store.

Expect this plugin to keep attracting heuristics more than an ordinary mod would. That is inherent
to how it gets the data, not something that can be engineered away.

## What you can do

- **Check the file is the one that was built.** Every release is built by GitHub Actions from the
  source in this repository, the build is verified reproducible in CI, and the release notes list
  the SHA-256. Compare it against your download.

  If someone hands you a `gtaw-siren-panel.asi` from anywhere else and the hash does not match a
  release, do not run it. A tampered copy is the one real risk here, and it is the risk this
  check actually addresses.

- **Check it is loadable and is what it claims.** `tests/verify_asi.py gtaw-siren-panel.asi` confirms it
  is a native 64-bit DLL with no CLR header, exports nothing, has NX, ASLR and high-entropy ASLR
  set, and carries the FX_ASI_BUILD stamps.

- **Build it yourself.** `build.bat` needs only the free Visual Studio Build Tools, and there is
  nothing to restore first because the project has no dependencies. Then the file is one you made.

- **Read it.** It is about 2,500 lines with no third-party code in it at all; the only other
  files compiled in are the panel images, which are data and are checked as such. The injected script
  is one string literal in `src/hud/payload.cpp`; the whole network path is `src/cdp/`.

- **Report the false positive.** If Defender flagged it, Microsoft's
  [false positive form](https://www.microsoft.com/en-us/wdsi/filesubmission) usually clears an
  `!ml` detection within a few days, for everyone rather than just you.

## How the file is packaged

Two things generic machine-learning models weigh were dealt with from the first build, since
both are packaging rather than behaviour:

- **No large encoded blob in the code.** The panel images are plain `RCDATA` resources, the PNG
  files as they are, which is how every DLL carries its images, rather than base64 string
  literals in `.rdata` that look like a packed or dropped payload to a model that cannot tell
  they are pictures.
- **A sane build date.** `/Brepro` writes a content hash where the build date goes, and read as
  a date it lands decades in the future, a classic anomaly score. The header carries the
  commit's time instead, which is just as reproducible.

The debugging-port strings above remain, because they are how the plugin gets its data.

## What this project will not do

It will not obfuscate, pack, or otherwise dress the file up to slip past scanners. That is what
actual malware does, it makes detections worse rather than better, and it would destroy the one
thing that makes a plugin like this checkable: that you can read every line of what it does.

[Back to the README](../README.md)

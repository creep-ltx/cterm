# FTP extras in the file browser (2026-10-15)

The design notes of the 2026-10-15 helper for the file browser (Parts/Files):
local times, active FTP, upload resume, whole drawers, FTPS, and Shift's
key bar. For the main session to fold into design.md. README's "Files on
other computers" section has the user's side.

## What was built

1. **Local times.** Dates the host gives in UTC (MLSD's `modify=`, MDTM,
   SFTP's mtime) are shown in the Amiga's time; LIST's dates are the host's
   own clock and stay as given. A file fetched gets its date (SetFileDate):
   the listing's when it was UTC, else MDTM's (one SIZE-like round trip per
   file, quiet; a host without MDTM - `nomdtm` - gets the listing's own
   clock). `REnt` grew `when` (the Amiga's seconds since 1978, 0 not known)
   and `tz` (0 none, 1 the host's clock, 2 from UTC); `ent_when()` fills
   both and the shown text.
   - **The zone:** `tz_east(utc)` in files.c: `ENV:TZ` when set, in POSIX
     form (`CET-1CEST,M3.5.0,M10.5.0/3`, `<+0530>-5:30`, `EST5EDT,M3.2.0,
     M11.1.0`: std name, hours *west*, summer name, optional offset,
     `,Mm.w.d[/time]` start and end), else locale.library's
     `loc_GMTOffset` (no summer time). Read once per part load.
2. **Active FTP.** `data_open()` tries passive (EPSV, then PASV - both now
   quiet when refused); when that fails (refused, or the connect to its
   port fails) it says so once and sets `c->active`: `port_open()` binds a
   listening socket on the control socket's own address (getsockname) and
   sends `PORT h1,..,p2`; after the command's 1xx, `data_go()` accepts the
   host's call (30 s). The command line's `passive` flips `c->active`.
   No EPRT (the Amiga stacks are IPv4).
3. **Upload resume.** REST is now sent after PASV/PORT, right before RETR
   or STOR (as curl does). An upload that stops or fails remembers its
   host name in `c->upart`; the next upload of that name asks SIZE and goes
   on from the host's size: REST n + STOR, or APPE when REST is refused
   (quietly). SFTP opens with `how` 2 (append) and seeks. The replace
   question got a third answer, `g` ("only what is missing", `upmode` 1):
   a file there as long is left out ("there already"), a shorter one goes
   on, a longer one is left as it is. ARexx PUT always replaces (upmode
   0) but still goes on with `upart`.
4. **Whole drawers.** `struct Job` is now a list of `XItem`s (name, its
   name on the other side `rel`, size, when, dir, tz, exist, deep) with a
   string pool; `job_add()`, `job_walk()` and `job_free()` are files.c's.
   `job_walk()` appends what every drawer in the job holds after it
   (breadth first: a drawer always before its insides; deleting walks the
   list backwards). The host's drawers are listed into a scratch panel
   (`c->lp`, which `ent_add`/`ent_when`/`sf_list` now honour) with
   `MLSD path`/`LIST path` or SFTP's opendir of the path; the Amiga's with
   ExNext. Links are not followed (no loops); depth 30 and 20000 entries at
   most. A download item `deep` goes into `dir/` + its path, each part
   made safe (`local_path`); a drawer item is made (or used when there);
   inside a drawer copied again a file of the same size is "there
   already" (so a stopped drawer copy goes on). An upload drawer is MKD
   (quiet - `hush` - when it may be there). F5, `get`/`put` (a named
   drawer whole; `-r` lets a pattern take drawers), ARexx GET/PUT. Delete:
   F8 and `rm` delete a drawer with all in it after a question that says
   so ("Delete the drawer X and all in it, on the host? (y/n)"); ARexx
   DELETE still only removes an empty drawer (a script gets no question).
5. **FTPS** (explicit). See below.
6. **Shifted key bar.** keys.c's `browser_key` gives Shift+F1..F10 as
   `KM_SF1`..+9 (cterm.h; before, Shift was dropped and Shift+F5 was F5),
   bindable as `<S-F5>` in `[files]`; and the Shift keys' down/up (raw
   $60/$61, up events too) reach the part as `KM_SHIFTDN`/`KM_SHIFTUP` while
   the panels are up. The bar (and a click on it) then shows/uses Shift's
   layer. Default: `<S-F6> rename` (mc's), in keymap.c's defbinds.

## FTPS

- `src/ftps.c` + `ftps.h`, linked into Parts/Files with the AmiSSL SDK's
  headers only (`-I third_party/AmiSSL/Developer/include`); the library
  bases are the part's own BSS (`nm`: B, so the part check passes).
  OpenSSL's headers stay out of ftp.c.
- Place kind `FK_FTPS` (book.h) - phone book `kind=ftps` / "ftps (files)",
  `ftps://` in book_tidy (so Call, `CTerm FTP ftps://...`, ARexx `FTP
  ftps://...`), net.c's start word passes `ftps://`. Inside the part it is
  `kind FK_FTP` + `c->tls`. Tab and title say "ftps".
- `ftp_open`: after 220, `tls_begin`: AmiSSL opened (first FTPS place;
  counted per place in `tlsup`, closed with the last in `ftps_done`), AUTH
  TLS (no 234: the place is not called, said), handshake, certificate;
  then USER/PASS inside TLS, PBSZ 0, PROT P (refused: given up). The
  control line's put_cmd/get_line try TLS first and wait the way it says.
- Data: the TLS handshake starts **after** the 1xx (as vsftpd/ProFTPD
  expect; a server that handshakes before its 150 would deadlock),
  `SSL_set_session` from the control line (session reuse: verified with
  the test server's log), close_notify (`SSL_shutdown` once) when a
  transfer or listing ended well. `ftp_dread`/`ftp_dwrite` (files.c's
  x_read/x_write for FTP now) check `SSL_pending` before the socket, and
  say which way to wait in `c->dwant`.
- **Certificates:** `tls_trusted` = a peer certificate and
  `SSL_get_verify_result == X509_V_OK` (verify mode NONE, so a bad one is
  not refused by OpenSSL, only not "trusted"; hostname via
  `SSL_set1_host`, an IP via `X509_VERIFY_PARAM_set1_ip_asc`; the store:
  `SSL_CTX_set_default_verify_paths` = AmiSSL:Certs, only when `AmiSSL:`
  exists - FindDosEntry, no requester - and AmiSSL:UserCerts when there).
  Else pinned like ssh's host keys: `ENVARC:CTerm/ftps_hosts`, lines
  `host:port SHA256:<base64 of the DER's SHA-256, no =>` (the same form
  `openssl x509 -outform der | openssl dgst -sha256 -binary | base64`
  gives); new: shown and asked y/n; changed: loud warning, "yes" in full;
  written through `ftps_hosts.new`.
- **CTermSSH refuses FTPS** (`has_ssh`): it has AmiSSL open for ssh
  already in the same task; opening it twice per task was not tried.
- **Ciphers:** ChaCha20-Poly1305 asked first (TLS 1.3 suites and 1.2
  list); groups X25519, P-256, P-384 (no ML-KEM, as wget).

## Traps found

- **Locale's loc_GMTOffset counts minutes WEST** (verified in FS-UAE, OS
  3.2.1: Prefs/Locale map UTC+1 -> -60, UTC+10 -> -600): local = UTC -
  offset*60. A fresh 3.2.1 with no locale prefs says -300 (UTC+5). It has
  no summer time.
- **tls_shake's wait codes are negative** (-2/-3): a first version tested
  `r < 0` for failure and gave up whenever the handshake had to wait - it
  only worked when the host's answer was already there (fast loopback),
  and failed now and then ("[TLS: ]", empty reason).
- **AmiSSL points bsdsocket's errno at AmiSSL_ErrNoPtr** (SBTC_ERRNOLONGPTR
  on CTerm's SocketBase). The variable lives in the part, which is
  unloaded with the last place: after CloseAmiSSL the part sets the
  pointer back to 0 (SocketBaseTagList; the varargs SocketBaseTags macro
  does not compile here: `_sfdc_vararg`).
- A TLS host that closes without close_notify: AmiSSL's read says "wait"
  for ever while the socket reads ready; `t_read` peeks (MSG_PEEK) and
  takes 0 as the end.
- An upload's stop with big socket buffers (FS-UAE's bsdsocket is the
  PC's TCP): the client may have "sent" 2 MB the host has not read; the
  host's SIZE, not CTerm's count, says where to go on (that is why the
  resume asks SIZE).
- Lines in a resumed transfer: a SIZE asked on a line the host had
  dropped went nowhere and the upload started from 0; SIZE now goes
  through `run_op` (called again first).
- The question line must fit 80 columns with the answers: names are cut
  to 16 (start~end) in the replace question.

## Tests (verified 2026-10-15, FS-UAE rig `ctftp`, wasabid 1251)

Test servers: `FTPPORT=2131 tests/ftp/run.sh start [--nopasv|--norest|
--nomlsd|--rate KB]`, `FTPPORT=2132 tests/ftp/run.sh start --tls PEM`
(ftpd.py: PORT/EPRT, `--nopasv`, `--norest`, REST for STOR, AUTH TLS/PBSZ/
PROT, data TLS logged with its session reuse); a private sshd on 2133
(the test sshd's config with that port). The 8 MB A1200 for FTP; the
030/50 + 32 MB (`CONF=030 tests/ftp/uae.sh start`) for FTPS and SFTP.

- Dates (`t-dates.rexx`): locale UTC+10 -> 22:00 for 12:00 UTC; TZ
  `CET-1CEST,...` -> summer 14:00, winter 13:00, today 09:44 (CEST);
  `<+0530>-5:30` -> 17:30; `EST5EDT,...` -> 08:00/07:00; LIST host
  (`--nomlsd`): the host's own text, file dates from MDTM right. The
  fetched files' dates (List) the same.
- ARexx regression `tests/files/t-files.rexx` (on the last tab): all as
  before; 3 MB GET md5-identical.
- Drawers (`t-tree.rexx`): host tree -> RAM:dl (drawer with a space,
  three levels, dates kept), again: all "there already", RC 0; sent back
  into `up/`: `diff -r` identical. The same with `--nopasv` (26 PORTs,
  active only), over FTPS, and over SFTP (CTermSSH, `t-sftptree.rexx`).
  In the panels: F5 on a drawer both ways; the replace question with
  `g` (a truncated file went on, a missing one sent, the rest "there
  already"); F8 on a host drawer and on an Amiga drawer with all in them.
- Upload resume (`t-put.rexx`, Esc while it ran, the server restarted):
  SIZE 429056, REST 429056, STOR from there, md5-identical; with
  `--norest`: REST refused quietly, APPE, md5-identical.
- FTPS (`t-ftps.rexx`, `t-get.rexx`): AUTH TLS, TLS 1.3; the certificate
  question with the same SHA256 openssl prints; y remembered (no question
  next time); a new certificate: the warning, "y" refused, "yes"
  accepted and the file rewritten (one line); `ftps://` to a plain host:
  "no FTPS", nothing sent in clear after; the phone book's "ftps (files)"
  place called from the window; 3 MB md5-identical, stopped with Esc
  and resumed with REST, md5-identical; drawers both ways; data
  connections reuse the session. AmiSSL on the 8 MB machine: "would not
  open" said, nothing else happened. After the FTPS tab closed, plain FTP
  went on fine (errno pointer reset), and memory came back (Avail FLUSH).
- Speed on the emulated 030/50: plain FTP 3 MB in 2.6 s; FTPS 3 MB with
  AES-256-GCM 240 s (12.5 KB/s), with ChaCha20-Poly1305 112 s (27 KB/s);
  opening AmiSSL + the control handshake ~9 s, each data connection's
  handshake ~3-4 s even with session reuse (TLS 1.3 still does ECDHE).
- Shift+F6 opened rename; a build that ignored Shift's release showed
  the shifted bar ("6Rename" alone) - wasabi cannot hold Shift for a grab.
- `make` (the Linux vt/shadow/streams tests): all passed.

## Not verified / not done

- The real A1200 (Emu68, lwIP) - none of this was tried there; active
  FTP through lwIP's bind/listen/accept especially.
- A real FTPS server (vsftpd's `require_ssl_reuse`, ProFTPD, FileZilla
  Server), a CA-signed certificate (`tls_trusted` path), TLS 1.2 hosts.
- FTPS upload resume (same code as FTP's; not run), FTPS with active FTP.
- ARexx DELETE of a non-empty drawer (left as it was on purpose).
- Dates on uploads (MFMT / SFTP setstat) - not done; drawers made on the
  Amiga get the time they were made.
- A host's symbolic links inside a walked drawer are copied as files
  (RETR of a link to a drawer fails, said).
- Implicit FTPS (port 990) - not done (explicit only).
- `ftps://` links in the text (find.c's list) do not open a tab yet: that
  file is the links helper's; add `ftps://` beside `ftp://` there.
- PART_VERSION not raised (PartEnv and partlist.h unchanged). But an old
  Parts/PhoneBook with this CTerm would get chooser index 5 for an ftps
  place: the parts must be installed together (they always are).
- Parts/Files: 53,160 -> 65,388 bytes (+12 KB: walks, jobs, TLS glue,
  time zones). CTerm: +~200 bytes (keys, book).

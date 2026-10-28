# Linux dependency preflight — throwaway

**Dependency build: BLOCKED / NOT ATTEMPTED.** No Qt installer, login, terms acceptance, vcpkg or compiler build was executed. A green workflow means this capture completed, not that the dependency slice passed.

### capture: preflight_only

UTC: 2026-09-27T06:21:30Z
CMake: 4.4.3
Host: Linux 
Candidate lock SHA-256: 3a03191e8be9b8e2989f1f2739b39f8a3961989aadcede676020b1773f7235b7
Runs before project(); project languages remain NONE.

### ci_ImageOS: observed

ubuntu26

### ci_ImageVersion: observed

20260920.143.1

### ci_RUNNER_OS: observed

Linux

### ci_RUNNER_ARCH: observed

X64

### ci_GITHUB_SHA: observed

4959243fefdf0737e655985e300ff9f20e12d9ac

### ci_GITHUB_RUN_ID: observed

36299849291

### ci_GITHUB_RUN_ATTEMPT: observed

1

### host_resources: snapshot_only

Logical processors: 4; total physical memory: 15983 MiB. Not a performance reference host or capacity guarantee.

### tool_git: observed

git version 2.55.0

### tool_ninja: observed

1.13.2

### tool_gcc: observed

gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0
Copyright (C) 2025 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is NO
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

### tool_g++: observed

g++ (Ubuntu 15.2.0-16ubuntu1) 15.2.0
Copyright (C) 2025 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is NO
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

### tool_make: observed

GNU Make 4.4.1
Built for x86_64-pc-linux-gnu
Copyright (C) 1988-2023 Free Software Foundation, Inc.
License GPLv3+: GNU GPL version 3 or later <https://gnu.org/licenses/gpl.html>
This is free software: you are free to change and redistribute it.
There is NO WARRANTY, to the extent permitted by law.

### tool_pkg-config: observed

2.5.1

### tool_autoconf: observed

autoconf (GNU Autoconf) 2.72
Copyright (C) 2023 Free Software Foundation, Inc.
License GPLv3+/Autoconf: GNU GPL version 3 or later
<https://gnu.org/licenses/gpl.html>, <https://gnu.org/licenses/exceptions.html>
This is free software: you are free to change and redistribute it.
There is NO WARRANTY, to the extent permitted by law.

Written by David J. MacKenzie and Akim Demaille.

### tool_automake: observed

automake (GNU automake) 1.18.1
Features: subsecond-mtime

Copyright (C) 2025 Free Software Foundation, Inc.
License GPLv2+: GNU GPL version 2 or later
  <https://gnu.org/licenses/gpl-2.0.html>
This is free software: you are free to change and redistribute it.
There is NO WARRANTY, to the extent permitted by law.

Written by Tom Tromey <tromey@redhat.com>
       and Alexandre Duret-Lutz <adl@gnu.org>.

### tool_libtool: missing

Not found on PATH; no installation attempted.

### tool_nasm: missing

Not found on PATH; no installation attempted.

### tool_meson: missing

Not found on PATH; no installation attempted.

### tool_python3: observed

Python 3.14.4

### tool_curl: observed

curl 8.18.0 (x86_64-pc-linux-gnu) libcurl/8.18.0 OpenSSL/3.5.5 zlib/1.3.1 brotli/1.2.0 zstd/1.5.7 libidn2/2.3.8 libpsl/0.21.2 libssh2/1.11.1 nghttp2/1.68.0 librtmp/2.3 mit-krb5/1.22.1 OpenLDAP/2.6.10
Release-Date: 2026-01-07, security patched: 8.18.0-1ubuntu2.5
Protocols: dict file ftp ftps gopher gophers http https imap imaps ipfs ipns ldap ldaps mqtt pop3 pop3s rtmp rtsp scp sftp smb smbs smtp smtps telnet tftp ws wss
Features: alt-svc AsynchDNS brotli GSS-API HSTS HTTP2 HTTPS-proxy IDN IPv6 Kerberos Largefile libz NTLM PSL SPNEGO SSL threadsafe TLS-SRP UnixSockets zstd

### tool_tar: observed

tar (GNU tar) 1.35
Copyright (C) 2023 Free Software Foundation, Inc.
License GPLv3+: GNU GPL version 3 or later <https://gnu.org/licenses/gpl.html>.
This is free software: you are free to change and redistribute it.
There is NO WARRANTY, to the extent permitted by law.

Written by John Gilmore and Jay Fenlason.

### tool_unzip: unavailable_or_partial

Exit: 10


### tool_zip: observed

Copyright (c) 1990-2008 Info-ZIP - Type 'zip "-L"' for software license.
This is Zip 3.0 (July 5th 2008), by Info-ZIP.
Currently maintained by E. Gordon.  Please send bug reports to
the authors using the web page at www.info-zip.org; see README for details.

Latest sources and executables are at ftp://ftp.info-zip.org/pub/infozip,
as of above date; see http://www.info-zip.org/ for other sites.

Compiled with gcc 15.2.0 for Unix (Linux ELF).

Zip special compilation options:
	USE_EF_UT_TIME       (store Universal Time)
	BZIP2_SUPPORT        (bzip2 library version 1.0.8, 13-Jul-2019)
	    bzip2 code and library copyright (c) Julian R Seward
	    (See the bzip2 license for terms of use)
	SYMLINK_SUPPORT      (symbolic links supported)
	LARGE_FILE_SUPPORT   (can read and write large files on file system)
	ZIP64_SUPPORT        (use Zip64 to store large files in archives)
	UNICODE_SUPPORT      (store and read UTF-8 Unicode paths)
	STORE_UNIX_UIDs_GIDs (store UID/GID sizes/values using new extra field)
	UIDGID_NOT_16BIT     (old Unix 16-bit UID/GID extra field not used)
	[encryption, version 2.91 of 05 Jan 2007] (modified for Zip 3)

Encryption notice:
	The encryption code of this program is not copyrighted and is
	put in the public domain.  It was originally written in Europe
	and, to the best of our knowledge, can be freely distributed
	in both source and object forms from any country, including
	the USA under License Exception TSU of the U.S. Export
	Administration Regulations (section 740.13(e)) of 6 June 2002.

Zip environment options:
             ZIP:  [none]
          ZIPOPT:  [none]

### distribution: observed_not_immutable

PRETTY_NAME="Ubuntu 26.04.1 LTS"
VERSION_ID="26.04"
ID=ubuntu

### kernel: observed

Linux 7.0.0-1012-azure x86_64 GNU/Linux

### glibc: observed

glibc 2.43

### disk_free: observed

Filesystem     1024-blocks     Used Available Capacity Mounted on
/dev/root        151361008 55527452  95817172      37% /

### host_packages: unavailable_or_partial

Exit: 1
autoconf	2.72-3.1ubuntu2
automake	1:1.18.1-3build1
curl	8.18.0-1ubuntu2.5
g++	4:15.2.0-5ubuntu1
gcc	4:15.2.0-5ubuntu1
git	1:2.55.0-0ppa1~ubuntu26.04.2
libatspi2.0-0t64:amd64	2.60.4-0ubuntu0.1
libc6:amd64	2.43-2ubuntu2.4
libdbus-1-3:amd64	1.16.2-2ubuntu4
libfontconfig1:amd64	2.17.1-3ubuntu1
libfreetype6:amd64	2.14.2+dfsg-1ubuntu0.1
libgl1:amd64	1.7.0-3
libstdc++6:amd64	16-20260322-1ubuntu1
libtool	2.5.4-9
libwayland-client0:amd64	1.24.0-2
libx11-6:amd64	2:1.8.13-1
libx11-xcb1:amd64	2:1.8.13-1
libxcb-randr0:amd64	1.17.0-2ubuntu1
libxcb1:amd64	1.17.0-2ubuntu1
libxkbcommon0:amd64	1.13.1-1
pkg-config:amd64	2.5.1-4
tar	1.35+dfsg-4ubuntu0.4
unzip	6.0-29ubuntu1
xvfb	2:21.1.22-1ubuntu1
zip	3.0-15ubuntu3

### qt_auth: missing_or_not_declared

No CI secret-presence signal. An authorized installer account is required for the future provisioner. No account files read.

### qt_terms: not_authorized_by_this_probe

This preflight has no license/obligation acceptance scope. It cannot execute an installer even when an account is available.

### installer_checksum: public_metadata_fetched

URL: https://download.qt.io/archive/online_installers/4.11/qt-online-installer-linux-x64-4.11.0.run.sha256
Metadata SHA-256: c51e1990f31446777c57ba8e06929982e506acba6081db23e27bd35036156ea8

### installer_identity: declared_sha256_matches

Versioned URL: https://download.qt.io/archive/online_installers/4.11/qt-online-installer-linux-x64-4.11.0.run
Declared binary SHA-256: 40b76bdf74f6a396341efb70ae2e754fcd878474babb6cd9d7f07eff12a85c62
Binary not downloaded or executed; digest is publisher metadata, not a local binary verification.

### qt_repository_metadata: public_metadata_fetched

URL: https://download.qt.io/online/qtsdkrepository/linux_x64/desktop/qt6_6112/qt6_6112/Updates.xml
Metadata SHA-256: 5cc522ccfe18d76a6e703bfb33704cdfff3ed24695ca3d449c4d710bc6385e32

### qt_package: exact_metadata_snapshot_matches

Package: qt.qt6.6112.linux_gcc_64
Version: 6.11.2-0-202608131018
Declared dependencies: qt.tools.qtcreator, qt.qt6.6112.doc, qt.qt6.6112.examples
This locks one metadata snapshot, not the full installer dependency closure or upstream retention.

### archive_0_checksum: public_metadata_fetched

URL: https://download.qt.io/online/qtsdkrepository/linux_x64/desktop/qt6_6112/qt6_6112/qt.qt6.6112.linux_gcc_64/6.11.2-0-202608131018qtbase-Linux-RHEL_9_6-GCC-Linux-RHEL_9_6-X86_64.7z.sha256
Metadata SHA-256: c52ba9dc2f3609e10f6fb94c0e1e8e21ebc9ca25f180208a58c6b4b07153b888

### archive_0: declared_sha256_matches

URL: https://download.qt.io/online/qtsdkrepository/linux_x64/desktop/qt6_6112/qt6_6112/qt.qt6.6112.linux_gcc_64/6.11.2-0-202608131018qtbase-Linux-RHEL_9_6-GCC-Linux-RHEL_9_6-X86_64.7z
Declared binary SHA-256: 0f86f13b161141e77b1d056b54e2b6fc40fb16f243e71123346f9fb35d418027
Archive not downloaded or installed.

### archive_1_checksum: public_metadata_fetched

URL: https://download.qt.io/online/qtsdkrepository/linux_x64/desktop/qt6_6112/qt6_6112/qt.qt6.6112.linux_gcc_64/6.11.2-0-202608131018qtsvg-Linux-RHEL_9_6-GCC-Linux-RHEL_9_6-X86_64.7z.sha256
Metadata SHA-256: 8e7a0a8163525e5bee7b7f34e73537f0144fe601478b2c5b693a54f4ed90cf5a

### archive_1: declared_sha256_matches

URL: https://download.qt.io/online/qtsdkrepository/linux_x64/desktop/qt6_6112/qt6_6112/qt.qt6.6112.linux_gcc_64/6.11.2-0-202608131018qtsvg-Linux-RHEL_9_6-GCC-Linux-RHEL_9_6-X86_64.7z
Declared binary SHA-256: 939fe0e5d49d11d6d3eceea0184219703c25a1f8dc75f45e99071ca244824a0d
Archive not downloaded or installed.

### archive_2_checksum: public_metadata_fetched

URL: https://download.qt.io/online/qtsdkrepository/linux_x64/desktop/qt6_6112/qt6_6112/qt.qt6.6112.linux_gcc_64/6.11.2-0-202608131018qtdeclarative-Linux-RHEL_9_6-GCC-Linux-RHEL_9_6-X86_64.7z.sha256
Metadata SHA-256: bade06947da553e054a5010f39abad3bf7c70a46c714391c5e47c21856f4cd43

### archive_2: declared_sha256_matches

URL: https://download.qt.io/online/qtsdkrepository/linux_x64/desktop/qt6_6112/qt6_6112/qt.qt6.6112.linux_gcc_64/6.11.2-0-202608131018qtdeclarative-Linux-RHEL_9_6-GCC-Linux-RHEL_9_6-X86_64.7z
Declared binary SHA-256: 5f0ce87c077f749723dbb6e923142adb860c146ecaf93c68197feda5307f22dd
Archive not downloaded or installed.

### archive_3_checksum: public_metadata_fetched

URL: https://download.qt.io/online/qtsdkrepository/linux_x64/desktop/qt6_6112/qt6_6112/qt.qt6.6112.linux_gcc_64/6.11.2-0-202608131018qtdoc-Linux-RHEL_9_6-GCC-Linux-RHEL_9_6-X86_64.7z.sha256
Metadata SHA-256: b79803a6f7180f084e316a59dbeb98b6d09e7b8f34cdb975d08c13975eab72da

### archive_3: declared_sha256_matches

URL: https://download.qt.io/online/qtsdkrepository/linux_x64/desktop/qt6_6112/qt6_6112/qt.qt6.6112.linux_gcc_64/6.11.2-0-202608131018qtdoc-Linux-RHEL_9_6-GCC-Linux-RHEL_9_6-X86_64.7z
Declared binary SHA-256: 0f4cad1f99bdcbe8c12540fbc195be64b5010c83e184315bd19614905f114600
Archive not downloaded or installed.

### archive_4_checksum: public_metadata_fetched

URL: https://download.qt.io/online/qtsdkrepository/linux_x64/desktop/qt6_6112/qt6_6112/qt.qt6.6112.linux_gcc_64/6.11.2-0-202608131018qttools-Linux-RHEL_9_6-GCC-Linux-RHEL_9_6-X86_64.7z.sha256
Metadata SHA-256: 66d200e3727ed5b4a8d6b82de576db94609b377d4e91c82962d295dd9d8a29ce

### archive_4: declared_sha256_matches

URL: https://download.qt.io/online/qtsdkrepository/linux_x64/desktop/qt6_6112/qt6_6112/qt.qt6.6112.linux_gcc_64/6.11.2-0-202608131018qttools-Linux-RHEL_9_6-GCC-Linux-RHEL_9_6-X86_64.7z
Declared binary SHA-256: 42d5f3dbfc25647d9d95ef8b64401dc7e3ef7c83a39a29b548dfa0985f71c0ca
Archive not downloaded or installed.

### archive_5_checksum: public_metadata_fetched

URL: https://download.qt.io/online/qtsdkrepository/linux_x64/desktop/qt6_6112/qt6_6112/qt.qt6.6112.linux_gcc_64/6.11.2-0-202608131018qttranslations-Linux-RHEL_9_6-GCC-Linux-RHEL_9_6-X86_64.7z.sha256
Metadata SHA-256: cce7d3fb35d5aad458ed4e1b467a77bdda31c1a15d83a7cffa2ab9230b493583

### archive_5: declared_sha256_matches

URL: https://download.qt.io/online/qtsdkrepository/linux_x64/desktop/qt6_6112/qt6_6112/qt.qt6.6112.linux_gcc_64/6.11.2-0-202608131018qttranslations-Linux-RHEL_9_6-GCC-Linux-RHEL_9_6-X86_64.7z
Declared binary SHA-256: fcef8e3f3a76367523ed68cc2f6e46ff80cedd41b3a8d6ad464515d237c7777e
Archive not downloaded or installed.

### archive_6_checksum: public_metadata_fetched

URL: https://download.qt.io/online/qtsdkrepository/linux_x64/desktop/qt6_6112/qt6_6112/qt.qt6.6112.linux_gcc_64/6.11.2-0-202608131018qtwayland-Linux-RHEL_9_6-GCC-Linux-RHEL_9_6-X86_64.7z.sha256
Metadata SHA-256: 5c13c63b947a80870c04fb906a6d6e9c3d3d62b0b9b328555b7da87ae9fb00a2

### archive_6: declared_sha256_matches

URL: https://download.qt.io/online/qtsdkrepository/linux_x64/desktop/qt6_6112/qt6_6112/qt.qt6.6112.linux_gcc_64/6.11.2-0-202608131018qtwayland-Linux-RHEL_9_6-GCC-Linux-RHEL_9_6-X86_64.7z
Declared binary SHA-256: 7a541f8d36f93a1e337e6d3fc040537c04fc70a65a04b6078738dd0f59ddf83d
Archive not downloaded or installed.

### archive_7_checksum: public_metadata_fetched

URL: https://download.qt.io/online/qtsdkrepository/linux_x64/desktop/qt6_6112/qt6_6112/qt.qt6.6112.linux_gcc_64/6.11.2-0-202608131018icu-linux-Rhel8.6-x86_64.7z.sha256
Metadata SHA-256: 264d4dc8aaff79c208d4dcdb17799c567d1e75ed1458599709e02f2d8eeb12cd

### archive_7: declared_sha256_matches

URL: https://download.qt.io/online/qtsdkrepository/linux_x64/desktop/qt6_6112/qt6_6112/qt.qt6.6112.linux_gcc_64/6.11.2-0-202608131018icu-linux-Rhel8.6-x86_64.7z
Declared binary SHA-256: 111bdae30a66fff6ef65620e95766170fa5a6f425c360ea33b79cb2ec7e2fd86
Archive not downloaded or installed.

### artifact_closure: incomplete

Eight base SDK archive identities are candidate-pinned. Parent/license/package scripts, Creator/docs/examples dependencies, repository redirects, retention and enforcement of the installer's actual consumed bytes remain unproved.

### vcpkg_graph: not_provisioned

Candidate research registry ee6a47dac031930715bea2e6dfb9ad60caaa5d83; no complete tool/registry/overlay lock is supplied here. No vcpkg executable invoked. Qt remains outside its graph.

### ffms2_private_abi: not_built

Required fork 45d5f72100d88c52acdd54bfedcc0315a44c735d plus indexing_additional.cpp blob be378044fff07bac704f04dc57caa4fc5f95691e need one owned recipe and compatible FFmpeg selection. No distro-library substitute.

### qualification: not_attempted

No C++ SDK compile/link, Quick scene, Linguist round-trip, FFMS2 private extension, libass render, cold/warm reconstruction, deployment, corruption case, Fedora/Wayland/a11y or performance pass. Host image and tool versions are observed, not an immutable build environment.

### public_metadata_validation: passed_metadata_only

Repository metadata and nine published binary checksum declarations match the candidate. Actual binary content and installer enforcement are unverified.


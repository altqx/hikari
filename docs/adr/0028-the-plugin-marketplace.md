---
status: accepted
amends: 0022
---

# The plugin marketplace

On 2026-11-02 altqx settled the hosted HikariSub plugin marketplace in two grilling rounds and then approved its mockups ([PL10 #664](https://github.com/altqx/hikari/issues/664)). All three were "yes to all". The mockups are on the `mockups` branch of [altqx/hikari-marketplace](https://github.com/altqx/hikari-marketplace/tree/mockups).

This amends [ADR 0022](0022-hikarisub-plugins.md). Its "Hosting later" bullet expected author accounts, web upload and a review queue. Instead, the marketplace is Git-first with no accounts. The rest of ADR 0022's "Distribution and signatures" section stands: the one signed index, the author keys pinned at first install, the review of permission increases, automatic updates, revocation, privacy, sideloads and the v1 scope.

## Address and repositories

- **Address:** https://marketplace.hikarisub.org, on Cloudflare, in the same account as hikarisub.org.
- **Two public MIT repositories:**
  - [altqx/hikari-marketplace](https://github.com/altqx/hikari-marketplace) holds the website's source.
  - [altqx/hikari-plugins](https://github.com/altqx/hikari-plugins) is the registry. It holds the listings, the CI that watches releases, the index signing and the R2 upload.
- Authors' pull requests touch only hikari-plugins, never the site's code.

## Submitting and updating

- **The first listing is a pull request.** It adds `plugins/<id>.json` to hikari-plugins, naming the author's GitHub repository, their minisign public key, one or two categories and their handle. We review it.
- **Updates are GitHub releases.** The author attaches `<id>-<version>.hikariplugin` and its `.minisig`. Registry CI finds the release and runs these checks:
  - the manifest schema;
  - the API number;
  - the size;
  - the path rules;
  - the author's signature, against the pinned key;
  - the permission diff.

  When they pass, CI copies the package to R2 and signs the index again.
- **More permissions go back to us.** A release that asks for more (a network host, a folder, `documents.all` or the unrestricted tier) makes CI open a pull request for our review instead of publishing.
- **Ids:** the first segment of the dotted manifest id is the author's handle, as in `alice.karaoke-tools`. The handle is claimed with the author's first accepted listing and tied to their key. Renaming a plugin, transferring it or changing its key goes through us.
- Web upload with accounts can come later, if authors ask for it.

## Storage and signing

- **Where packages live:** in Cloudflare R2, at `https://marketplace.hikarisub.org/p/<id>/<version>.hikariplugin`. The signed `index.json` and its `.minisig` sit beside them.
  - Nothing is downloaded from an author's own host.
  - Versions are never deleted, only revoked.
- **The marketplace key:** GitHub Actions in hikari-plugins signs `index.json` with it, and it is kept there as a repository secret.
  - The owner creates the key offline and hands it over through `request_secret`.
  - It is separate from MS1's update key ([MS1 #656](https://github.com/altqx/hikari/issues/656)).
  - No signing key is ever kept in a Worker.
  - CI tests use a throwaway key.

## Listing content

- **The listing's media come from the package,** so they are versioned with the plugin, and a sideloaded plugin has them too. The [PL1 manifest](../schemas/hikari-plugin.schema.json) gains three optional fields:
  - `summary`: the one-line summary, up to 100 characters;
  - `icon`: a 256-px PNG or an SVG;
  - `screenshots`: up to six 16:9 images.

  A `README.md` at the package root is the plugin page's text.
- **The changelog is the GitHub release notes,** which CI copies into the index.
- **A plugin without an icon** shows a monogram, tinted by its id.
- **Categories:** Timing, Typesetting, Karaoke, Translation, Styles and fonts, Video and audio, Import and export, Quality checks, Utilities. A listing picks one or two.

## Website

- **How it's built:** static, generated from `index.json` at build time and rebuilt whenever the index changes. Search runs in the browser, and the site is served by its own Worker.
- **Its look:** it uses hikari-site's theme tokens, IBM Plex, header and footer, and links to hikarisub.org for guides and downloads.
  - **Header:** "HikariSub | Marketplace", then Browse, Publish a plugin, Guides and Download HikariSub. Search, the theme switch and a GitHub link to hikari-plugins are on the right.
  - **Footer:** says that plugins are made by their authors, and links Report a problem, Security, Listings on GitHub and Site source.
- **Home:** search, the categories, New and Recently updated.
  - Featured is our pick, and it stays hidden while it is empty, as it is at launch.
  - Once it has a plugin, Featured shows above New as up to three cards.
- **Plugin page:**
  - icon, name, author and summary;
  - the tier badge, which is always shown here ("Restricted" or "Unrestricted"). Cards and lists show only "Unrestricted";
  - "Install in HikariSub", with "Download .hikariplugin" as the second button and "Needs HikariSub X or later" below them;
  - a screenshot carousel and the rendered README;
  - "What it can do", the permissions box;
  - version history with changelogs, where a revoked version is struck through with its reason;
  - details: version, minimum HikariSub and API, downloads, size, licence and categories;
  - links to the source code, the signing key and "Report a problem".
- **The permissions box** gives one plain-language line per permission, with the manifest's reason under it, then one line on what every plugin may do. For an unrestricted plugin, the box is a single "Full access to your computer" warning. On a phone, the box comes straight after the install buttons.
- **Author page:** a monogram, the handle, the GitHub link, the listing date and the start of the pinned key, then the author's plugins. We host no avatars.
- **Publish a plugin:** five steps (write the plugin, make a key, open the listing pull request, publish a release, how updates work), linking PL9's authoring guide ([#421](https://github.com/altqx/hikari/issues/421)).
- **Download counts:** rounded ("1.2k") and taken from R2 or Worker request counts per package version. No IP address is stored, and HikariSub sends nothing. Counts appear only on the website.

## Installing from the web

"Install in HikariSub" opens `hikarisub://plugin/install?id=<id>` ([MK1 #667](https://github.com/altqx/hikari/issues/667)). HikariSub looks the id up in its own signed index and opens its normal install confirmation.

With offline mode on, or plugin updates turned off, the link opens a short dialog instead. It names the cause and offers to change it, so nothing ever installs silently.

## In HikariSub

The plugin manager ([PL4 #416](https://github.com/altqx/hikari/issues/416), [PL6 #418](https://github.com/altqx/hikari/issues/418)) follows the approved mockups.

- **Two views, Installed and Marketplace,** chosen with segmented buttons. Each view has a list and a details pane.
  - Installed shows a count while an update waits for the user.
  - The Marketplace view has a search field and one category box above its list.
- **An icon-only tool strip, named on hover:** Install from file…, Check for updates, and Plugin updates. Plugin updates opens a popover with the two global switches:
  - "Check for plugin updates", which follows offline mode and says that the installed list is never sent;
  - "Install updates automatically".

  Settings has no second copy of these switches. Each plugin has its own "Install updates automatically" check in its details.
- **The install confirmation always opens,** even for a plugin that asks for nothing extra.
  - It lists the permissions, then the line on what every plugin may do, then who signed the package.
  - For the unrestricted tier, the button is "Install with full access" in the danger style, with no extra checkbox.
- **A held update:** an update that asks for more waits. Its list row says "<version> waits for you", and a bar in the details offers Review….
  - The prompt tags the new items "New" and marks the others "allowed now".
  - "Keep <current version>" skips only that version, so the next one asks again.
- **Roll back** sits beside the version and goes back one step. Automatic updates then skip the version the user left and take the next one. A toast confirms the roll back and offers Undo.
- **Revoked:** at the next check, HikariSub turns the plugin off and greys its row with "Revoked".
  - A red bar gives the reason, and the buttons are "Install <last good version>" and Uninstall. One toast says it happened.
  - When a whole plugin is revoked, the only button is Uninstall.
- **Sideloads** carry an "Unverified" badge in the list and the details, with the file name beside the version.
  - When the marketplace lists the same id with the same author key, an info bar offers "Move to the marketplace" or "Not now".
  - "Not now" hides the offer until the marketplace has a newer version.

## Reports and revocation

- **"Report a problem"** opens a pre-filled issue in hikari-plugins.
- **Malware reports** go to `security@hikarisub.org`. Cloudflare Email Routing forwards them to the owner, who verifies the destination once. Both repositories have a SECURITY.md.
- **A revocation** is a pull request that marks a version or a whole plugin revoked, with a reason. The index rebuilds within minutes.

## Launch

We build now and deploy to a preview URL. marketplace.hikarisub.org goes public only with the HikariSub release that ships PL6 and MK1.

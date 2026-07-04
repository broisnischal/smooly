# Code signing (free, via SignPath Foundation)

smooly is signed with a free OV certificate from the **SignPath Foundation** program
for open-source projects, so Windows SmartScreen / "Unknown publisher" warnings go away.
Signing runs entirely inside the GitHub Actions release pipeline — no key ever touches
a developer machine.

## One-time setup

1. **Make the repo public** and add an OSI license (this repo uses MIT — see `LICENSE`).

2. **Apply to SignPath Foundation** (free OSS signing):
   https://signpath.org/apply — connect the GitHub repository. Approval is manual and
   usually takes a few days.

3. Once approved, in the SignPath web console for your organization:
   - Create (or note) a **Project** with slug `smooly`.
   - Create an **Artifact configuration** with slug `initial` that signs:
     - `smooly-<version>-setup.exe` — Authenticode.
     - `smooly.exe` inside `smooly-<version>-portable.zip` — Authenticode (nested).
   - Create a **Signing policy** with slug `release-signing` bound to the Foundation certificate.
   - Create a **CI user API token**.

4. In GitHub → repo → **Settings → Secrets and variables → Actions**:
   - Secret **`SIGNPATH_API_TOKEN`** = the SignPath CI user token.
   - Variable **`SIGNPATH_ORGANIZATION_ID`** = your SignPath organization id (GUID).

5. Tag a release: `git tag v1.0.0 && git push origin v1.0.0`.
   `.github/workflows/release.yml` builds the installer + portable zip, submits them to
   SignPath, waits for the signed artifacts, and publishes them to the GitHub Release.

## Before approval

Until SignPath approves the project, remove (or comment out) the **"Sign with SignPath"**
step in `release.yml` and change the **"Publish GitHub Release"** step to upload from
`dist/` instead of `dist-signed/`. Those builds are unsigned (SmartScreen may warn); switch
back once signing is live.

## Alternatives (also free for OSS)

- **SignPath Foundation** — recommended, used here (managed, no key handling).
- **Sigstore / cosign** — signs artifacts but is not Authenticode, so it does *not* remove
  Windows SmartScreen warnings.
- **Self-signed** — only useful for internal machines that trust your test cert.

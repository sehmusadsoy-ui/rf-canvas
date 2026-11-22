# Security checklist

Per-change checklist derived from [MASTER_SECURITY_POLICY.md](MASTER_SECURITY_POLICY.md).
Mark each item PASS / FAIL / PENDING / BLOCKED / NOT APPLICABLE (with reason).

## Every pull request
- [ ] Work is on a branch; no direct push to `main`
- [ ] No secrets, keys, passwords, tokens or personal data in diff, logs or screenshots
- [ ] Inputs from users, files, UART/I2C/SPI, network and other agents are validated and bounded
- [ ] Buffers, indices and integer arithmetic are bounds-checked; sensitive buffers wiped with a non-elidable wipe
- [ ] Error paths fail secure; resources (memory, files, threads, hardware) are released
- [ ] New or upgraded dependencies: source verified, pinned, lockfile updated, advisories checked, major versions reviewed
- [ ] GitHub Actions: pinned to full commit SHA, explicit minimal `permissions`, no untrusted input interpolated into `run:`
- [ ] Required CI checks pass; CodeQL/SAST and dependency review have no new High/Critical findings
- [ ] Tests added or updated; only tests that actually ran are reported as PASS
- [ ] Other agents' open work is not overwritten; concurrent edits to the same files avoided

## Before merging
- [ ] Independent review done (not only "CI is green")
- [ ] Compatible with current `main`
- [ ] Physical/device acceptance not required, or already recorded with evidence
- [ ] `main` CI re-checked after merge

## Releases
- [ ] Built from a reviewed commit on `main`
- [ ] SHA-256 checksums published; SBOM available
- [ ] No debug keys, credentials or local paths in artifacts
- [ ] Owner approval for production release

## Hardware / firmware
- [ ] No flashing, eFuse burning, flash/NVS erase without explicit owner approval for the specific device
- [ ] Backup and restore path verified before risky operations
- [ ] Physical results are measured, never simulated

## Android / mobile
- [ ] Minimal permissions; components not exported unless required
- [ ] WebView: no file/universal access, restricted navigation, no mixed content, cleartext off
- [ ] Signing keys and keystores outside the repository
- [ ] Player/user data preserved across upgrade

# Maintain What's New notes

Lichii's What's New dialog reads notes embedded in the binary as Qt resources. It does not fetch notes from GitHub or require a network connection.

1. Before a release, update [`next.json`](../../dist/whats-new/next.json) on the branch you plan to build. Verify that the notes describe changes in that revision. Include a short `summary` and a `changes` array. Leave out the build number and date.
2. In GitHub Actions, manually run [`release-binaries`](../../.github/workflows/release-binaries.yml) on that revision with **Publish the binaries as a GitHub release** selected. Select **Include What's New notes** (`include_whats_new`) only if the staged notes match the build you're publishing. CI stamps the notes with the workflow run's build number and the binary build date.
3. After publishing a build with notes, move its `summary` and `changes` into [`releases.json`](../../dist/whats-new/releases.json). Add the actual positive integer `build` and the stamped `date` in `yyyy-MM-dd` format. Keep one entry per build, newest first. Replace `next.json` with the draft for the next release cycle.

Each change has three plain-text string fields: `icon`, `title`, and `body`. Use one of these icon names: `cpu`, `clock`, `apps`, `layers`, `bug`, `gamepad`, `globe`, `download`, or `sparkles`.

Leave **Include What's New notes** off for test builds or when the staged notes don't match. If the current build has no matching notes, the dialog says so and still offers older releases. It never shows the staged draft or another build's notes as the current release.

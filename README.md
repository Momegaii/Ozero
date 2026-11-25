# ci-evidence

Boot-cell serial logs and QEMU diagnostics republished from
GitHub Actions by the publish-evidence job in
.github/workflows/quality.yml.

This branch exists because the Actions job-log and artifact
endpoints require repository admin rights: a reader who can clone
the repository can read these files, but cannot read the same
content from the API. Each directory is evidence/ci/run-<run>/.
Nothing here is a build input.

# Agent instructions

## Follow the contribution guide

Read [CONTRIBUTING.md](CONTRIBUTING.md) word by word before writing code, and check every change against it before committing.

The `## AI/LLM use when contributing` section does not apply, except that commit messages and pull request descriptions must be in English and commit messages stay one sentence, adding lines only for essential context or attribution. Do not hard-wrap commit messages; the subject and each body paragraph are single lines.

## Keep changes minimal

Reuse the existing code structure and interfaces. Do not refactor unrelated modules, add dependencies, or reformat code the change does not touch.

## CONTRIBUTING.md is never synced from upstream

CONTRIBUTING.md belongs to this fork. Skip an upstream commit that touches only it; when a commit touches it alongside other files, take the other files and leave CONTRIBUTING.md as it is.

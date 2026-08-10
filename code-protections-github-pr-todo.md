# GitHub PR Code Protections & Alert Options (TODO)

This document tracks options and setup templates for automatically flagging and routing pull requests that touch critical code areas (e.g., C++ VM core kernel, Rust compiler AST/optimizer passes).

---

## Option 1: GitHub Labeler Action (Auto-Labeling)
Automatically tags PRs with descriptive labels (e.g., `core`, `compiler`, `high-risk`) based on the files changed.

### Steps to Implement:
1. Create `.github/labeler.yml`:
   ```yaml
   core:
     - changed-files:
       - any-glob-to-any-file: 'impulse-cpp/**'
   compiler:
     - changed-files:
       - any-glob-to-any-file: 'impulse-rust/**'
   ```
2. Create `.github/workflows/labeler.yml`:
   ```yaml
   name: Labeler
   on:
     pull_request_target:
   permissions:
     contents: read
     pull-requests: write
   jobs:
     label:
       runs-on: ubuntu-latest
       steps:
         - uses: actions/labeler@v5
   ```

---

## Option 2: Native Code Owners (`CODEOWNERS`)
Automatically assigns designated teams or maintainers as reviewers when files in specified paths are modified.

### Steps to Implement:
1. Create `.github/CODEOWNERS`:
   ```text
   # Order matters: later rules take precedence
   /impulse-cpp/  @impulse-graph/core-maintainers
   /impulse-rust/ @impulse-graph/compiler-team
   ```
2. Configure branch protection rules on GitHub to require approval from Code Owners before merging.

---

## Option 3: Automated Sticky PR Warning Comments
Generates an automatic comment in the PR thread using `actions/github-script` based on output from the path-detection job.

### Steps to Implement:
Add the following job to `.github/workflows/ci.yml`:
```yaml
comment-warnings:
  needs: detect-changes
  if: |
    github.event_name == 'pull_request' && 
    needs.detect-changes.outputs.cpp == 'true'
  runs-on: ubuntu-latest
  permissions:
    pull-requests: write
  steps:
    - name: Post Core Modification Warning
      uses: actions/github-script@v7
      with:
        script: |
          github.rest.issues.createComment({
            issue_number: context.issue.number,
            owner: context.repo.owner,
            repo: context.repo.repo,
            body: '⚠️ **WARNING**: This PR modifies the core C++ VM kernel. Please ensure rigorous execution vector validation.'
          })
```
